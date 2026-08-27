// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "detectors/spectrometer/prepare_measurements.hpp"

#include "acts_field_provider.hpp"
#include "acts_geometry_provider.hpp"
#include "detectors/spectrometer/SpillContext.hpp"

#include <Acts/Geometry/GeometryContext.hpp>
#include <SHiP/RecHit.hpp>
#include <SHiP/detectors/StrawTubesHit.hpp>
#include <memory>
#include <vector>

using namespace phlex;

void register_prepare_measurements(ModuleProxy& m, phlex::experimental::identifier const& layer) {
    m.transform(
         "prepare_measurements",
         [](std::vector<SHiP::StrawTubesHit> const& ip,
            std::shared_ptr<DetectorGeometry> const& detector,
            std::shared_ptr<DetectorField> const& field) -> std::shared_ptr<SpillContext> {
             auto ctx = std::make_shared<SpillContext>();
             if (!detector || !detector->trackingGeometry || !field || !field->field)
                 return ctx;

             std::vector<SHiP::RecHit> hits;
             hits.reserve(ip.size());
             for (auto const& h : ip)
                 hits.push_back(h.recHit);

             auto gctx = Acts::GeometryContext::dangerouslyDefaultConstruct();
             ctx->measurements = std::make_shared<SpectrometerMeasurements>(
                 *detector->trackingGeometry, gctx, hits);
             ctx->ckf = std::make_shared<SpectrometerCkf>(detector->trackingGeometry, field->field);
             return ctx;
         },
         concurrency::unlimited)
        .input_family(
            product_selector{
                .creator = "rntuple_source", .layer = layer, .suffix = "straw_tubes_hits"},
            product_selector{.creator = "tracking_geometry", .layer = "job", .suffix = "detector"},
            product_selector{.creator = "spectrometer_field", .layer = "job", .suffix = "field"})
        .output_product_suffixes("spill_context");
}
