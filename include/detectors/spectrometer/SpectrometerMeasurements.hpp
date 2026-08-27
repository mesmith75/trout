// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// SpectrometerMeasurements.hpp — builds the per-surface measurement and
// source-link containers a CombinatorialKalmanFilter needs, from a flat
// vector of RecHit and a real Acts::TrackingGeometry (see
// acts_geometry_provider.cpp). Unlike ToyDetector, every hit is kept — not
// just one per station — since pattern recognition (not this class) is what
// decides which hits belong to which candidate track.

#pragma once

#include <Acts/Definitions/Algebra.hpp>
#include <Acts/Definitions/Units.hpp>
#include <Acts/EventData/SourceLink.hpp>
#include <Acts/Geometry/GeometryContext.hpp>
#include <Acts/Geometry/GeometryIdentifier.hpp>
#include <Acts/Geometry/TrackingGeometry.hpp>
#include <Acts/Surfaces/Surface.hpp>

#include "GeometryContainers.hpp"
#include "HitUncertainty.hpp"
#include "Index.hpp"
#include "IndexSourceLink.hpp"

#include <SHiP/RecHit.hpp>

#include <cmath>
#include <limits>
#include <vector>

class SpectrometerMeasurements {
 public:
  struct Measurement {
    const Acts::Surface* surface;
    Acts::Vector3 global;  // kept alongside local for seed construction
    Acts::Vector2 local;
    Acts::SquareMatrix2 covariance;
    double time;  // ns, for the time-coincidence cut in seeding
  };

  // zWindowMm: a hit is assigned to the nearest station surface only if
  // within this z distance — the same tolerance selectOneHitPerStation used,
  // now applied per-hit instead of picking a single best hit per station.
  SpectrometerMeasurements(const Acts::TrackingGeometry& geometry,
                           const Acts::GeometryContext& gctx,
                           const std::vector<SHiP::RecHit>& hits,
                           double zWindowMm = 510.0) {
    std::vector<const Acts::Surface*> stationSurfaces;
    geometry.visitSurfaces(
        [&](const Acts::Surface* s) { stationSurfaces.push_back(s); 
        std::cout<<"surface z: "<<s->center(gctx).z()<<std::endl;
        });
    
    for (auto const& hit : hits) {
      Acts::Vector3 const global(hit.position[0] * Acts::UnitConstants::mm,
                                 hit.position[1] * Acts::UnitConstants::mm,
                                 hit.position[2] * Acts::UnitConstants::mm);

      const Acts::Surface* best = nullptr;
      double bestDz = std::numeric_limits<double>::max();
      for (auto const* s : stationSurfaces) {
        double const dz = std::abs(global.z() - s->center(gctx).z());
        //std::cout<<"dz: "<<dz<<" - hit: "<<global.z()<<" - tracker: "<<s->center(gctx).z()<<std::endl;
        if (dz < zWindowMm * Acts::UnitConstants::mm && dz < bestDz) {
          bestDz = dz;
          best = s;
        }
      }
      if (!best){
        //std::cout<<"no best"<<std::endl;
         continue;  // no station within window — drop the hit
      }
      //std::cout<<"found a window"<<std::endl;
      auto locRes = best->globalToLocal(gctx, global, Acts::Vector3::UnitZ(), zWindowMm * Acts::UnitConstants::mm);
      if (!locRes.ok()){
        //std::cout<<"no local"<<std::endl;
         continue;
      }
      //std::cout<<"about to push back!"<<std::endl;
      double const time = hit.time * Acts::UnitConstants::ns;
      auto const cov = hitCovariance(HitUncertaintyContext{global, time});
      auto const idx = static_cast<ActsExamples::Index>(m_measurements.size());
      m_measurements.push_back(Measurement{best, global, locRes.value(), cov, time});
      m_sourceLinks.emplace(best->geometryId(), idx);
    }
  }

  const std::vector<Measurement>& measurements() const { return m_measurements; }

  const ActsExamples::GeometryIdMultiset<IndexSourceLink>& sourceLinks() const {
    return m_sourceLinks;
  }

  // Delegate: write calibrated measurement (same shape as
  // ToyDetector::calibrator, looked up by IndexSourceLink instead of
  // MySourceLink).
  template <typename trajectory_t>
  void calibrator(const Acts::GeometryContext&, const Acts::CalibrationContext&,
                  const Acts::SourceLink& sl,
                  typename trajectory_t::TrackStateProxy ts) const {
                    //std::cout << "calibrator called\n";
                    //std::cout<<"geometry id: "<<sl.get<IndexSourceLink>().geometryId()<<std::endl;

                    //std::cout<<"measurements size: "<<m_measurements.size()<<std::endl;
    auto const& indexSourceLink = sl.get<IndexSourceLink>();
                    //std::cout<<"link index: "<<indexSourceLink.index()<<std::endl;
    auto const& m = m_measurements[indexSourceLink.index()];
    ts.allocateCalibrated(m.local, m.covariance);
    // Without this, states this calibrator services never carry an
    // uncalibrated source link at all — findCandidateHits() (SpectrometerCkf.hpp)
    // needs it to recover which measurement index a CKF-found state
    // corresponds to.
    ts.setUncalibratedSourceLink(Acts::SourceLink{sl});
  }

 private:
  std::vector<Measurement> m_measurements;
  ActsExamples::GeometryIdMultiset<IndexSourceLink> m_sourceLinks;
};
