// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// rec_output_module.cpp — Phlex module plugin
//
// Observers for reconstructed output:
//   - RNTuple parallel writer that stores each reconstructed object collection in
//     its own RNTuple within a single ROOT output file
//   - Validation histograms

#include "HistoFileService.hpp"
#include "TFile.h"
#include "TH1D.h"
#include "detectors/calorimeter/calorimeter_histogrammer.hpp"
#include "detectors/spectrometer/SpectrometerHistogrammer.hpp"
#include "detectors/surround_tagger/surround_tagger_histogrammer.hpp"
#include "detectors/timing_detector/timing_detector_histogrammer.hpp"
#include "detectors/upstream_tagger/upstream_tagger_histogrammer.hpp"
#include "phlex/core/product_selector.hpp"
#include "phlex/module.hpp"

#include <ROOT/RHist.hxx>
#include <ROOT/RHistConcurrentFiller.hxx>
#include <ROOT/RHistFillContext.hxx>
#include <ROOT/RNTupleFillContext.hxx>
#include <ROOT/RNTupleFillStatus.hxx>
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleParallelWriter.hxx>
#include <tuple>
#include <type_traits>

#include <SHiP/SimHit.hpp>
#include <SHiP/SimParticle.hpp>
#include <SHiP/TrackFitResult.hpp>
#include <SHiP/detectors/CaloHit.hpp>
#include <SHiP/detectors/SBTHit.hpp>
#include <SHiP/detectors/TimeDetHit.hpp>
#include <SHiP/detectors/UBTHit.hpp>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ROOT::REntry;
using ROOT::RNTupleModel;
using ROOT::RNTupleParallelWriter;
using ROOT::Experimental::RHist;
using ROOT::Experimental::RHistConcurrentFiller;
using ROOT::Experimental::RHistFillContext;

using SpectrometerTracks = std::vector<SHiP::TrackFitResult>;
using UpstreamTaggerObjects = std::vector<SHiP::UBTHit>;
using SurroundTaggerObjects = std::vector<SHiP::SBTHit>;
using CalorimeterObjects = std::vector<SHiP::CaloHit>;
using TimingDetectorObjects = std::vector<SHiP::TimeDetHit>;
using SimHits = std::vector<SHiP::SimHit>;
using SimParticles = std::vector<SHiP::SimParticle>;

class RNTupleFileService {
   public:
    explicit RNTupleFileService(std::string const& filename)
        : file_{TFile::Open(filename.c_str(), "RECREATE")} {
        if (!file_ || file_->IsZombie()) {
            throw std::runtime_error{"Could not create ROOT output file: " + filename};
        }
    }

    TFile& file() { return *file_; }
    std::mutex& mutex() { return mutex_; }

   private:
    // Keep this before file_, or explicitly destroy the writers before this
    // service is destroyed.
    std::unique_ptr<TFile> file_;
    std::mutex mutex_;
};

template <typename Hit>
class TypedHitWriter {
   public:
    TypedHitWriter(RNTupleFileService& file_service, std::string_view ntuple_name)
        : file_service_{file_service} {
        auto model = ROOT::RNTupleModel::CreateBare();
        model->MakeField<Hit>("hit");

        std::scoped_lock lock{file_service_.mutex()};

        writer_ = ROOT::RNTupleParallelWriter::Append(std::move(model), ntuple_name,
                                                      file_service_.file());
    }

    void write(Hit const& hit) {
        auto& state = states_.local();

        if (!state.context) {
            state.context = writer_->CreateFillContext();
            state.entry = state.context->CreateEntry();
        }

        *state.entry->template GetPtr<Hit>("hit") = hit;

        ROOT::RNTupleFillStatus status;
        state.context->FillNoFlush(*state.entry, status);

        if (status.ShouldFlushCluster()) {
            state.context->FlushColumns();

            std::scoped_lock lock{file_service_.mutex()};
            state.context->FlushCluster();
        }
    }

   private:
    struct FillState {
        std::shared_ptr<ROOT::RNTupleFillContext> context;
        std::unique_ptr<ROOT::REntry> entry;
    };

    RNTupleFileService& file_service_;
    std::unique_ptr<ROOT::RNTupleParallelWriter> writer_;
    tbb::enumerable_thread_specific<FillState> states_;
};

class HitRNTupleWriter {
   public:
    explicit HitRNTupleWriter(std::string const& filename, bool isSim)
        : file_service_{filename},
          writers_{TypedHitWriter<SHiP::TrackFitResult>{file_service_, "spectrometer_tracks"},
                   TypedHitWriter<SHiP::UBTHit>{file_service_, "upstream_tagger"},
                   TypedHitWriter<SHiP::SBTHit>{file_service_, "surround_tagger"},
                   TypedHitWriter<SHiP::CaloHit>{file_service_, "calorimeter"},
                   TypedHitWriter<SHiP::TimeDetHit>{file_service_, "timing_detector"}} {
        if (isSim) {
            simhits_.emplace(file_service_, "sim_hits");
            simparticles_.emplace(file_service_, "sim_particles");
        }
    }

    // Bind a specific Hit instantiation at each phlex registration site,
    // e.g. &HitRNTupleWriter::write<SHiP::TrackFitResult> — std::get<T>
    // picks the one TypedHitWriter<Hit> in writers_ by type, so this covers
    // every non-sim writer without a method per type.
    template <typename Hit>
    void write(std::vector<Hit> const& hits) {
        write_all(hits, std::get<TypedHitWriter<Hit>>(writers_));
    }

    // Kept separate: only conditionally constructed (when isSim), so they
    // can't live in the always-present writers_ tuple.
    void write_sim_hits(std::vector<SHiP::SimHit> const& hits) { write_all(hits, *simhits_); }

    void write_sim_particles(std::vector<SHiP::SimParticle> const& particles) {
        write_all(particles, *simparticles_);
    }

   private:
    template <typename Hit>
    static void write_all(std::vector<Hit> const& hits, TypedHitWriter<Hit>& writer) {
        for (auto const& hit : hits) {
            writer.write(hit);
        }
    }

    // Declared first so it is destroyed last.
    RNTupleFileService file_service_;

    std::tuple<TypedHitWriter<SHiP::TrackFitResult>, TypedHitWriter<SHiP::UBTHit>,
               TypedHitWriter<SHiP::SBTHit>, TypedHitWriter<SHiP::CaloHit>,
               TypedHitWriter<SHiP::TimeDetHit>>
        writers_;

    // Left unconstructed (no RNTuple created at all) unless isSim
    std::optional<TypedHitWriter<SHiP::SimHit>> simhits_;
    std::optional<TypedHitWriter<SHiP::SimParticle>> simparticles_;
};

using HistD = RHist<double>;
using FillerD = RHistConcurrentFiller<double>;
using ContextD = RHistFillContext<double>;

std::shared_ptr<HistD> make_hist(int nbins, double low, double high) {
    return std::make_shared<HistD>(static_cast<std::uint64_t>(nbins), std::make_pair(low, high));
}

// Simulation-truth validation: sim_hits/sim_particles multiplicity and
// position distributions. Only ever registered when isSim (see
// PHLEX_REGISTER_ALGORITHMS below).
class SimTruthHistogrammer {
   public:
    explicit SimTruthHistogrammer(std::shared_ptr<HistoFileService> file_service)
        : file_service_{std::move(file_service)},
          h_hit_multiplicity_{make_hist(1000, -0.5, 999.5)},
          h_particle_multiplicity_{make_hist(1000, -0.5, 999.5)},
          h_hit_x_{make_hist(200, -3000., 3000.)},
          h_hit_y_{make_hist(200, -6000., 6000.)},
          h_hit_z_{make_hist(200, -1000., 120000.)},
          h_particle_vtx_x_{make_hist(200, -100., 100.)},
          h_particle_vtx_y_{make_hist(200, -100., 100.)},
          h_particle_vtx_z_{make_hist(200, -100., 1000.)},
          f_hit_multiplicity_{h_hit_multiplicity_},
          f_particle_multiplicity_{h_particle_multiplicity_},
          f_hit_x_{h_hit_x_},
          f_hit_y_{h_hit_y_},
          f_hit_z_{h_hit_z_},
          f_particle_vtx_x_{h_particle_vtx_x_},
          f_particle_vtx_y_{h_particle_vtx_y_},
          f_particle_vtx_z_{h_particle_vtx_z_} {}

    void observe(SimHits const& sim_hits, SimParticles const& sim_particles) {
        auto& ctxs = ensure_contexts();
        ctxs.hit_multiplicity->Fill(static_cast<double>(sim_hits.size()));
        for (auto const& hit : sim_hits) {
            ctxs.hit_x->Fill(hit.position[0]);
            ctxs.hit_y->Fill(hit.position[1]);
            ctxs.hit_z->Fill(hit.position[2]);
        }
        ctxs.particle_multiplicity->Fill(static_cast<double>(sim_particles.size()));
        for (auto const& particle : sim_particles) {
            ctxs.particle_vtx_x->Fill(particle.vertex[0]);
            ctxs.particle_vtx_y->Fill(particle.vertex[1]);
            ctxs.particle_vtx_z->Fill(particle.vertex[2]);
        }
    }

    ~SimTruthHistogrammer() {
        fill_contexts_.clear();
        file_service_->put("h_sim_hit_multiplicity", "Simulated hits per event;N;Events",
                           *h_hit_multiplicity_);
        file_service_->put("h_sim_particle_multiplicity", "Simulated particles per event;N;Events",
                           *h_particle_multiplicity_);
        file_service_->put("h_sim_hit_x", "Simulated hit x position;x [mm];Entries", *h_hit_x_);
        file_service_->put("h_sim_hit_y", "Simulated hit y position;y [mm];Entries", *h_hit_y_);
        file_service_->put("h_sim_hit_z", "Simulated hit z position;z [mm];Entries", *h_hit_z_);
        file_service_->put("h_sim_particle_vtx_x",
                           "Simulated particle vtx x position;x [mm];Entries", *h_particle_vtx_x_);
        file_service_->put("h_sim_particle_vtx_y",
                           "Simulated particle vtx y position;y [mm];Entries", *h_particle_vtx_y_);
        file_service_->put("h_sim_particle_vtx_z",
                           "Simulated particle vtx z position;z [mm];Entries", *h_particle_vtx_z_);
    }

   private:
    struct FillContexts {
        std::shared_ptr<ContextD> hit_multiplicity, particle_multiplicity;
        std::shared_ptr<ContextD> hit_x, hit_y, hit_z;
        std::shared_ptr<ContextD> particle_vtx_x, particle_vtx_y, particle_vtx_z;
    };

    FillContexts& ensure_contexts() {
        auto& ctxs = fill_contexts_.local();
        if (!ctxs.hit_multiplicity) {
            ctxs.hit_multiplicity = f_hit_multiplicity_.CreateFillContext();
            ctxs.particle_multiplicity = f_particle_multiplicity_.CreateFillContext();
            ctxs.hit_x = f_hit_x_.CreateFillContext();
            ctxs.hit_y = f_hit_y_.CreateFillContext();
            ctxs.hit_z = f_hit_z_.CreateFillContext();
            ctxs.particle_vtx_x = f_particle_vtx_x_.CreateFillContext();
            ctxs.particle_vtx_y = f_particle_vtx_y_.CreateFillContext();
            ctxs.particle_vtx_z = f_particle_vtx_z_.CreateFillContext();
        }
        return ctxs;
    }

    std::shared_ptr<HistoFileService> file_service_;
    std::shared_ptr<HistD> h_hit_multiplicity_, h_particle_multiplicity_;
    std::shared_ptr<HistD> h_hit_x_, h_hit_y_, h_hit_z_, h_particle_vtx_x_, h_particle_vtx_y_,
        h_particle_vtx_z_;
    FillerD f_hit_multiplicity_, f_particle_multiplicity_;
    FillerD f_hit_x_, f_hit_y_, f_hit_z_, f_particle_vtx_x_, f_particle_vtx_y_, f_particle_vtx_z_;
    tbb::enumerable_thread_specific<FillContexts> fill_contexts_;
};

// No-op observer for benchmarking pure framework overhead.
class RecoNoop {
   public:
    void observe(SpectrometerTracks const&, UpstreamTaggerObjects const&,
                 SurroundTaggerObjects const&, CalorimeterObjects const&,
                 TimingDetectorObjects const&, SimHits const&, SimParticles const&) {}
    void observe_tracks_only(SpectrometerTracks const&, UpstreamTaggerObjects const&,
                             SurroundTaggerObjects const&, CalorimeterObjects const&,
                             TimingDetectorObjects const&) {}
};

}  // namespace

PHLEX_REGISTER_ALGORITHMS(m, config) {
    using namespace phlex;

    auto mode = config.get<std::string>("mode", std::string{"reco"});
    auto rntuple_file =
        config.get<std::string>("rntuple_file", std::string{"reconstructed_objects.root"});
    auto histo_file = config.get<std::string>("histo_file", std::string{"reco_validation.root"});
    auto layer = config.get<std::string>("layer", std::string{"spill"});
    auto isSim = config.get<bool>("simulation", false);

    if (mode != "reco" && mode != "noop")
        throw std::runtime_error("Unknown rec_output_module mode: '" + mode +
                                 "' (expected 'reco' or 'noop')");

    // Recreating the histogram file at teardown would truncate the RNTuple
    // output if both are configured to the same path.
    if (std::filesystem::weakly_canonical(rntuple_file) ==
        std::filesystem::weakly_canonical(histo_file))
        throw std::runtime_error(
            "rec_output_module: rntuple_file and histo_file must "
            "differ (both resolve to '" +
            rntuple_file + "')");

    // product_selector's members move from whatever they are given (even
    // lvalues), so hand each selector its own identifier copies.
    auto selector = [](char const* creator, char const* layer, char const* suffix) {
        return product_selector{.creator = phlex::experimental::identifier{creator},
                                .layer = phlex::experimental::identifier{layer},
                                .suffix = phlex::experimental::identifier{suffix}};
    };

    auto passthrough = [&](char const* suffix) {
        return selector("rntuple_source", "spill", suffix);
    };

    // Registers a phlex observer under `name`, wired to the single
    // `sel` input selector — collapses the repeated
    // `writer.observe(...).input_family(...)` pattern below to one line
    // per writer method.
    auto register_writer = [](auto& target, char const* name, auto member_ptr,
                              product_selector sel) {
        target.observe(name, member_ptr, concurrency::unlimited).input_family(std::move(sel));
    };

    if (mode == "noop") {
        auto noop = m.make<RecoNoop>();
        if (isSim) {
            noop.observe("noop", &RecoNoop::observe, concurrency::unlimited)
                .input_family(selector("fit_seed", "seed", "track_fit_result"),
                              selector("upstream_tagger_reco", "spill", "upstream_tagger_reco"),
                              selector("surround_tagger_reco", "spill", "surround_tagger_reco"),
                              selector("calorimeter_reco", "spill", "calorimeter_reco"),
                              selector("timing_detector_reco", "spill", "timing_detector_reco"),
                              passthrough("sim_hits"), passthrough("sim_particles"));
        } else {
            noop.observe("noop", &RecoNoop::observe_tracks_only, concurrency::unlimited)
                .input_family(selector("fit_seed", "seed", "track_fit_result"),
                              selector("upstream_tagger_reco", "spill", "upstream_tagger_reco"),
                              selector("surround_tagger_reco", "spill", "surround_tagger_reco"),
                              selector("calorimeter_reco", "spill", "calorimeter_reco"),
                              selector("timing_detector_reco", "spill", "timing_detector_reco"));
        }
        return;
    }

    auto writer = m.make<HitRNTupleWriter>(rntuple_file, isSim);

    register_writer(writer, "write_spectrometer_tracks",
                    &HitRNTupleWriter::write<SHiP::TrackFitResult>,
                    selector("fit_seed", "seed", "track_fit_result"));
    register_writer(writer, "write_upstream_tagger", &HitRNTupleWriter::write<SHiP::UBTHit>,
                    selector("upstream_tagger_reco", "spill", "upstream_tagger_reco"));
    register_writer(writer, "write_surround_tagger", &HitRNTupleWriter::write<SHiP::SBTHit>,
                    selector("surround_tagger_reco", "spill", "surround_tagger_reco"));
    register_writer(writer, "write_calorimeter", &HitRNTupleWriter::write<SHiP::CaloHit>,
                    selector("calorimeter_reco", "spill", "calorimeter_reco"));
    register_writer(writer, "write_timing_detector", &HitRNTupleWriter::write<SHiP::TimeDetHit>,
                    selector("timing_detector_reco", "spill", "timing_detector_reco"));

    if (isSim) {
        register_writer(writer, "write_sim_hits", &HitRNTupleWriter::write_sim_hits,
                        passthrough("sim_hits"));
        register_writer(writer, "write_sim_particles", &HitRNTupleWriter::write_sim_particles,
                        passthrough("sim_particles"));
    }

    // One shared output file; each histogrammer below Put()s its own
    // histograms into it at its own destruction (see HistoFileService).
    auto histo_file_service = std::make_shared<HistoFileService>(histo_file);

    auto spectrometer_histo = m.make<SpectrometerHistogrammer>(histo_file_service);
    register_writer(spectrometer_histo, "validate_spectrometer", &SpectrometerHistogrammer::observe,
                    selector("fit_seed", "seed", "track_fit_result"));

    auto ubt_histo = m.make<UpstreamTaggerHistogrammer>(histo_file_service);
    register_writer(ubt_histo, "validate_upstream_tagger", &UpstreamTaggerHistogrammer::observe,
                    selector("upstream_tagger_reco", "spill", "upstream_tagger_reco"));

    auto sbt_histo = m.make<SurroundTaggerHistogrammer>(histo_file_service);
    register_writer(sbt_histo, "validate_surround_tagger", &SurroundTaggerHistogrammer::observe,
                    selector("surround_tagger_reco", "spill", "surround_tagger_reco"));

    auto calo_histo = m.make<CalorimeterHistogrammer>(histo_file_service);
    register_writer(calo_histo, "validate_calorimeter", &CalorimeterHistogrammer::observe,
                    selector("calorimeter_reco", "spill", "calorimeter_reco"));

    auto time_det_histo = m.make<TimingDetectorHistogrammer>(histo_file_service);
    register_writer(time_det_histo, "validate_timing_detector",
                    &TimingDetectorHistogrammer::observe,
                    selector("timing_detector_reco", "spill", "timing_detector_reco"));

    if (isSim) {
        auto sim_truth_histo = m.make<SimTruthHistogrammer>(histo_file_service);
        sim_truth_histo
            .observe("validate_sim_truth", &SimTruthHistogrammer::observe, concurrency::unlimited)
            .input_family(passthrough("sim_hits"), passthrough("sim_particles"));
    }
}
