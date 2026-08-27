// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <Acts/Geometry/TrackingGeometry.hpp>
#include <GeometryService/SHiPGeometryService.h>
#include <memory>

struct DetectorGeometry {
    std::shared_ptr<ship::SHiPGeometryService> geometryService;
    std::shared_ptr<const Acts::TrackingGeometry> trackingGeometry;

    // Also retain converted detector elements if your ACTS version requires it.
};
