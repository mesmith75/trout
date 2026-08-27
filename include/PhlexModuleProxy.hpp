// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "phlex/module.hpp"

// The type of the `m` parameter PHLEX_REGISTER_ALGORITHMS hands the plugin
// entry point — named here so each pipeline stage's registration function
// (register_prepare_measurements, register_generate_seeds, register_fit_seed)
// can take it as an ordinary function parameter instead of every stage's
// registration living inside one PHLEX_REGISTER_ALGORITHMS block.
using ModuleProxy = phlex::detail::module_graph_proxy<phlex::detail::void_tag>;
