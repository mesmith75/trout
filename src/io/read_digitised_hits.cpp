// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// read_digitised_hits.cpp — Phlex provider plugin
//
// Reads shannon's digitised-hit output (one RNTuple per detector type, each
// a flat record-per-hit stream — SHiP has no discrete "event", just a
// continuous hit stream, so there's no on-disk event boundary) and provides
// each detector's hit collection as its own typed product, plus a
// pass-through copy of the raw SimHits. Every entry is read into one flat
// vector covering the whole file; time-window hit assembly is a separate,
// later reconstruction step.

#include "phlex/configuration.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/source.hpp"
#include "phlex/module.hpp"
#include "phlex/core/product_selector.hpp"

#include <ROOT/RNTupleReader.hxx>
#include <ROOT/RNTupleView.hxx>
#include <SHiP/SimHit.hpp>
#include <SHiP/SimParticle.hpp>
#include <SHiP/detectors/CaloHit.hpp>
#include <SHiP/detectors/SBTHit.hpp>
#include <SHiP/detectors/StrawTubesHit.hpp>
#include <SHiP/detectors/TimeDetHit.hpp>
#include <SHiP/detectors/UBTHit.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace phlex;

namespace {

template <typename Hit, typename Registrar>
void provide_hit_type(Registrar& m, std::string const& input_file,
                      std::string const& ntuple_name, std::string const& suffix,
                      phlex::experimental::identifier const& layer) {
  auto reader = std::shared_ptr<ROOT::RNTupleReader>(
      ROOT::RNTupleReader::Open(ntuple_name, input_file).release());
  auto view = reader->GetView<Hit>("hit");

  auto hits = std::make_shared<std::vector<Hit>>();
  auto const n_entries = reader->GetNEntries();
  hits->reserve(n_entries);
  for (ROOT::NTupleSize_t i = 0; i < n_entries; ++i)
    hits->push_back(view(i));

  m.provide(
      "read_" + suffix,
      [hits](data_cell_index const&) -> std::vector<Hit> { return *hits; },
      concurrency::serial)
    .output_product("rntuple_source", phlex::experimental::identifier{suffix}, layer);
}

}  // namespace

PHLEX_REGISTER_PROVIDERS(m, config)
{
  auto const input_file = config.get<std::string>("input_file");
  auto const layer = phlex::experimental::identifier{
      config.get<std::string>("layer")};
  auto const isSim = config.get<bool>("simulation", false);

  // Mirrors the fixed set of RNTuples HitRNTupleWriter writes in
  // digitised_output_module.cpp — field name "hit" is constant for all of
  // them (TypedHitWriter<Hit> always names its field "hit").
  provide_hit_type<SHiP::UBTHit>(m, input_file, "ubt_hits", "ubt_hits", layer);
  provide_hit_type<SHiP::SBTHit>(m, input_file, "sbt_hits", "sbt_hits", layer);
  provide_hit_type<SHiP::StrawTubesHit>(m, input_file, "straw_tubes_hits",
                                        "straw_tubes_hits", layer);
  provide_hit_type<SHiP::CaloHit>(m, input_file, "calorimeter_hits",
                                  "calorimeter_hits", layer);
  provide_hit_type<SHiP::TimeDetHit>(m, input_file, "timing_detector_hits",
                                     "timing_detector_hits", layer);
  if (isSim) {
    provide_hit_type<SHiP::SimHit>(m, input_file, "sim_hits", "sim_hits", layer);
    provide_hit_type<SHiP::SimParticle>(m, input_file, "sim_particles", "sim_particles", layer);
  }
}
