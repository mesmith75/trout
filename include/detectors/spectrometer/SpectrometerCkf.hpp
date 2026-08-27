// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// SpectrometerCkf.hpp — Acts::CombinatorialKalmanFilter wrapper for the
// straw-tube spectrometer, replacing ToyKalmanFilter/ToyDetector's
// single-hand-picked-hit fit with real pattern recognition: seeds are built
// from every station-1/station-2 hit pair, and the CKF explores all
// compatible hits at each downstream station (no measurement-selector chi2
// cut yet — every candidate at a surface is kept, so ambiguity resolution
// between branches is a later step).

#pragma once

#include "IndexSourceLink.hpp"
#include "SpectrometerMeasurements.hpp"
#include "Utilities.hpp"

#include <Acts/EventData/ParticleHypothesis.hpp>
#include <Acts/EventData/TrackContainer.hpp>
#include <Acts/EventData/VectorMultiTrajectory.hpp>
#include <Acts/EventData/VectorTrackContainer.hpp>
#include <Acts/Geometry/GeometryContext.hpp>
#include <Acts/Geometry/GeometryIdentifier.hpp>
#include <Acts/Geometry/TrackingGeometry.hpp>
#include <Acts/MagneticField/MagneticFieldProvider.hpp>
#include <Acts/Propagator/EigenStepper.hpp>
#include <Acts/Propagator/Navigator.hpp>
#include <Acts/Propagator/Propagator.hpp>
#include <Acts/TrackFinding/CombinatorialKalmanFilter.hpp>
#include <Acts/TrackFinding/TrackStateCreator.hpp>
#include <Acts/TrackFitting/GainMatrixSmoother.hpp>
#include <Acts/TrackFitting/GainMatrixUpdater.hpp>
#include <Acts/TrackFitting/KalmanFitter.hpp>
#include <Acts/Utilities/CalibrationContext.hpp>
#include <Acts/Utilities/Logger.hpp>
#include <Acts/Utilities/VectorHelpers.hpp>
#include <SHiP/TrackFitResult.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

// Straight-line seed through two hits on different stations. p0 is a fixed
// placeholder momentum (no curvature estimate yet — field-off/low-field
// assumption between the first two stations), same as the previous
// hand-rolled seed in keep_tracks.cpp.
inline std::optional<Acts::BoundTrackParameters> makeSeedFromHitPair(
    const SpectrometerMeasurements::Measurement& m0,
    const SpectrometerMeasurements::Measurement& m1) {
    Acts::Vector3 const direction = (m1.global - m0.global).normalized();
    if (!direction.allFinite())
        return std::nullopt;

    Acts::BoundVector pars = Acts::BoundVector::Zero();
    pars[Acts::eBoundLoc0] = m0.local[0];
    pars[Acts::eBoundLoc1] = m0.local[1];
    pars[Acts::eBoundTime] = 0.;
    pars[Acts::eBoundPhi] = Acts::VectorHelpers::phi(direction);
    pars[Acts::eBoundTheta] = Acts::VectorHelpers::theta(direction);
    double const p0 = 10.0 * Acts::UnitConstants::GeV;
    pars[Acts::eBoundQOverP] = 1.0 / p0;

    // Widened by ~5.5x (sigma) from the original guess (sLoc=5mm, sAng=0.05,
    // sQop=0.5/p0): with the original values, refit chi2/ndf on real 4-hit
    // tracks (as opposed to CKF's occasional wrong/combinatorial 4-hit
    // matches, distinguishable by a far larger chi2) was running ~900,
    // implying the assumed sigma was too tight by about sqrt(900/1) ~ 30x in
    // chi2, i.e. ~5.5x in sigma. This won't make wrong matches look good too
    // (chi2 ~ 1/sigma^2, so they just move from absurd to merely bad), so a
    // chi2/ndf cut downstream can still separate real tracks from those.
    Acts::BoundMatrix cov = Acts::BoundMatrix::Zero();
    double const sLoc = 27.5 * Acts::UnitConstants::mm;
    cov(Acts::eBoundLoc0, Acts::eBoundLoc0) = sLoc * sLoc;
    cov(Acts::eBoundLoc1, Acts::eBoundLoc1) = sLoc * sLoc;
    double const sT = 1.0 * Acts::UnitConstants::ns;
    cov(Acts::eBoundTime, Acts::eBoundTime) = sT * sT;
    double const sAng = 0.275;
    cov(Acts::eBoundPhi, Acts::eBoundPhi) = sAng * sAng;
    cov(Acts::eBoundTheta, Acts::eBoundTheta) = sAng * sAng;
    double const sQop = 2.75 / p0;
    cov(Acts::eBoundQOverP, Acts::eBoundQOverP) = sQop * sQop;

    return Acts::BoundTrackParameters(m0.surface->getSharedPtr(), pars, cov,
                                      Acts::ParticleHypothesis::pion());
}

// Cheap stand-in for a real chi2 measurement selector: keeps only the
// candidate whose calibrated local position is closest (squared distance)
// to the predicted one at this surface, discarding the rest. With no
// selector at all, every hit within the (currently generous) station-match
// window is accepted as a separate branch — hundreds of candidates on a
// busy station — which is the likely cause of the CKF crash; this collapses
// that back down to one candidate per surface to test that diagnosis before
// committing to a real Acts::MeasurementSelector.
template <typename TrackStateProxy>
Acts::Result<std::pair<typename std::vector<TrackStateProxy>::iterator,
                       typename std::vector<TrackStateProxy>::iterator>>
selectNearestMeasurement(std::vector<TrackStateProxy>& candidates, bool& isOutlier,
                         const Acts::Logger&) {
    isOutlier = false;
    if (candidates.empty())
        return std::pair{candidates.begin(), candidates.end()};

    auto best = candidates.begin();
    double bestDist2 = std::numeric_limits<double>::max();
    for (auto it = candidates.begin(); it != candidates.end(); ++it) {
        auto const pred = it->predicted();
        auto const meas = it->template calibrated<2>();
        double const dx = pred[Acts::eBoundLoc0] - meas[0];
        double const dy = pred[Acts::eBoundLoc1] - meas[1];
        double const dist2 = dx * dx + dy * dy;
        if (dist2 < bestDist2) {
            bestDist2 = dist2;
            best = it;
        }
    }
    std::iter_swap(candidates.begin(), best);
    return std::pair{candidates.begin(), std::next(candidates.begin())};
}

// Same field extraction ToyKalmanFitter/Utilities.hpp::fromACTSFitResult
// does for a single-track KalmanFitter result, generalised to any
// TrackProxy — CombinatorialKalmanFilter::findTracks returns a vector of
// them rather than one Result<TrackProxy>.
template <typename TrackProxy, typename TrackStateContainer>
SHiP::TrackFitResult trackProxyToFitResult(TrackProxy const& track, TrackStateContainer& states,
                                           Acts::GeometryContext const& gctx) {
    SHiP::TrackFitResult result;
    result.fitStatus = 0;
    result.chi2 = track.chi2();
    result.ndf = track.nDoF();

    // Acts::CombinatorialKalmanFilterExtensions has no smoother (only
    // updater/branchStopper/createTrackStates), so states never have a
    // "smoothed" component, and track.parameters() is only ever set when a
    // target surface triggers CKF's isTargetReached branch — which
    // findTracks() below never configures. Use each state's filtered
    // (forward-pass) parameters instead, and take the tip's (last-added, so
    // first seen by visitBackwards) filtered state as the track's summary
    // parameters.
    bool haveRefParams = false;
    states.visitBackwards(track.tipIndex(), [&](auto ts) {
        if (!ts.typeFlags().isMeasurement())
            return true;
        if (!ts.hasCalibrated())
            return true;
        if (!ts.hasFiltered())
            return true;

        auto meas = ts.template calibrated<2>();
        auto filtered = ts.filtered();

        if (!haveRefParams) {
            result.qoverp = filtered[Acts::eBoundQOverP];
            result.phi = filtered[Acts::eBoundPhi];
            result.theta = filtered[Acts::eBoundTheta];
            result.time = filtered[Acts::eBoundTime];
            Acts::Vector3 const direction(std::sin(result.theta) * std::cos(result.phi),
                                          std::sin(result.theta) * std::sin(result.phi),
                                          std::cos(result.theta));
            auto const global = ts.referenceSurface().localToGlobal(
                gctx, Acts::Vector2{filtered[Acts::eBoundLoc0], filtered[Acts::eBoundLoc1]},
                direction);
            result.refLoc = {global.x(), global.y(), global.z()};
            haveRefParams = true;
        }

        Acts::Vector2 fitted;
        fitted << filtered[Acts::eBoundLoc0], filtered[Acts::eBoundLoc1];
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

class SpectrometerCkf {
   public:
    using Trajectory = Acts::VectorMultiTrajectory;
    using TrackContainerBackend = Acts::VectorTrackContainer;
    using BField = Acts::MagneticFieldProvider;
    using Stepper = Acts::EigenStepper<>;
    using Navigator = Acts::Navigator;
    using Propagator = Acts::Propagator<Stepper, Navigator>;

    SpectrometerCkf(std::shared_ptr<const Acts::TrackingGeometry> geometry,
                    std::shared_ptr<Acts::MagneticFieldProvider> field)
        : m_geometry{std::move(geometry)},
          m_geoContext(Acts::GeometryContext::dangerouslyDefaultConstruct()),
          m_field{std::move(field)} {
        // TEMPORARY: sanity check the field magnitude/units — a wildly large
        // or mis-scaled value here would explain the propagator curling too
        // tightly to cover the ~2m station spacing within the step budget.
        {
            auto cache = m_field->makeCache(m_magContext);
            Acts::Vector3 const samplePos(0., 0., 90000. * Acts::UnitConstants::mm);
            auto fieldRes = m_field->getField(samplePos, cache);
            if (fieldRes.ok())
                std::cout << "[INFO]: field at " << samplePos.transpose() << " = "
                          << fieldRes.value().transpose() << " (internal units), "
                          << (fieldRes.value() / Acts::UnitConstants::T).transpose() << " T\n";
            else
                std::cout << "field lookup failed: " << fieldRes.error() << "\n";
        }

        Stepper stepper(m_field);
        Navigator::Config navCfg;
        navCfg.trackingGeometry = m_geometry;
        Navigator navigator(navCfg, Acts::getDefaultLogger("Navigator", Acts::Logging::INFO));

        Propagator propagator(std::move(stepper), std::move(navigator));
        m_ckf = std::make_unique<Ckf>(std::move(propagator),
                                      Acts::getDefaultLogger("CKF", Acts::Logging::INFO));

        // Separate stepper/navigator/propagator for the refit() KalmanFitter
        // below — the CKF's own propagator was just moved into m_ckf.
        Stepper fitStepper(m_field);
        Navigator::Config fitNavCfg;
        fitNavCfg.trackingGeometry = m_geometry;
        Navigator fitNavigator(fitNavCfg,
                               Acts::getDefaultLogger("FitNavigator", Acts::Logging::INFO));
        Propagator fitPropagator(std::move(fitStepper), std::move(fitNavigator));
        m_fitter = std::make_unique<Fitter>(std::move(fitPropagator));
    }

    // Runs the CKF from one seed and appends any tracks found to `out`.
    void findTracks(Acts::BoundTrackParameters const& seed,
                    SpectrometerMeasurements const& measurements,
                    std::vector<SHiP::TrackFitResult>& out) const {
        // std::cout<<"in a track finding function"<<std::endl;
        IndexSourceLinkAccessor sourceLinkAccessor;
        sourceLinkAccessor.container = &measurements.sourceLinks();

        using TrackStateCreatorType =
            Acts::TrackStateCreator<IndexSourceLinkAccessor::Iterator, TrackContainer>;
        TrackStateCreatorType trackStateCreator;
        trackStateCreator.sourceLinkAccessor.template connect<&IndexSourceLinkAccessor::range>(
            &sourceLinkAccessor);
        trackStateCreator.calibrator
            .template connect<&SpectrometerMeasurements::calibrator<Trajectory>>(&measurements);
        // Cheap nearest-candidate cut (see selectNearestMeasurement above) in
        // place of the default accept-everything selector, to test whether
        // unbounded branching on busy stations is what's crashing the CKF.
        trackStateCreator.measurementSelector.template connect<
            &selectNearestMeasurement<typename TrackContainer::TrackStateProxy>>();

        Acts::GainMatrixUpdater kfUpdater;

        Acts::CombinatorialKalmanFilterExtensions<TrackContainer> extensions;
        extensions.updater.template connect<&Acts::GainMatrixUpdater::operator()<Trajectory>>(
            &kfUpdater);
        extensions.createTrackStates.template connect<&TrackStateCreatorType::createTrackStates>(
            &trackStateCreator);

        Acts::PropagatorPlainOptions propOptions(m_geoContext, m_magContext);
        // Navigation is now confirmed correct (Gen1/Gen3 portal fix in
        // acts_geometry_provider.cpp), so this no longer needs to guard against
        // a broken process — just bound legitimate integration through the real
        // field over the ~11 m station span, which can need more than a
        // handful of adaptive steps.
        propOptions.maxSteps = 500;
        propOptions.pathLimit = 50000. * Acts::UnitConstants::mm;  // stations span ~11 m

        Acts::CombinatorialKalmanFilterOptions<TrackContainer> options(
            m_geoContext, m_magContext, std::cref(m_calibContext), extensions, propOptions);

        TrackContainerBackend trackStorage;
        Trajectory trajStorage;
        TrackContainer tracks(trackStorage, trajStorage);

        auto result = m_ckf->findTracks(seed, options, tracks);
        if (!result.ok()) {
            std::cerr << "CKF track finding failed: " << result.error() << "\n";
            return;
        }
        // std::cout << "candidates: " << result->size()
        //<< " track states total: " << tracks.trackStateContainer().size() << "\n";
        for (auto const& trackProxy : *result) {
            // Branches that never got extended with a measurement (no
            // branchStopper is wired up, so the CKF can still hand these back)
            // have no track states at all — skip them rather than crash walking
            // an invalid tip.
            if (trackProxy.tipIndex() == Acts::kTrackIndexInvalid) {
                std::cout << "tipIndex issue" << std::endl;
                continue;
            }
            out.push_back(
                trackProxyToFitResult(trackProxy, tracks.trackStateContainer(), m_geoContext));
        }
    }

    // Runs the CKF from one seed, same as findTracks() above, but returns just
    // the ordered measurement indices each surviving candidate branch picked
    // up rather than a converted SHiP::TrackFitResult. A TrackProxy can't
    // outlive the TrackContainer built inside this call, so hit indices are
    // the only thing that can cross out to a caller — pass them to refit()
    // below to get a real fit for one candidate.
    std::vector<std::vector<ActsExamples::Index>> findCandidateHits(
        Acts::BoundTrackParameters const& seed,
        SpectrometerMeasurements const& measurements) const {
        std::vector<std::vector<ActsExamples::Index>> out;

        IndexSourceLinkAccessor sourceLinkAccessor;
        sourceLinkAccessor.container = &measurements.sourceLinks();

        using TrackStateCreatorType =
            Acts::TrackStateCreator<IndexSourceLinkAccessor::Iterator, TrackContainer>;
        TrackStateCreatorType trackStateCreator;
        trackStateCreator.sourceLinkAccessor.template connect<&IndexSourceLinkAccessor::range>(
            &sourceLinkAccessor);
        trackStateCreator.calibrator
            .template connect<&SpectrometerMeasurements::calibrator<Trajectory>>(&measurements);
        trackStateCreator.measurementSelector.template connect<
            &selectNearestMeasurement<typename TrackContainer::TrackStateProxy>>();

        Acts::GainMatrixUpdater kfUpdater;

        Acts::CombinatorialKalmanFilterExtensions<TrackContainer> extensions;
        extensions.updater.template connect<&Acts::GainMatrixUpdater::operator()<Trajectory>>(
            &kfUpdater);
        extensions.createTrackStates.template connect<&TrackStateCreatorType::createTrackStates>(
            &trackStateCreator);

        Acts::PropagatorPlainOptions propOptions(m_geoContext, m_magContext);
        propOptions.maxSteps = 500;
        propOptions.pathLimit = 50000. * Acts::UnitConstants::mm;

        Acts::CombinatorialKalmanFilterOptions<TrackContainer> options(
            m_geoContext, m_magContext, std::cref(m_calibContext), extensions, propOptions);

        TrackContainerBackend trackStorage;
        Trajectory trajStorage;
        TrackContainer tracks(trackStorage, trajStorage);

        auto result = m_ckf->findTracks(seed, options, tracks);
        if (!result.ok()) {
            std::cerr << "CKF track finding failed: " << result.error() << "\n";
            return out;
        }
        for (auto const& trackProxy : *result) {
            if (trackProxy.tipIndex() == Acts::kTrackIndexInvalid)
                continue;
            std::vector<ActsExamples::Index> hits;
            tracks.trackStateContainer().visitBackwards(trackProxy.tipIndex(), [&](auto ts) {
                if (!ts.typeFlags().isMeasurement())
                    return true;
                // isMeasurement() alone doesn't guarantee this — ACTS itself only
                // asserts it inside getUncalibratedSourceLink() (stripped by our
                // -DNDEBUG release build), so an unguarded call here threw
                // bad_optional_access until SpectrometerMeasurements::calibrator
                // was fixed to actually set it.
                if (!ts.hasUncalibratedSourceLink())
                    return true;
                auto const& isl = ts.getUncalibratedSourceLink().template get<IndexSourceLink>();
                hits.push_back(isl.index());
                return true;
            });
            if (!hits.empty())
                out.push_back(std::move(hits));
        }
        return out;
    }

    // Refits one candidate's hit list with a real forward+backward-smoothed
    // Acts::KalmanFitter, starting from the same straight-line `seed` used to
    // find it. Unlike the CKF above (no smoother available — see
    // trackProxyToFitResult's comment), this gets real smoothed residuals, via
    // the same SHiP::fromACTSFitResult converter the pre-CKF single-hit fit
    // used (see ToyKalmanFitter::fit) — appropriate here since a smoother and
    // reference surface are both actually configured below.
    SHiP::TrackFitResult refit(Acts::BoundTrackParameters const& seed,
                               SpectrometerMeasurements const& measurements,
                               std::vector<ActsExamples::Index> const& hitIndices) const {
        // KalmanFitter's regular-Navigator fit() matches a visited surface to a
        // measurement by raw Surface* pointer equality (see its Actor's
        // `inputMeasurements.find(&surface)`), built upfront from whatever this
        // accessor returns per SourceLink. IndexSourceLinkSurfaceAccessor would
        // re-derive that pointer via TrackingGeometry::findSurface(geometryId) —
        // a different lookup path than what the Navigator itself hands back
        // while crossing the surface, which this geometry backend has already
        // shown (the earlier Gen1/Gen3 portal issue) isn't always pointer-stable
        // by id alone. Sidestep that entirely: SpectrometerMeasurements already
        // captured the exact Surface* each hit was built against, so use that
        // directly instead of re-deriving anything.
        struct MeasurementSurfaceAccessor {
            SpectrometerMeasurements const* measurements;
            const Acts::Surface* operator()(Acts::SourceLink const& sl) const {
                auto const& isl = sl.get<IndexSourceLink>();
                return measurements->measurements()[isl.index()].surface;
            }
        };

        Acts::GainMatrixUpdater kfUpdater;
        Acts::GainMatrixSmoother kfSmoother;
        MeasurementSurfaceAccessor surfaceAccessor{&measurements};

        Acts::KalmanFitterExtensions<Trajectory> ext;
        ext.surfaceAccessor.template connect<&MeasurementSurfaceAccessor::operator()>(
            &surfaceAccessor);
        ext.calibrator.template connect<&SpectrometerMeasurements::calibrator<Trajectory>>(
            &measurements);
        ext.updater.template connect<&Acts::GainMatrixUpdater::operator()<Trajectory>>(&kfUpdater);
        ext.smoother.template connect<&Acts::GainMatrixSmoother::operator()<Trajectory>>(
            &kfSmoother);

        Acts::PropagatorPlainOptions propOptions(m_geoContext, m_magContext);
        propOptions.maxSteps = 500;
        propOptions.pathLimit = 50000. * Acts::UnitConstants::mm;

        Acts::KalmanFitterOptions<Trajectory> options(m_geoContext, m_magContext,
                                                      std::cref(m_calibContext), ext, propOptions,
                                                      &seed.referenceSurface());

        Acts::VectorTrackContainer trackStorage;
        Trajectory trajStorage;
        Acts::TrackContainer tracks(std::move(trackStorage), std::move(trajStorage));

        std::vector<Acts::SourceLink> sourceLinks;
        sourceLinks.reserve(hitIndices.size());
        for (auto const idx : hitIndices) {
            auto const& m = measurements.measurements()[idx];
            sourceLinks.emplace_back(IndexSourceLink{m.surface->geometryId(), idx});
        }

        auto result = m_fitter->fit(sourceLinks.begin(), sourceLinks.end(), seed, options, tracks);
        return SHiP::fromACTSFitResult(result, tracks, m_geoContext);
    }

   private:
    using TrackContainer = Acts::TrackContainer<TrackContainerBackend, Trajectory>;
    using Ckf = Acts::CombinatorialKalmanFilter<Propagator, TrackContainer>;
    using Fitter = Acts::KalmanFitter<Propagator, Trajectory>;

    std::shared_ptr<const Acts::TrackingGeometry> m_geometry;
    Acts::GeometryContext m_geoContext;
    Acts::MagneticFieldContext m_magContext;
    Acts::CalibrationContext m_calibContext;
    std::shared_ptr<BField> m_field;
    std::unique_ptr<Ckf> m_ckf;
    std::unique_ptr<Fitter> m_fitter;
};
