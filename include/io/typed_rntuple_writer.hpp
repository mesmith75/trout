// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// typed_rntuple_writer.hpp
//
// Utility functions for writing types into an RNtuple

#pragma once

#include "TFile.h"

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
#include <SHiP/detectors/StrawTubesHit.hpp>
#include <SHiP/detectors/TimeDetHit.hpp>
#include <SHiP/detectors/UBTHit.hpp>
#include <algorithm>
#include <oneapi/tbb/enumerable_thread_specific.h>

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

// A compile-time string usable as a class-template non-type parameter
// (structural type), so each Hit type can carry its RNTuple name as part
// of one declaration (see NamedHit/TypedHitWriters below) instead of a
// separately-maintained parallel list of names.
template <std::size_t N>
struct FixedString {
    constexpr FixedString(char const (&s)[N]) { std::copy_n(s, N, value); }
    char value[N];
    constexpr operator std::string_view() const { return {value, N - 1}; }
};

template <FixedString Name, typename Hit>
struct NamedHit {
    static constexpr auto name = Name;
    using type = Hit;
};

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

// Generic bundle of TypedHitWriter<Hit>, one per NamedHit<Name, Hit> in
// NamedHits. Instantiate this once per group of hit types that should share
// one RNTupleFileService/output file (see ReconstructedWriters and
// SimTruthWriters below for two independent bundles) instead of writing
// bespoke tuple/constructor boilerplate per group.
template <typename... NamedHits>
class TypedHitWriters {
   public:
    explicit TypedHitWriters(RNTupleFileService& file_service)
        : writers_{TypedHitWriter<typename NamedHits::type>{file_service, NamedHits::name}...} {}

    template <typename Hit>
    TypedHitWriter<Hit>& get() {
        return std::get<TypedHitWriter<Hit>>(writers_);
    }

   private:
    std::tuple<TypedHitWriter<typename NamedHits::type>...> writers_;
};

using ReconstructedWriters = TypedHitWriters<
    NamedHit<"spectrometer_tracks", SHiP::TrackFitResult>,
    NamedHit<"upstream_tagger", SHiP::UBTHit>, NamedHit<"surround_tagger", SHiP::SBTHit>,
    NamedHit<"calorimeter", SHiP::CaloHit>, NamedHit<"timing_detector", SHiP::TimeDetHit>>;

using SimTruthWriters = TypedHitWriters<NamedHit<"sim_hits", SHiP::SimHit>,
                                        NamedHit<"sim_particles", SHiP::SimParticle>>;

class HitRNTupleWriter {
   public:
    explicit HitRNTupleWriter(std::string const& filename, bool isSim)
        : file_service_{filename}, writers_{file_service_} {
        if (isSim) {
            sim_writers_.emplace(file_service_);
        }
    }

    // Bind a specific Hit instantiation at each phlex registration site,
    // e.g. &HitRNTupleWriter::write<SHiP::TrackFitResult> — get<Hit>() picks
    // the matching TypedHitWriter<Hit> out of the bundle by type, so this
    // covers every writer in ReconstructedWriters without a method per type.
    template <typename Hit>
    void write(std::vector<Hit> const& hits) {
        write_all(hits, writers_.template get<Hit>());
    }

    // Kept separate: sim_writers_ is only conditionally constructed (when
    // isSim), so it can't live in the always-present writers_ bundle. Only
    // ever registered when isSim is true (see PHLEX_REGISTER_ALGORITHMS in
    // rec_output_module.cpp), so sim_writers_ is guaranteed to hold a value
    // whenever this is actually called.
    template <typename Hit>
    void write_sim(std::vector<Hit> const& hits) {
        write_all(hits, sim_writers_->template get<Hit>());
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

    ReconstructedWriters writers_;

    // Left unconstructed (no RNTuples created at all) unless isSim
    std::optional<SimTruthWriters> sim_writers_;
};
