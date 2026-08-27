#pragma once

#include "FieldService/IFieldSource.h"

#include <Acts/Definitions/Algebra.hpp>
#include <Acts/Definitions/Units.hpp>
#include <Acts/MagneticField/MagneticFieldProvider.hpp>
#include <Acts/Utilities/Result.hpp>
#include <SHiP/Units.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

// To-do - make this properly defined or automatically loaded from the field map. For now, we
// hardcode the bounds of the field map.
struct FieldBoundsM {
    double xmin = -4000 * Acts::UnitConstants::mm, xmax = 4000 * Acts::UnitConstants::mm,
           ymin = -4000 * Acts::UnitConstants::mm, ymax = 4000 * Acts::UnitConstants::mm,
           zmin = 0 * Acts::UnitConstants::mm, zmax = 100000 * Acts::UnitConstants::mm;
};

class ShipActsFieldProvider final : public Acts::MagneticFieldProvider {
   public:
    explicit ShipActsFieldProvider(std::shared_ptr<ship::IFieldEvaluator> eval)
        : m_eval(std::move(eval)) {}

    struct Cache {};

    [[nodiscard]] Acts::MagneticFieldProvider::Cache makeCache(
        const Acts::MagneticFieldContext& /*mctx*/) const override {
        return Acts::MagneticFieldProvider::Cache{Cache{}};
    }

    [[nodiscard]] Acts::Result<Acts::Vector3> getField(
        const Acts::Vector3& posActs,
        Acts::MagneticFieldProvider::Cache& /*cache*/) const override {
        return doEvaluate(posActs);
    }

    // Optional convenience overload (not override in your ACTS version)
    [[nodiscard]] Acts::Result<Acts::Vector3> getField(const Acts::Vector3& posActs) const {
        return doEvaluate(posActs);
    }

   private:
    [[nodiscard]] Acts::Result<Acts::Vector3> doEvaluate(const Acts::Vector3& posActs) const {
        if (!m_eval) {
            return Acts::Result<Acts::Vector3>::success(Acts::Vector3::Zero());
        }

        // finite input guard
        if (!std::isfinite(posActs[0]) || !std::isfinite(posActs[1]) ||
            !std::isfinite(posActs[2])) {
            return Acts::Result<Acts::Vector3>::success(Acts::Vector3::Zero());
        }

        // ACTS -> mm
        const double x_mm = posActs[0] / Acts::UnitConstants::mm;
        const double y_mm = posActs[1] / Acts::UnitConstants::mm;
        const double z_mm = posActs[2] / Acts::UnitConstants::mm;

        // Check if we are within the bounds of the field map
        if (x_mm < bounds.xmin || x_mm > bounds.xmax || y_mm < bounds.ymin || y_mm > bounds.ymax ||
            z_mm < bounds.zmin || z_mm > bounds.zmax) {
            return Acts::Result<Acts::Vector3>::success(Acts::Vector3::Zero());
        }

        const ship::Length x = x_mm * ship::units::mm;
        const ship::Length y = y_mm * ship::units::mm;
        const ship::Length z = z_mm * ship::units::mm;

        std::array<ship::IFieldEvaluator::field_q, 3> b{};
        try {
            b = m_eval->at(x, y, z);
        } catch (...) {
            return Acts::Result<Acts::Vector3>::success(Acts::Vector3::Zero());
        }

        const double bx_T = b[0].numerical_value_in(ship::units::tesla);
        const double by_T = b[1].numerical_value_in(ship::units::tesla);
        const double bz_T = b[2].numerical_value_in(ship::units::tesla);

        if (!std::isfinite(bx_T) || !std::isfinite(by_T) || !std::isfinite(bz_T)) {
            return Acts::Result<Acts::Vector3>::success(Acts::Vector3::Zero());
        }

        return Acts::Result<Acts::Vector3>::success(Acts::Vector3{bx_T * Acts::UnitConstants::T,
                                                                  by_T * Acts::UnitConstants::T,
                                                                  bz_T * Acts::UnitConstants::T});
    }

    FieldBoundsM bounds;
    std::shared_ptr<ship::IFieldEvaluator> m_eval;
};

// Output of the acts_field_provider phlex module, mirroring DetectorGeometry
// (acts_geometry_provider.hpp): the field is loaded once and shared as a
// single product, rather than every consumer loading its own copy.
struct DetectorField {
    std::shared_ptr<Acts::MagneticFieldProvider> field;
};
