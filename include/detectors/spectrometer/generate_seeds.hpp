// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "GeometryContainers.hpp"
#include "PhlexModuleProxy.hpp"
#include "SpillContext.hpp"
#include "phlex/core/product_selector.hpp"

#include <memory>
#include <string>

// A single seed's identity: two hit indices into SpillContext::measurements
// (one at station 1, one at station 2) — fit_seed rebuilds the actual
// BoundTrackParameters via makeSeedFromHitPair.
struct SeedHitPair {
    ActsExamples::Index idx0{0};
    ActsExamples::Index idx1{0};
};

// generate_seeds' actual per-child output: the pair plus the SpillContext it
// came from, bundled together into one same-layer product for fit_seed (see
// register_generate_seeds's doc comment for why this isn't instead a
// separate, cross-layer broadcast input on fit_seed's side).
struct SeedWithContext {
    std::shared_ptr<SpillContext> ctx;
    SeedHitPair pair;
};

// generate_seeds (spill layer, unfold): walks every station-1/station-2 hit
// pair in the spill, applying a time-coincidence + forward-direction cut,
// and spawns one "seed"-layer child data cell per surviving pair. No static
// `total` is declared for the "seed" layer — phlex::unfold sizes it
// dynamically at runtime.
void register_generate_seeds(ModuleProxy& m, phlex::experimental::identifier const& layer,
                             std::string const& seedLayerName);
