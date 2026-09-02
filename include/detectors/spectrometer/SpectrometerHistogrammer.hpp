// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// SpectrometerHistogrammer.hpp — spectrometer track validation histograms:
// multiplicity plus reference-position distributions.

#pragma once

#include "HistoFileService.hpp"

#include <ROOT/RHist.hxx>
#include <ROOT/RHistConcurrentFiller.hxx>
#include <ROOT/RHistFillContext.hxx>
#include <SHiP/TrackFitResult.hpp>
#include <cstdint>
#include <memory>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <utility>
#include <vector>

class SpectrometerHistogrammer {
   public:
    explicit SpectrometerHistogrammer(std::shared_ptr<HistoFileService> file_service)
        : file_service_{std::move(file_service)},
          h_multiplicity_{make_hist(1000, -0.5, 999.5)},
          h_ref_x_{make_hist(200, -3000., 3000.)},
          h_ref_y_{make_hist(200, -6000., 6000.)},
          h_ref_z_{make_hist(200, -1000., 120000.)},
          f_multiplicity_{h_multiplicity_},
          f_ref_x_{h_ref_x_},
          f_ref_y_{h_ref_y_},
          f_ref_z_{h_ref_z_} {}

    void observe(std::vector<SHiP::TrackFitResult> const& tracks) {
        auto& ctxs = ensure_contexts();
        ctxs.multiplicity->Fill(static_cast<double>(tracks.size()));
        for (auto const& track : tracks) {
            ctxs.ref_x->Fill(track.refLoc[0]);
            ctxs.ref_y->Fill(track.refLoc[1]);
            ctxs.ref_z->Fill(track.refLoc[2]);
        }
    }

    ~SpectrometerHistogrammer() {
        fill_contexts_.clear();
        file_service_->put("h_spectrometer_track_multiplicity", "Spectrometer tracks per event;N;Events",
                           *h_multiplicity_);
        file_service_->put("h_ref_x", "Spectrometer track reference x position;x [mm];Entries",
                           *h_ref_x_);
        file_service_->put("h_ref_y", "Spectrometer track reference y position;y [mm];Entries",
                           *h_ref_y_);
        file_service_->put("h_ref_z", "Spectrometer track reference z position;z [mm];Entries",
                           *h_ref_z_);
    }

   private:
    using HistD = ROOT::Experimental::RHist<double>;
    using FillerD = ROOT::Experimental::RHistConcurrentFiller<double>;
    using ContextD = ROOT::Experimental::RHistFillContext<double>;

    static std::shared_ptr<HistD> make_hist(int nbins, double low, double high) {
        return std::make_shared<HistD>(static_cast<std::uint64_t>(nbins), std::make_pair(low, high));
    }

    struct FillContexts {
        std::shared_ptr<ContextD> multiplicity, ref_x, ref_y, ref_z;
    };

    FillContexts& ensure_contexts() {
        auto& ctxs = fill_contexts_.local();
        if (!ctxs.multiplicity) {
            ctxs.multiplicity = f_multiplicity_.CreateFillContext();
            ctxs.ref_x = f_ref_x_.CreateFillContext();
            ctxs.ref_y = f_ref_y_.CreateFillContext();
            ctxs.ref_z = f_ref_z_.CreateFillContext();
        }
        return ctxs;
    }

    std::shared_ptr<HistoFileService> file_service_;
    std::shared_ptr<HistD> h_multiplicity_, h_ref_x_, h_ref_y_, h_ref_z_;
    FillerD f_multiplicity_, f_ref_x_, f_ref_y_, f_ref_z_;
    tbb::enumerable_thread_specific<FillContexts> fill_contexts_;
};
