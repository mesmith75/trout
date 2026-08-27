// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "PhlexModuleProxy.hpp"
#include "phlex/core/product_selector.hpp"

// prepare_measurements (spill layer, transform): straw_tubes_hits +
// tracking_geometry -> SpillContext (SpectrometerMeasurements +
// SpectrometerCkf), built ONCE per spill and shared by every seed spawned
// downstream (rather than rebuilt per seed, which would also reload the
// field file every time).
void register_prepare_measurements(ModuleProxy& m,
                                   phlex::experimental::identifier const& layer);
