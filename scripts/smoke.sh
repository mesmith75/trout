#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Quick smoke test for CI: generate a small simulation-like input, run the
# digitisation workflow end to end, and check that the outputs were produced.
set -euo pipefail

./build/make_test_input smoke_input.root 10

# Remove outputs from previous runs so stale files cannot pass the checks.
rm -f smoke_digi_output.root smoke_digi_validation.root

phlex -c workflows/smoke.jsonnet

for f in smoke_digi_output.root smoke_digi_validation.root; do
    if ! [ -s "$f" ]; then
        echo "missing output: $f" >&2
        exit 1
    fi
done
python scripts/validate_smoke.py smoke_digi_output.root smoke_digi_validation.root
echo "Smoke test OK"
