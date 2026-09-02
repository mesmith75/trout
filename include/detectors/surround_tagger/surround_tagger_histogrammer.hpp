// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// surround_tagger_histogrammer.hpp — Surround Tagger validation histograms.
// Currently just per-event hit multiplicity; independent of the other
// subdetector histogrammers, so SBT-specific monitoring can grow here
// without touching them.

#pragma once

#include "HistoFileService.hpp"

#include <ROOT/RHist.hxx>
#include <ROOT/RHistConcurrentFiller.hxx>
#include <ROOT/RHistFillContext.hxx>
#include <SHiP/detectors/SBTHit.hpp>
#include <cstdint>
#include <memory>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <utility>
#include <vector>

class SurroundTaggerHistogrammer {
   public:
    explicit SurroundTaggerHistogrammer(std::shared_ptr<HistoFileService> file_service)
        : file_service_{std::move(file_service)},
          h_multiplicity_{std::make_shared<HistD>(static_cast<std::uint64_t>(1000),
                                                   std::make_pair(-0.5, 999.5))},
          f_multiplicity_{h_multiplicity_} {}

    void observe(std::vector<SHiP::SBTHit> const& hits) {
        ensure_context().Fill(static_cast<double>(hits.size()));
    }

    ~SurroundTaggerHistogrammer() {
        fill_contexts_.clear();
        file_service_->put("h_surround_tagger_multiplicity", "Surround tagger hits per event;N;Events",
                           *h_multiplicity_);
    }

   private:
    using HistD = ROOT::Experimental::RHist<double>;
    using FillerD = ROOT::Experimental::RHistConcurrentFiller<double>;
    using ContextD = ROOT::Experimental::RHistFillContext<double>;

    ContextD& ensure_context() {
        auto& ctx = fill_contexts_.local();
        if (!ctx) {
            ctx = f_multiplicity_.CreateFillContext();
        }
        return *ctx;
    }

    std::shared_ptr<HistoFileService> file_service_;
    std::shared_ptr<HistD> h_multiplicity_;
    FillerD f_multiplicity_;
    tbb::enumerable_thread_specific<std::shared_ptr<ContextD>> fill_contexts_;
};
