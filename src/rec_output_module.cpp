// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// sim_output_module.cpp — Phlex module plugin
//
// Observers for simulation output:
//   - RNTuple parallel writer for reconstruction 
//   - Validation histograms

#include <oneapi/tbb/enumerable_thread_specific.h>

#include <ROOT/RNTupleFillContext.hxx>
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleParallelWriter.hxx>
#include <SHiP/MCParticle.hpp>
#include <SHiP/SimHit.hpp>
#include <SHiP/SimParticle.hpp>
#include <SHiP/SimResult.hpp>
#include <boost/histogram.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "TFile.h"
#include "TH1D.h"
#include "phlex/core/product_query.hpp"
#include "phlex/module.hpp"

namespace {

using ROOT::REntry;
using ROOT::RNTupleModel;
using ROOT::RNTupleParallelWriter;

struct FillState {
  std::shared_ptr<ROOT::RNTupleFillContext> ctx;
  std::unique_ptr<REntry> entry;
};

namespace bh = boost::histogram;

using ts_histogram =
    bh::histogram<std::tuple<bh::axis::regular<>>,
                  bh::dense_storage<bh::accumulators::count<double, true>>>;

ts_histogram make_ts_histogram(int nbins, double low, double high) {
  return bh::make_histogram_with(
      bh::dense_storage<bh::accumulators::count<double, true>>{},
      bh::axis::regular<>(nbins, low, high));
}

std::unique_ptr<TH1D> to_root(ts_histogram const& h, char const* name,
                              char const* title) {
  auto const& ax = h.axis();
  auto root_h = std::make_unique<TH1D>(name, title, static_cast<int>(ax.size()),
                                       ax.value(0), ax.value(ax.size()));
  for (int i = 0; i < static_cast<int>(ax.size()); ++i)
    root_h->SetBinContent(i + 1, h.at(i).value());
  root_h->SetBinContent(0, h.at(-1).value());
  root_h->SetBinContent(static_cast<int>(ax.size()) + 1,
                        h.at(ax.size()).value());
  root_h->ResetStats();
  return root_h;
}

// RNTuple writer for MC particles only (used when no G4 simulation)
class MCRNTupleWriter {
 public:
  explicit MCRNTupleWriter(std::string filename) {
    auto model = RNTupleModel::CreateBare();
    model->MakeField<std::vector<SHiP::MCParticle>>("mc_particles");
    writer_ =
        RNTupleParallelWriter::Recreate(std::move(model), "events", filename);
  }

  void write(std::vector<SHiP::MCParticle> const& particles) {
    auto& state = fill_states_.local();
    if (!state.ctx) {
      state.ctx = writer_->CreateFillContext();
      state.entry = state.ctx->CreateEntry();
    }
    *state.entry->GetPtr<std::vector<SHiP::MCParticle>>("mc_particles") =
        particles;
    state.ctx->Fill(*state.entry);
  }

 private:
  std::unique_ptr<RNTupleParallelWriter> writer_;
  tbb::enumerable_thread_specific<FillState> fill_states_;
};

// RNTuple writer for full simulation output (MC + G4 hits)
class SimRNTupleWriter {
 public:
  SimRNTupleWriter(std::string filename, bool filter_empty = false)
      : filter_empty_{filter_empty} {
    auto model = RNTupleModel::CreateBare();
    model->MakeField<std::vector<SHiP::MCParticle>>("mc_particles");
    model->MakeField<std::vector<SHiP::SimHit>>("sim_hits");
    model->MakeField<std::vector<SHiP::SimParticle>>("sim_particles");
    writer_ =
        RNTupleParallelWriter::Recreate(std::move(model), "events", filename);
  }

  void write(std::vector<SHiP::MCParticle> const& particles,
             SHiP::SimResult const& result) {
    if (filter_empty_ && result.hits.empty()) return;

    auto& state = fill_states_.local();
    if (!state.ctx) {
      state.ctx = writer_->CreateFillContext();
      state.entry = state.ctx->CreateEntry();
    }
    *state.entry->GetPtr<std::vector<SHiP::MCParticle>>("mc_particles") =
        particles;
    *state.entry->GetPtr<std::vector<SHiP::SimHit>>("sim_hits") = result.hits;
    *state.entry->GetPtr<std::vector<SHiP::SimParticle>>("sim_particles") =
        result.particles;
    state.ctx->Fill(*state.entry);
  }

 private:
  bool filter_empty_;
  std::unique_ptr<RNTupleParallelWriter> writer_;
  tbb::enumerable_thread_specific<FillState> fill_states_;
};

// Validation histograms for MC particles (thread-safe via atomic accumulators)
class MCHistogrammer {
 public:
  explicit MCHistogrammer(std::string filename)
      : filename_{std::move(filename)},
        h_multiplicity_{make_ts_histogram(200, -0.5, 199.5)},
        h_momentum_{make_ts_histogram(200, 0., 500.)},
        h_pdg_{make_ts_histogram(1000, -500.5, 499.5)} {}

  void observe(std::vector<SHiP::MCParticle> const& particles) {
    h_multiplicity_(static_cast<double>(particles.size()));
    for (auto const& p : particles) {
      auto const& mom = p.momentum;
      double pmag =
          std::sqrt(mom[0] * mom[0] + mom[1] * mom[1] + mom[2] * mom[2]);
      h_momentum_(pmag);
      h_pdg_(p.pdgCode);
    }
  }

  ~MCHistogrammer() {
    std::unique_ptr<TFile> file{TFile::Open(filename_.c_str(), "RECREATE")};
    if (!file || file->IsZombie()) return;
    file->cd();
    file->WriteTObject(to_root(h_multiplicity_, "h_mc_multiplicity",
                               "MC particles per event;N;Events")
                           .get());
    file->WriteTObject(to_root(h_momentum_, "h_mc_momentum",
                               "MC particle |p|;|p| [GeV/c];Entries")
                           .get());
    file->WriteTObject(
        to_root(h_pdg_, "h_mc_pdg", "MC particle PDG code;PDG;Entries").get());
    file->Close();
  }

 private:
  std::string filename_;
  ts_histogram h_multiplicity_, h_momentum_, h_pdg_;
};

// Validation histograms for full simulation (thread-safe via atomic
// accumulators)
class SimHistogrammer {
 public:
  explicit SimHistogrammer(std::string filename)
      : filename_{std::move(filename)},
        h_mc_multiplicity_{make_ts_histogram(200, -0.5, 199.5)},
        h_mc_momentum_{make_ts_histogram(200, 0., 500.)},
        h_hit_multiplicity_{make_ts_histogram(1000, -0.5, 999.5)},
        h_hit_edep_{make_ts_histogram(200, 0., 1.0)},
        h_hit_z_{make_ts_histogram(200, -1000., 12000.)},
        h_hit_detector_{make_ts_histogram(100, -0.5, 99.5)},
        h_hit_momentum_{make_ts_histogram(200, 0., 500.)},
        h_particle_multiplicity_{make_ts_histogram(1000, -0.5, 999.5)} {}

  void observe(std::vector<SHiP::MCParticle> const& particles,
               SHiP::SimResult const& result) {
    h_mc_multiplicity_(static_cast<double>(particles.size()));
    for (auto const& p : particles) {
      auto const& mom = p.momentum;
      double pmag =
          std::sqrt(mom[0] * mom[0] + mom[1] * mom[1] + mom[2] * mom[2]);
      h_mc_momentum_(pmag);
    }
    h_hit_multiplicity_(static_cast<double>(result.hits.size()));
    for (auto const& hit : result.hits) {
      h_hit_edep_(hit.energyDeposit);
      h_hit_z_(hit.position[2]);
      h_hit_detector_(hit.detectorId);
      auto const& mom = hit.momentum;
      h_hit_momentum_(
          std::sqrt(mom[0] * mom[0] + mom[1] * mom[1] + mom[2] * mom[2]));
    }
    h_particle_multiplicity_(static_cast<double>(result.particles.size()));
  }

  ~SimHistogrammer() {
    std::unique_ptr<TFile> file{TFile::Open(filename_.c_str(), "RECREATE")};
    if (!file || file->IsZombie()) return;
    file->cd();
    file->WriteTObject(to_root(h_mc_multiplicity_, "h_mc_multiplicity",
                               "MC particles per event;N;Events")
                           .get());
    file->WriteTObject(to_root(h_mc_momentum_, "h_mc_momentum",
                               "MC particle |p|;|p| [GeV/c];Entries")
                           .get());
    file->WriteTObject(to_root(h_hit_multiplicity_, "h_hit_multiplicity",
                               "Sim hits per event;N;Events")
                           .get());
    file->WriteTObject(to_root(h_hit_edep_, "h_hit_edep",
                               "Hit energy deposit;E_{dep} [GeV];Entries")
                           .get());
    file->WriteTObject(
        to_root(h_hit_z_, "h_hit_z", "Hit z position;z [mm];Entries").get());
    file->WriteTObject(to_root(h_hit_detector_, "h_hit_detector",
                               "Hit detector ID;Detector ID;Entries")
                           .get());
    file->WriteTObject(to_root(h_hit_momentum_, "h_hit_momentum",
                               "Hit |p|;|p| [GeV/c];Entries")
                           .get());
    file->WriteTObject(to_root(h_particle_multiplicity_,
                               "h_particle_multiplicity",
                               "Sim particles per event;N;Events")
                           .get());
    file->Close();
  }

 private:
  std::string filename_;
  ts_histogram h_mc_multiplicity_, h_mc_momentum_;
  ts_histogram h_hit_multiplicity_, h_hit_edep_, h_hit_z_, h_hit_detector_,
      h_hit_momentum_;
  ts_histogram h_particle_multiplicity_;
};

// No-op observers for benchmarking pure framework overhead.
class MCNoop {
 public:
  void observe(std::vector<SHiP::MCParticle> const&) {}
};

class SimNoop {
 public:
  void observe(std::vector<SHiP::MCParticle> const&, SHiP::SimResult const&) {}
};

}  // namespace

PHLEX_REGISTER_ALGORITHMS(m, config) {
  using namespace phlex;

  auto mode = config.get<std::string>("mode", std::string{"mc_only"});
  auto rntuple_file =
      config.get<std::string>("rntuple_file", std::string{"sim_output.root"});
  auto histo_file =
      config.get<std::string>("histo_file", std::string{"validation.root"});

  if (mode != "mc_only" && mode != "full" && mode != "noop" &&
      mode != "noop_full")
    throw std::runtime_error(
        "Unknown sim_output_module mode: '" + mode +
        "' (expected 'mc_only', 'full', 'noop', or 'noop_full')");

  if (mode == "noop") {
    auto noop = m.make<MCNoop>();
    noop.observe("noop", &MCNoop::observe, concurrency::unlimited)
        .input_family(
            product_query{.creator = "mc_particles"_id, .layer = "event"_id});
  } else if (mode == "noop_full") {
    auto noop = m.make<SimNoop>();
    noop.observe("noop", &SimNoop::observe, concurrency::unlimited)
        .input_family(
            product_query{.creator = "mc_particles"_id, .layer = "event"_id},
            product_query{.creator = "geant4"_id,
                          .layer = "event"_id,
                          .suffix = "sim_result"_id});
  } else if (mode == "mc_only") {
    auto writer = m.make<MCRNTupleWriter>(rntuple_file);
    writer
        .observe("write_rntuple", &MCRNTupleWriter::write,
                 concurrency::unlimited)
        .input_family(
            product_query{.creator = "mc_particles"_id, .layer = "event"_id});

    auto histogrammer = m.make<MCHistogrammer>(histo_file);
    histogrammer
        .observe("validate", &MCHistogrammer::observe, concurrency::unlimited)
        .input_family(
            product_query{.creator = "mc_particles"_id, .layer = "event"_id});
  } else {
    auto filter_empty = config.get<bool>("filter_empty", false);

    auto writer = m.make<SimRNTupleWriter>(rntuple_file, filter_empty);
    writer
        .observe("write_rntuple", &SimRNTupleWriter::write,
                 concurrency::unlimited)
        .input_family(
            product_query{.creator = "mc_particles"_id, .layer = "event"_id},
            product_query{.creator = "geant4"_id,
                          .layer = "event"_id,
                          .suffix = "sim_result"_id});

    auto histogrammer = m.make<SimHistogrammer>(histo_file);
    histogrammer
        .observe("validate", &SimHistogrammer::observe, concurrency::unlimited)
        .input_family(
            product_query{.creator = "mc_particles"_id, .layer = "event"_id},
            product_query{.creator = "geant4"_id,
                          .layer = "event"_id,
                          .suffix = "sim_result"_id});
  }
}
