#pragma once

#include <Acts/Definitions/Algebra.hpp>
#include <Acts/EventData/VectorMultiTrajectory.hpp>
#include <Acts/EventData/VectorTrackContainer.hpp>
#include <Acts/Geometry/GeometryContext.hpp>
#include <SHiP/TrackFitResult.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SHiP {

inline TrackFitResult fromACTSFitResult(
    const Acts::Result<Acts::TrackContainer<Acts::VectorTrackContainer, Acts::VectorMultiTrajectory,
                                            Acts::ValueHolder>::TrackProxy>& actsResult,
    Acts::TrackContainer<Acts::VectorTrackContainer, Acts::VectorMultiTrajectory,
                         Acts::ValueHolder>& tracks,
    Acts::GeometryContext const& gctx) {
    TrackFitResult result;
    if (!actsResult.ok()) {
        result.fitStatus = 1;
        return result;
    }

    auto track = actsResult.value();
    const auto& params = track.parameters();
    auto& states = tracks.trackStateContainer();

    result.fitStatus = 0;
    result.chi2 = track.chi2();
    result.ndf = track.nDoF();
    result.qoverp = params[Acts::eBoundQOverP];
    result.phi = params[Acts::eBoundPhi];
    result.theta = params[Acts::eBoundTheta];
    result.time = params[Acts::eBoundTime];

    // refLoc used to be {loc0, loc1, 0.0} — the LOCAL position on the
    // reference surface with a dead placeholder third component, not a
    // global position at all (so e.g. it never read anywhere near the
    // first station's real z, no matter how good the fit was). Convert to
    // the surface's actual global position instead.
    if (track.hasReferenceSurface()) {
        Acts::Vector3 const direction(std::sin(result.theta) * std::cos(result.phi),
                                      std::sin(result.theta) * std::sin(result.phi),
                                      std::cos(result.theta));
        auto const global = track.referenceSurface().localToGlobal(
            gctx, Acts::Vector2{params[Acts::eBoundLoc0], params[Acts::eBoundLoc1]}, direction);
        result.refLoc = {global.x(), global.y(), global.z()};
    } else {
        // No reference surface set — fall back to the old (local-position)
        // behaviour rather than reporting a meaningless global position.
        result.refLoc = {params[Acts::eBoundLoc0], params[Acts::eBoundLoc1], 0.0};
    }

    states.visitBackwards(track.tipIndex(), [&](auto ts) {
        if (!ts.typeFlags().isMeasurement()) {
            return true;
        }
        if (!(ts.hasCalibrated())) {
            ;
            return true;
        }
        if (!ts.hasSmoothed()) {
            return true;
        }

        auto meas = ts.template calibrated<2>();
        auto smooth = ts.smoothed();

        Acts::Vector2 fitted;
        fitted << smooth[Acts::eBoundLoc0], smooth[Acts::eBoundLoc1];

        Acts::Vector2 residual = meas - fitted;
        result.inputMeasurementsX.push_back(meas[0]);
        result.inputMeasurementsY.push_back(meas[1]);
        result.fittedMeasurementsX.push_back(fitted[0]);
        result.fittedMeasurementsY.push_back(fitted[1]);
        result.residualsX.push_back(residual[0]);
        result.residualsY.push_back(residual[1]);
        return true;
    });
    result.nMeas = static_cast<std::int32_t>(result.inputMeasurementsX.size());

    return result;
}
}  // namespace SHiP
