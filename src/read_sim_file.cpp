#include "phlex/configuration.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/source.hpp"

#include <ROOT/RNTupleReader.hxx>
#include <ROOT/RNTupleView.hxx>

#include <memory>
#include <string>

using namespace phlex;
using ROOT::Experimental::RNTupleReader;

PHLEX_REGISTER_PROVIDERS(m, config)
{
  auto const input_file  = config.get<std::string>("input_file");
  auto const ntuple_name = config.get<std::string>("ntuple_name"); // e.g. "spill"
  auto const field_name  = config.get<std::string>("field_name");  // e.g. "processed_value"
  auto const layer       = config.get<std::string>("layer");

  // Open the RNTuple once; share it across calls via shared_ptr
  auto reader = std::shared_ptr<RNTupleReader>(
    RNTupleReader::Open(ntuple_name, input_file).release()
  );

  m.provide(
    "read_from_rntuple",
    [reader, field_name](data_cell_index const& id) -> double {
      // Use the data cell's sequential number as the RNTuple entry index
      auto entry_index = static_cast<ROOT::NTupleSize_t>(id.number() - 1); // 1-based → 0-based

      auto view = reader->GetView<double>(field_name);
      return view(entry_index);
    })
    .output_product({.creator = "rntuple_source", .layer = layer, .suffix = field_name});
}
