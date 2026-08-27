// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "detectors/spectrometer/HitUncertainty.hpp"

#include <Acts/Definitions/Units.hpp>

Acts::SquareMatrix2 hitCovariance(HitUncertaintyContext const& /*context*/) {
    // Placeholder, uncalibrated: sigma=100mm was arrived at empirically —
    // widened from an initial sigma=sqrt(10)~3.16mm guess after refit
    // chi2/ndf on real 4-hit tracks stayed ~930 even once the seed's own
    // prior covariance (SpectrometerCkf.hpp's makeSeedFromHitPair) was
    // widened by ~5.5x, which showed this per-hit covariance — not the
    // seed's — was what dominated chi2 once real Kalman updates had pulled
    // the fit onto the real hits. Not a real detector resolution; ignores
    // `context` entirely for now.
    Acts::SquareMatrix2 cov = Acts::SquareMatrix2::Zero();
    double const sigma = 100.0 * Acts::UnitConstants::mm;
    cov(0, 0) = sigma * sigma;
    cov(1, 1) = sigma * sigma;
    return cov;
}
