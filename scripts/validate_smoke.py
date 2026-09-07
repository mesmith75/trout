#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
#
# SPDX-License-Identifier: LGPL-3.0-or-later

"""Validate the smoke-test outputs of the digitisation workflow.

Checks that the digitised-output file contains all per-detector RNTuples and
that the validation file contains all histograms, each with entries.
"""

import os
import sys

import ROOT

NTUPLES = [
    "calorimeter",
    "surround_tagger",
    "spectrometer_tracks",
    "timing_detector",
    "upstream_tagger",
    "sim_hits",
    "sim_particles"
]

HISTOGRAMS = [
    "h_upstream_tagger_multiplicity",
    "h_surround_tagger_multiplicity",
    "h_spectrometer_track_multiplicity",
    "h_calorimeter_multiplicity",
    "h_timing_detector_multiplicity",
    "h_sim_hit_multiplicity",
    "h_sim_particle_multiplicity",
    "h_sim_hit_x",
    "h_sim_hit_y",
    "h_sim_hit_z",
    "h_ref_x",
    "h_ref_y",
    "h_ref_z",    
    "h_sim_particle_vtx_x",
    "h_sim_particle_vtx_y",
    "h_sim_particle_vtx_z",
]


def check_ntuples(path):
    errors = []
    for name in NTUPLES:
        try:
            reader = ROOT.RNTupleReader.Open(name, path)
        except Exception as e:
            errors.append(f"{path}: cannot open RNTuple '{name}': {e}")
            continue
        if reader.GetNEntries() == 0:
            errors.append(f"{path}: RNTuple '{name}' has no entries")
    return errors


def check_histograms(path):
    try:
        file = ROOT.TFile.Open(path)
    except OSError as e:
        return [f"{path}: cannot open file: {e}"]
    if not file or file.IsZombie():
        return [f"{path}: cannot open file"]
    errors = []
    for name in HISTOGRAMS:
        hist = file.Get(name)
        if not hist:
            errors.append(f"{path}: missing histogram '{name}'")
        elif hist.GetEntries() == 0:
            errors.append(f"{path}: histogram '{name}' has no entries")
    return errors


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <smoke_results.root> <reco_validation.root>", file=sys.stderr)
        sys.exit(2)
    errors = check_ntuples(sys.argv[1]) + check_histograms(sys.argv[2])
    for error in errors:
        print(error, file=sys.stderr)
    sys.stdout.flush()
    sys.stderr.flush()
    # Skip interpreter shutdown to dodge a PyROOT RNTuple teardown crash.
    os._exit(1 if errors else 0)
