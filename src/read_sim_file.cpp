#include "phlex/configuration.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/source.hpp"
#include "phlex/module.hpp"


#include <ROOT/RNTupleReader.hxx>
#include <ROOT/RNTupleView.hxx>
#include <SHiP/SimParticle.hpp>
#include <memory>
#include <string>
#include <stdio.h>

using namespace phlex;
PHLEX_REGISTER_PROVIDERS(m, config)
{
  auto const input_file  = config.get<std::string>("input_file");
  auto const ntuple_name = config.get<std::string>("ntuple_name");
  auto const field_name  = config.get<std::string>("field_name");
  auto const layer       = config.get<std::string>("layer");

  auto reader = std::shared_ptr<ROOT::RNTupleReader>(
    ROOT::RNTupleReader::Open(ntuple_name, input_file).release()
  );
  auto view = std::make_shared<ROOT::RNTupleView<std::vector<SHiP::SimParticle>>>(
    reader->GetView<std::vector<SHiP::SimParticle>>(field_name)
  );

  m.provide(
    "read_rntuple",
    [reader, view](data_cell_index const& id) -> std::vector<SHiP::SimParticle> {
      auto entry_index = static_cast<ROOT::NTupleSize_t>(id.number());
      return (*view)(entry_index);
    },
    concurrency::serial)
  .output_product(product_query{.creator = "rntuple_source", .layer = layer, .suffix = "sim_particles"});
}