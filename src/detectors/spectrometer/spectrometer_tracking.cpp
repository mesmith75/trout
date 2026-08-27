// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// spectrometer_tracking.cpp — Phlex module plugin
//
// Splits pattern recognition into phlex-native stages so per-seed work is
// parallelized by the framework's own scheduling, instead of a single
// serial loop inside one transform (contrast keep_tracks.cpp, which does
// everything for a whole spill in one call). Each stage's implementation
// lives in its own file — this one just wires them together, in order:
//
//   1. prepare_measurements.{hpp,cpp} — spill layer, transform.
//        straw_tubes_hits + tracking_geometry -> SpillContext, built once
//        per spill and shared read-only by every seed below.
//   2. generate_seeds.{hpp,cpp} — spill layer, unfold -> "seed" layer.
//        Seeding: pairs station-1/station-2 hits into candidate seeds.
//   3. fit_seed.{hpp,cpp} — seed layer, transform.
//        Finding (CKF pattern recognition) + fitting (KalmanFitter) for
//        one seed at a time.
//
// See each header for what that stage actually does, and SpectrometerCkf.hpp
// for the underlying ACTS track-finding/fitting engine all of them share.

#include "detectors/spectrometer/fit_seed.hpp"
#include "detectors/spectrometer/generate_seeds.hpp"
#include "detectors/spectrometer/prepare_measurements.hpp"
#include "phlex/module.hpp"

#include <string>

PHLEX_REGISTER_ALGORITHMS(m, config) {
    auto const layer = phlex::experimental::identifier{config.get<std::string>("layer")};
    auto const seedLayerName = std::string{"seed"};
    auto const seedLayer = phlex::experimental::identifier{seedLayerName};

    register_prepare_measurements(m, layer);
    register_generate_seeds(m, layer, seedLayerName);
    register_fit_seed(m, seedLayer);
}
