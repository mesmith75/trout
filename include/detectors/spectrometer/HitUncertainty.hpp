// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// HitUncertainty.hpp — single point of truth for per-hit measurement
// covariance, so it can go from "one fixed placeholder" to a real
// calibration (parameterised by hit location / track parameters /
// occupancy / ...) without touching any call site.

#pragma once

#include <Acts/Definitions/Algebra.hpp>

// Everything a future calibrated model might need to parameterise a hit's
// uncertainty. Only globalPosition and time are actually populated today —
// SpectrometerMeasurements builds these before pattern recognition or
// fitting ever runs, so per-track quantities (incidence angle, momentum)
// aren't available yet at the call site, and occupancy (hit density at
// this station/time) isn't computed anywhere yet either. Both are noted
// here so the day a real model needs them, it's this struct and this one
// call site that grow — not every caller of hitCovariance().
struct HitUncertaintyContext {
  Acts::Vector3 globalPosition;
  double time = 0.0;

  // Not yet populated. Would need e.g. a two-pass approach (fit once with
  // today's placeholder, feed the resulting track parameters back in) or
  // a fuller occupancy count computed across each station up front.
  // std::optional<Acts::BoundTrackParameters> trackParameters;
  // std::optional<std::size_t> stationOccupancy;
};

// Per-hit measurement covariance in the surface's local 2D frame
// (loc0/loc1). Currently a fixed placeholder regardless of `context` — see
// HitUncertainty.cpp for how that number was arrived at (not a calibrated
// detector value). Replace the implementation once real hit-resolution
// calibration exists; callers (SpectrometerMeasurements today) shouldn't
// need to change.
Acts::SquareMatrix2 hitCovariance(HitUncertaintyContext const& context);
