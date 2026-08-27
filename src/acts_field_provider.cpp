// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// acts_field_provider.cpp — Phlex provider plugin
//
// Loads the spectrometer's magnetic field map once (via covfie) and provides
// it as a shared Acts::MagneticFieldProvider — the same "build once on layer
// job" pattern acts_geometry_provider.cpp uses for the tracking geometry.
// Replaces the field previously being loaded from a hardcoded path inside
// SpectrometerCkf's own constructor.

#include "acts_field_provider.hpp"

#include "phlex/configuration.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/module.hpp"
#include "phlex/source.hpp"

#include <FieldService/CovfieFieldSource.h>
#include <memory>
#include <string>
#include <utility>

using namespace phlex;

PHLEX_REGISTER_PROVIDERS(m, config) {
    // Bare filenames resolve under $SHIPFIELD_ROOT/share/field/ (see
    // loadCovfieField's own doc comment); absolute/relative paths pass through
    // unchanged.
    auto const field_file = config.get<std::string>("field_file");

    auto eval = ship::loadCovfieField(field_file);
    auto detector = std::make_shared<DetectorField>();
    detector->field = std::make_shared<ShipActsFieldProvider>(std::move(eval));

    m.provide(
         "read_field",
         [detector](data_cell_index const&) -> std::shared_ptr<DetectorField> { return detector; },
         concurrency::unlimited)
        .output_product("spectrometer_field", phlex::experimental::identifier{"field"},
                        phlex::experimental::identifier{"job"});
}
