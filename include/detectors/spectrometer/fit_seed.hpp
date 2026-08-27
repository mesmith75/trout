// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "PhlexModuleProxy.hpp"
#include "phlex/core/product_selector.hpp"

// fit_seed (seed layer, transform): takes one seed (SeedWithContext, which
// already carries the SpillContext generate_seeds used), runs
// ckf.findCandidateHits() for just that seed, and refits every surviving
// CKF candidate branch (SpectrometerCkf::refit(), a real
// forward+backward-smoothed Acts::KalmanFitter — the CKF exploration itself
// has no smoother, see SpectrometerCkf.hpp) in a plain loop, returning all
// of them as one track_fit_result vector.
//
// A seed can yield zero, one, or several CKF candidates; that fan-out is
// deliberately NOT a further phlex-level unfold. A third dynamic "track"
// layer chained directly off generate_seeds' unfold output was tried and
// hit what looks like a phlex framework bug — a "bad optional access" crash
// (in phlex::detail::internal::repeater_node) when a static-layer product
// was broadcast into an unfold node; phlex's own test suite only ever
// feeds an unfold's output to a fold or transform, never to a second
// unfold. Looping over candidates in plain C++ inside fit_seed sidesteps
// that combination entirely.
void register_fit_seed(ModuleProxy& m, phlex::experimental::identifier const& seedLayer);
