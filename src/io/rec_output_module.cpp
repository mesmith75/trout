// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// rec_output_module.cpp — Phlex module plugin
//
// Observers for reconstructed output:
//   - RNTuple parallel writer that stores each reconstructed object collection in
//     its own RNTuple within a single ROOT output file
//   - Validation histograms

#include "TFile.h"
#include "TH1D.h"
#include "phlex/core/product_selector.hpp"
#include "phlex/module.hpp"

#include <ROOT/Hist/ConvertToTH1.hxx>
#include <ROOT/RFile.hxx>
#include <ROOT/RHist.hxx>
#include <ROOT/RHistConcurrentFiller.hxx>
#include <ROOT/RHistFillContext.hxx>
#include <ROOT/RNTupleFillContext.hxx>
#include <ROOT/RNTupleFillStatus.hxx>
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleParallelWriter.hxx>
#include <type_traits>

#include <SHiP/SimHit.hpp>
#include <SHiP/SimParticle.hpp>
#include <SHiP/TrackFitResult.hpp>
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
using ROOT::Experimental::RFile;
using ROOT::Experimental::RHist;
using ROOT::Experimental::RHistConcurrentFiller;
using ROOT::Experimental::RHistFillContext;

using SpectrometerTracks = std::vector<SHiP::TrackFitResult>;
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
          spectrometer_tracks_{file_service_, "spectrometer_tracks"} {
        if (isSim) {
            simhits_.emplace(file_service_, "sim_hits");
            simparticles_.emplace(file_service_, "sim_particles");
        }
    }

    void write_spectrometer_tracks(std::vector<SHiP::TrackFitResult> const& hits) {
        for (auto const& hit : hits) {
            spectrometer_tracks_.write(hit);
        }
    }

    // Only ever registered as an observer (see PHLEX_REGISTER_ALGORITHMS
    // below) when isSim is true, so simhits_ is guaranteed to hold a value
    // whenever this is actually called.
    void write_sim_hits(std::vector<SHiP::SimHit> const& hits) {
        for (auto const& hit : hits) {
            simhits_->write(hit);
        }
    }

    void write_sim_particles(std::vector<SHiP::SimParticle> const& particles) {
        for (auto const& ptcl : particles) {
            simparticles_->write(ptcl);
        }
    }

   private:
    // Declared first so it is destroyed last.
    RNTupleFileService file_service_;

    TypedHitWriter<SHiP::TrackFitResult> spectrometer_tracks_;
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

// Validation histograms for reconstructed objects (thread-safe via per-thread
// RHistFillContext atomic fillers)
class RecoHistogrammer {
   public:
    explicit RecoHistogrammer(std::string filename, bool isSim = false)
        : filename_{std::move(filename)},
          h_spectrometer_track_multiplicity_{make_hist(1000, -0.5, 999.5)},
          h_ref_x_{make_hist(200, -3000., 3000.)},
          h_ref_y_{make_hist(200, -6000., 6000.)},
          h_ref_z_{make_hist(200, -1000., 120000.)},
          f_spectrometer_track_multiplicity_{h_spectrometer_track_multiplicity_},
          f_ref_x_{h_ref_x_},
          f_ref_y_{h_ref_y_},
          f_ref_z_{h_ref_z_},
          m_isSim{isSim} {
            if (isSim) {
                // Plain assignment, not brace-init: h_sim_*_ are shared_ptr
                // (default-null, fine to assign post-construction).
                h_sim_hit_multiplicity_ = make_hist(1000, -0.5, 999.5);
                h_sim_particle_multiplicity_ = make_hist(1000, -0.5, 999.5);
                h_sim_hit_x_ = make_hist(200, -3000., 3000.);
                h_sim_hit_y_ = make_hist(200, -6000., 6000.);
                h_sim_hit_z_ = make_hist(200, -1000., 120000.);
                h_sim_particle_vtx_x_ = make_hist(200, -100., 100.);
                h_sim_particle_vtx_y_ = make_hist(200, -100., 100.);
                h_sim_particle_vtx_z_ = make_hist(200, -100., 1000.);

                // f_sim_*_ are RHistConcurrentFiller, which has no default
                // constructor (only one taking a shared_ptr<RHist>), so they
                // must be optional to stay unconstructed until here.
                f_sim_hit_multiplicity_.emplace(h_sim_hit_multiplicity_);
                f_sim_particle_multiplicity_.emplace(h_sim_particle_multiplicity_);
                f_sim_hit_x_.emplace(h_sim_hit_x_);
                f_sim_hit_y_.emplace(h_sim_hit_y_);
                f_sim_hit_z_.emplace(h_sim_hit_z_);
                f_sim_particle_vtx_x_.emplace(h_sim_particle_vtx_x_);
                f_sim_particle_vtx_y_.emplace(h_sim_particle_vtx_y_);
                f_sim_particle_vtx_z_.emplace(h_sim_particle_vtx_z_);
            }
    }

    // Full signature — used when sim_hits/sim_particles are actually being
    // read (mode: simulation input available).
    void observe(SpectrometerTracks const& spectrometer_tracks, SimHits const& sim_hits,
                 SimParticles const& sim_particles) {
        auto& ctxs = ensure_contexts();
        fill_one(ctxs, spectrometer_tracks, *ctxs.spectrometer_track_multiplicity);
        fill_one(ctxs, sim_hits, *ctxs.sim_hit_multiplicity);
        fill_one(ctxs, sim_particles, *ctxs.sim_particle_multiplicity);
    }

    // Track-only signature — used when there is no sim_hits/sim_particles
    // input to depend on at all (e.g. real data, no simulation truth), since
    // phlex requires input_family's selector count to match the registered
    // function's arity exactly.
    void observe_tracks_only(SpectrometerTracks const& spectrometer_tracks) {
        auto& ctxs = ensure_contexts();
        fill_one(ctxs, spectrometer_tracks, *ctxs.spectrometer_track_multiplicity);
    }

    ~RecoHistogrammer() {
        // Destroy per-thread fill contexts so their stats flush back into the
        // histograms before we read them, and so the concurrent fillers see no
        // live contexts at their own destruction (which would std::terminate).
        fill_contexts_.clear();
        try {
            auto file = RFile::Recreate(filename_);
            auto put = [&](char const* name, char const* title, HistD const& h) {
                auto th1 = ROOT::Experimental::Hist::ConvertToTH1D(h);
                th1->SetNameTitle(name, title);
                file->Put(name, *th1);
            };
            put("h_spectrometer_track_multiplicity", "Spectrometer tracks per event;N;Events", *h_spectrometer_track_multiplicity_);
            put("h_ref_x", "Spectrometer track reference x position;x [mm];Entries", *h_ref_x_);
            put("h_ref_y", "Spectrometer track reference y position;y [mm];Entries", *h_ref_y_);
            put("h_ref_z", "Spectrometer track reference z position;z [mm];Entries", *h_ref_z_);

            if(h_sim_hit_multiplicity_){
                put("h_sim_hit_multiplicity", "Simulated hits per event;N;Events",
                    *h_sim_hit_multiplicity_);
                put("h_sim_particle_multiplicity", "Simulated particles per event;N;Events",
                    *h_sim_particle_multiplicity_);
                put("h_sim_hit_x", "Simulated hit x position;x [mm];Entries", *h_sim_hit_x_);
                put("h_sim_hit_y", "Simulated hit y position;y [mm];Entries", *h_sim_hit_y_);
                put("h_sim_hit_z", "Simulated hit z position;z [mm];Entries", *h_sim_hit_z_);
                put("h_sim_particle_vtx_x", "Simulated particle vtx x position;x [mm];Entries",
                    *h_sim_particle_vtx_x_);
                put("h_sim_particle_vtx_y", "Simulated particle vtx y position;y [mm];Entries",
                    *h_sim_particle_vtx_y_);
                put("h_sim_particle_vtx_z", "Simulated particle vtx z position;z [mm];Entries",
                    *h_sim_particle_vtx_z_);
            }
        } catch (std::exception const& e) {
            // RException, filesystem errors etc. — must not escape the destructor.
            try {
                spdlog::error(
                    "rec_output_module: failed to write validation histograms to '{}': {}",
                    filename_, e.what());
            } catch (...) {
            }
        }
    }

   private:
    struct FillContexts;

    FillContexts& ensure_contexts() {
        auto& ctxs = fill_contexts_.local();
        if (!ctxs.spectrometer_track_multiplicity) {
            ctxs.spectrometer_track_multiplicity = f_spectrometer_track_multiplicity_.CreateFillContext();
            ctxs.ref_x = f_ref_x_.CreateFillContext();
            ctxs.ref_y = f_ref_y_.CreateFillContext();
            ctxs.ref_z = f_ref_z_.CreateFillContext();
            if(m_isSim){
                ctxs.sim_hit_multiplicity = f_sim_hit_multiplicity_->CreateFillContext();
                ctxs.sim_particle_multiplicity = f_sim_particle_multiplicity_->CreateFillContext();
                ctxs.sim_hit_x = f_sim_hit_x_->CreateFillContext();
                ctxs.sim_hit_y = f_sim_hit_y_->CreateFillContext();
                ctxs.sim_hit_z = f_sim_hit_z_->CreateFillContext();
                ctxs.sim_particle_vtx_x = f_sim_particle_vtx_x_->CreateFillContext();
                ctxs.sim_particle_vtx_y = f_sim_particle_vtx_y_->CreateFillContext();
                ctxs.sim_particle_vtx_z = f_sim_particle_vtx_z_->CreateFillContext();
            }
        }
        return ctxs;
    }

    static void fill_one(FillContexts& ctxs, auto const& hits, ContextD& multiplicity) {
        multiplicity.Fill(static_cast<double>(hits.size()));
        using HitT = std::remove_cvref_t<decltype(*std::begin(hits))>;
        for (auto const& hit : hits) {
            // FIXME: This is a fudge until the hit classes get sorted
            if constexpr (std::is_same_v<HitT, SHiP::SimHit>) {
                ctxs.sim_hit_x->Fill(hit.position[0]);
                ctxs.sim_hit_y->Fill(hit.position[1]);
                ctxs.sim_hit_z->Fill(hit.position[2]);
            } else if constexpr (std::is_same_v<HitT, SHiP::SimParticle>) {
                ctxs.sim_particle_vtx_x->Fill(hit.vertex[0]);
                ctxs.sim_particle_vtx_y->Fill(hit.vertex[1]);
                ctxs.sim_particle_vtx_z->Fill(hit.vertex[2]);
            } else {
                ctxs.ref_x->Fill(hit.refLoc[0]);
                ctxs.ref_y->Fill(hit.refLoc[1]);
                ctxs.ref_z->Fill(hit.refLoc[2]);
            }
        }
    }

    struct FillContexts {
        std::shared_ptr<ContextD> spectrometer_track_multiplicity;
        std::shared_ptr<ContextD> sim_hit_multiplicity;
        std::shared_ptr<ContextD> sim_particle_multiplicity;
        std::shared_ptr<ContextD> ref_x;
        std::shared_ptr<ContextD> ref_y;
        std::shared_ptr<ContextD> ref_z;
        std::shared_ptr<ContextD> sim_hit_x;
        std::shared_ptr<ContextD> sim_hit_y;
        std::shared_ptr<ContextD> sim_hit_z;
        std::shared_ptr<ContextD> sim_particle_vtx_x;
        std::shared_ptr<ContextD> sim_particle_vtx_y;
        std::shared_ptr<ContextD> sim_particle_vtx_z;
    };

    std::string filename_;
    std::shared_ptr<HistD> h_spectrometer_track_multiplicity_, h_sim_hit_multiplicity_,
        h_sim_particle_multiplicity_;
    std::shared_ptr<HistD> h_ref_x_, h_ref_y_, h_ref_z_, h_sim_hit_x_, h_sim_hit_y_, h_sim_hit_z_,
        h_sim_particle_vtx_x_, h_sim_particle_vtx_y_, h_sim_particle_vtx_z_;
    FillerD f_spectrometer_track_multiplicity_, f_ref_x_, f_ref_y_, f_ref_z_;
    // Optional: RHistConcurrentFiller has no default constructor, and these
    // are only ever constructed (via emplace, above) when isSim is set.
    std::optional<FillerD> f_sim_hit_multiplicity_, f_sim_particle_multiplicity_;
    std::optional<FillerD> f_sim_hit_x_, f_sim_hit_y_, f_sim_hit_z_,
        f_sim_particle_vtx_x_, f_sim_particle_vtx_y_, f_sim_particle_vtx_z_;
    tbb::enumerable_thread_specific<FillContexts> fill_contexts_;
    bool m_isSim = false;
};

// No-op observer for benchmarking pure framework overhead.
class RecoNoop {
   public:
    void observe(SpectrometerTracks const&, SimHits const&, SimParticles const&) {}
    void observe_tracks_only(SpectrometerTracks const&) {}
};

}  // namespace

PHLEX_REGISTER_ALGORITHMS(m, config) {
    using namespace phlex;

    auto mode = config.get<std::string>("mode", std::string{"reco"});
    auto rntuple_file = config.get<std::string>("rntuple_file", std::string{"reconstructed_objects.root"});
    auto histo_file = config.get<std::string>("histo_file", std::string{"reco_validation.root"});
    auto creator = config.get<std::string>("creator", std::string{"fit_seed"});
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
    auto selector = [&creator, &layer](char const* suffix) {
        return product_selector{.creator = phlex::experimental::identifier{creator},
                                .layer = phlex::experimental::identifier{layer},
                                .suffix = phlex::experimental::identifier{suffix}};
    };

    auto passthrough = [&layer](char const* suffix) {
        return product_selector{.creator = "rntuple_source",
                                .layer = "spill",
                                .suffix = phlex::experimental::identifier{suffix}};
    };

    if (mode == "noop") {
        auto noop = m.make<RecoNoop>();
        if (isSim) {
            noop.observe("noop", &RecoNoop::observe, concurrency::unlimited)
                .input_family(selector("track_fit_result"), passthrough("sim_hits"),
                              passthrough("sim_particles"));
        } else {
            noop.observe("noop", &RecoNoop::observe_tracks_only, concurrency::unlimited)
                .input_family(selector("track_fit_result"));
        }
        return;
    }

    auto writer = m.make<HitRNTupleWriter>(rntuple_file, isSim);

    writer.observe("write_spectrometer_tracks", &HitRNTupleWriter::write_spectrometer_tracks, concurrency::unlimited)
        .input_family(selector("track_fit_result"));

    if(isSim){
        writer.observe("write_sim_hits", &HitRNTupleWriter::write_sim_hits, concurrency::unlimited)
            .input_family(passthrough("sim_hits"));

        writer
            .observe("write_sim_particles", &HitRNTupleWriter::write_sim_particles,
                     concurrency::unlimited)
            .input_family(passthrough("sim_particles"));
    }

    auto histogrammer = m.make<RecoHistogrammer>(histo_file, isSim);
    // input_family's selector count must match the registered function's
    // arity exactly, so the sim/no-sim branches pick different overloads
    // rather than sharing one call with a variable number of selectors.
    if (isSim) {
        histogrammer.observe("validate", &RecoHistogrammer::observe, concurrency::unlimited)
            .input_family(selector("track_fit_result"), passthrough("sim_hits"),
                          passthrough("sim_particles"));
    } else {
        histogrammer.observe("validate", &RecoHistogrammer::observe_tracks_only, concurrency::unlimited)
            .input_family(selector("track_fit_result"));
    }
}
