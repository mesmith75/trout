// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// acts_geometry_provider.cpp — Phlex provider plugin
//
// Builds a minimal ACTS tracking geometry for the straw-tube spectrometer
// from the real SHiP geometry (via SHiPGeometryService), replacing the
// hardcoded plane z-positions in ToyDetector/keep_tracks.cpp. One
// Acts::PlaneSurface per tracker station (/SHiP/trackers/station_1..4),
// positioned at each station's real global placement.
//
// G4LogicalVolumeStore (used elsewhere, e.g. aegir's detector_construction)
// only carries shape/material, not placement — so unlike that pattern, this
// walks the physical-volume tree from the world down, accumulating
// transforms with G4NavigationHistory (Geant4's own, tested composition
// logic) to recover each station's global position.

#include "GeometryService/GeometryThread.h"
#include "acts_geometry_provider.hpp"

#include "phlex/configuration.hpp"
#include "phlex/model/data_cell_index.hpp"
#include "phlex/module.hpp"
#include "phlex/source.hpp"

#include <Acts/Definitions/Units.hpp>
#include <Acts/Geometry/Blueprint.hpp>
#include <Acts/Geometry/BlueprintOptions.hpp>
#include <Acts/Geometry/CuboidVolumeBounds.hpp>
#include <Acts/Geometry/GeometryContext.hpp>
#include <Acts/Geometry/GeometryIdentifier.hpp>
#include <Acts/Geometry/NavigationPolicyFactory.hpp>
#include <Acts/Geometry/StaticBlueprintNode.hpp>
#include <Acts/Geometry/TrackingGeometry.hpp>
#include <Acts/Geometry/TrackingVolume.hpp>
#include <Acts/Navigation/TryAllNavigationPolicy.hpp>
#include <Acts/Surfaces/PlaneSurface.hpp>
#include <Acts/Surfaces/RectangleBounds.hpp>
#include <Acts/Utilities/Logger.hpp>

#include <G4GeometryManager.hh>
#include <G4LogicalVolume.hh>
#include <G4LogicalVolumeStore.hh>
#include <G4NavigationHistory.hh>
#include <G4PVPlacement.hh>
#include <G4PhysicalVolumeStore.hh>
#include <G4RegionStore.hh>
#include <G4SolidStore.hh>
#include <G4VPhysicalVolume.hh>
#include <G4VSolid.hh>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>  // NOLINT(build/c++17)
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

using namespace phlex;
//noop
namespace {

constexpr int kNumStations = 4;

struct StationPlacement {
  G4ThreeVector origin;       // global position, G4/mm units
  double halfX{0}, halfY{0};  // transverse bounding-box half-extents, mm
};

// Depth-first search from `pv` for a volume named `target_name`, composing
// the placement transform with G4NavigationHistory (Geant4's own transform
// composition, rather than hand-rolled affine-transform math) so any
// rotated ancestors above the tracker are handled correctly.
std::optional<StationPlacement> find_station(G4VPhysicalVolume* pv,
                                              std::string const& target_name,
                                              G4NavigationHistory& history) {
  if (pv->GetName() == target_name) {
    auto const origin =
        history.GetTopTransform().Inverse().TransformPoint(G4ThreeVector());
    G4ThreeVector pMin, pMax;
    pv->GetLogicalVolume()->GetSolid()->BoundingLimits(pMin, pMax);
    return StationPlacement{origin, 0.5 * (pMax.x() - pMin.x()),
                            0.5 * (pMax.y() - pMin.y())};
  }
  auto* lv = pv->GetLogicalVolume();
  for (int i = 0; i < lv->GetNoDaughters(); ++i) {
    auto* daughter = lv->GetDaughter(i);
    history.NewLevel(daughter, kNormal, daughter->GetCopyNo());
    if (auto found = find_station(daughter, target_name, history)) return found;
    history.BackLevel();
  }
  return std::nullopt;
}

std::shared_ptr<Acts::TrackingGeometry> build_tracking_geometry(
    G4VPhysicalVolume* world_pv) {
  std::array<StationPlacement, kNumStations> stations;

  for (int i = 0; i < kNumStations; ++i) {
    std::string const name = "/SHiP/trackers/station_" + std::to_string(i + 1);
    G4NavigationHistory history;
    history.SetFirstEntry(world_pv);
    auto found = find_station(world_pv, name, history);
    if (!found)
      throw std::runtime_error(
          "acts_geometry_provider: could not find '" + name + "' in geometry");
    stations[i] = *found;
  }

  // Container volume spanning all stations, plus a margin.
  double minZ = stations.front().origin.z();
  double maxZ = stations.front().origin.z();
  double halfX = 0, halfY = 0;
  for (auto const& s : stations) {
    minZ = std::min(minZ, s.origin.z());
    maxZ = std::max(maxZ, s.origin.z());
    halfX = std::max(halfX, s.halfX);
    halfY = std::max(halfY, s.halfY);
  }
  double const margin = 100. * Acts::UnitConstants::mm;
  double const containerHalfZ = 0.5 * (maxZ - minZ) + margin;
  double const containerZ = 0.5 * (maxZ + minZ);

  auto containerBounds = std::make_shared<Acts::CuboidVolumeBounds>(
      halfX + margin, halfY + margin, containerHalfZ);
  Acts::Transform3 containerTrf = Acts::Transform3::Identity();
  containerTrf.translate(
      Acts::Vector3(0., 0., containerZ * Acts::UnitConstants::mm));

  auto worldVolume = std::make_unique<Acts::TrackingVolume>(
      containerTrf, containerBounds, "SpectrometerStations");

  for (int i = 0; i < kNumStations; ++i) {
    auto const& s = stations[i];
    Acts::Transform3 trf = Acts::Transform3::Identity();
    trf.translate(Acts::Vector3(s.origin.x() * Acts::UnitConstants::mm,
                                s.origin.y() * Acts::UnitConstants::mm,
                                s.origin.z() * Acts::UnitConstants::mm));

    auto bounds = std::make_shared<Acts::RectangleBounds>(
        s.halfX * Acts::UnitConstants::mm, s.halfY * Acts::UnitConstants::mm);

    auto surface = Acts::Surface::makeShared<Acts::PlaneSurface>(trf, bounds);
    surface->assignGeometryId(
        Acts::GeometryIdentifier{}.withVolume(1).withSensitive(i + 1));
    // Independent of the GeometryIdentifier's sensitive() component above:
    // Surface::isSensitive() reads its own m_isSensitive flag (default
    // false), which is what CombinatorialKalmanFilter::filter() checks
    // before ever attempting createTrackStates. Without this, the CKF
    // treats every station as non-sensitive and never calibrates a hit.
    surface->assignIsSensitive(true);

    worldVolume->addSurface(surface);
  }

  // TrackingVolume::addSurface only registers these for enumeration
  // (visitSurfaces/apply — what SpectrometerMeasurements uses to match
  // hits); it doesn't make them navigable. Building via the raw
  // TrackingVolume constructor gives a volume with zero portals, which
  // TrackingGeometry::geometryVersion() classifies as legacy Gen1 — and for
  // Gen1, Acts::Navigator ignores navigation policies entirely and only
  // searches for the next surface via the (nonexistent, since we never
  // built any) Layer/LayerArray system. Going through the Blueprint/Gen3
  // construction path instead gives the volume real portals, so the
  // Navigator actually consults the navigation policy — TryAllNavigationPolicy
  // tries every surface in volume->surfaces() directly, which is exactly the
  // addSurface-populated list above. Sensitives only: portals (this volume's
  // own boundary faces) and passives aren't needed for this model.
  Acts::Experimental::Blueprint::Config blueprintCfg;
  auto root = std::make_unique<Acts::Experimental::Blueprint>(blueprintCfg);
  root->addStaticVolume(std::move(worldVolume), [](auto& node) {
    Acts::TryAllNavigationPolicy::Config navPolicyCfg;
    navPolicyCfg.portals = false;
    navPolicyCfg.passives = false;
    node.setNavigationPolicyFactory(
        std::shared_ptr<Acts::NavigationPolicyFactory>(
            Acts::NavigationPolicyFactory{}
                .add<Acts::TryAllNavigationPolicy>(navPolicyCfg)
                .asUniquePtr()));
  });

  Acts::Experimental::BlueprintOptions blueprintOptions;
  auto gctx = Acts::GeometryContext::dangerouslyDefaultConstruct();
  return root->construct(blueprintOptions, gctx);
}

// Mirrors aegir's Geant4Sim destructor (geant4_module.cpp, issue #68): empty
// the geometry stores on the geometry thread before the static store
// destructors run on the main thread at process exit, where the thread-local
// G4 data (initialised only on the geometry thread) was never set up —
// deleting the still-registered volumes there segfaults
// (~G4PVPlacement -> GetRotation -> null per-thread base).
struct G4GeometryCleanup {
  ~G4GeometryCleanup() {
    ship::geometry_thread().run([] {
      G4GeometryManager::GetInstance()->OpenGeometry();
      G4RegionStore::Clean();
      G4PhysicalVolumeStore::Clean();
      G4LogicalVolumeStore::Clean();
      G4SolidStore::Clean();
    });
  }
};

}  // namespace

PHLEX_REGISTER_PROVIDERS(m, config) {
  auto db_file = config.get<std::string>("db_file");

  // Resolve relative/bare filenames via SHIPGEOMETRY_ROOT, same convention
  // as aegir's geometry_geomodel_provider.
  if (!std::filesystem::exists(db_file)) {
    auto resolved = std::filesystem::path(db_file).filename();
    if (auto const* root = std::getenv("SHIPGEOMETRY_ROOT"))
      resolved = std::filesystem::path(root) / "share" / "geometry" / resolved;
    if (std::filesystem::exists(resolved))
      db_file = resolved.string();
    else
      throw std::runtime_error(
          "acts_geometry_provider: cannot locate geometry DB '" + db_file +
          "'; set SHIPGEOMETRY_ROOT or provide an absolute path");
  }

  std::shared_ptr<DetectorGeometry> detector;
  ship::geometry_thread().run([&] {
    auto service = ship::SHiPGeometryService::sharedFromFile(db_file);
    auto* worldLV = service->geant4WorldLogical();
    if (!worldLV)
      throw std::runtime_error(
          "acts_geometry_provider: GeoModel->G4 conversion failed for " +
          db_file);
    auto* worldPV = new G4PVPlacement(nullptr, G4ThreeVector(), worldLV,
                                      worldLV->GetName(), nullptr, false, 0);

    detector = std::make_shared<DetectorGeometry>();
    detector->geometryService = service;
    detector->trackingGeometry = build_tracking_geometry(worldPV);
  });

  auto cleanup = std::make_shared<G4GeometryCleanup>();

  m.provide(
      "read_tracking_geometry",
      [detector, cleanup](data_cell_index const&) -> std::shared_ptr<DetectorGeometry> {
        return detector;
      },
      concurrency::unlimited)
    .output_product("tracking_geometry",
                    phlex::experimental::identifier{"detector"},
                    phlex::experimental::identifier{"job"});
}
