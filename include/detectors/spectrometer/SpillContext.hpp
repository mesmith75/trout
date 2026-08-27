// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <memory>

#include "SpectrometerCkf.hpp"
#include "SpectrometerMeasurements.hpp"

// Output of prepare_measurements, shared read-only by every seed spawned
// from the same spill.
struct SpillContext {
  std::shared_ptr<SpectrometerMeasurements> measurements;
  std::shared_ptr<SpectrometerCkf> ckf;
};
