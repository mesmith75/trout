// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// HistoFileService.hpp — shared ROOT output file for validation histograms.
// Opened once; every subdetector histogrammer keeps it alive via
// shared_ptr and Put()s its own histograms into it at its own destruction,
// so it doesn't matter which histogrammer is torn down last.

#pragma once

#include <ROOT/Hist/ConvertToTH1.hxx>
#include <ROOT/RFile.hxx>
#include <ROOT/RHist.hxx>
#include <TH1D.h>
#include <exception>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>

class HistoFileService {
   public:
    explicit HistoFileService(std::string filename)
        : filename_{std::move(filename)}, file_{ROOT::Experimental::RFile::Recreate(filename_)} {}

    void put(char const* name, char const* title, ROOT::Experimental::RHist<double> const& h) {
        try {
            std::scoped_lock lock{mutex_};
            auto th1 = ROOT::Experimental::Hist::ConvertToTH1D(h);
            th1->SetNameTitle(name, title);
            file_->Put(name, *th1);
        } catch (std::exception const& e) {
            // RException, filesystem errors etc. — must not escape a destructor.
            try {
                spdlog::error("HistoFileService: failed to write histogram '{}' to '{}': {}", name,
                              filename_, e.what());
            } catch (...) {
            }
        }
    }

   private:
    std::string filename_;
    std::unique_ptr<ROOT::Experimental::RFile> file_;
    std::mutex mutex_;
};
