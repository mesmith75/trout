// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// make_test_input.cpp — deterministic simulation-like input for smoke tests
//
// Writes one RNTuple per detector type (sim_particles, sim_hits, ubt_hits,
// sbt_hits, straw_tubes_hits, calorimeter_hits, timing_detector_hits), each
// a flat record-per-hit stream — the same shape shannon's
// digitised_output_module.cpp writes and read_digitised_hits.cpp reads (see
// TypedHitWriter/TypedHitWriters in typed_rntuple_writer.hpp) — so trout's
// reconstruction can be smoke tested without Geant4, aegir, or shannon.

#include "io/typed_rntuple_writer.hpp"

#include <SHiP/SimHit.hpp>
#include <SHiP/SimParticle.hpp>
#include <SHiP/detectors/CaloHit.hpp>
#include <SHiP/detectors/SBTHit.hpp>
#include <SHiP/detectors/StrawTubesHit.hpp>
#include <SHiP/detectors/TimeDetHit.hpp>
#include <SHiP/detectors/UBTHit.hpp>
#include <SHiP/detectors/detector_id.hpp>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <philox_rng.hpp>
#include <string>

std::vector<double> stat_z = {84070, 86070, 93070, 95070};

using SmokeTestWriters = TypedHitWriters<
    NamedHit<"sim_particles", SHiP::SimParticle>, NamedHit<"sim_hits", SHiP::SimHit>,
    NamedHit<"ubt_hits", SHiP::UBTHit>, NamedHit<"sbt_hits", SHiP::SBTHit>,
    NamedHit<"straw_tubes_hits", SHiP::StrawTubesHit>, NamedHit<"calorimeter_hits", SHiP::CaloHit>,
    NamedHit<"timing_detector_hits", SHiP::TimeDetHit>>;

int main(int argc, char* argv[]) {
    std::string const filename = argc > 1 ? argv[1] : "smoke_input.root";
    auto const n_events = argc > 2 ? std::stoul(argv[2]) : 10UL;
    // The event loop counts (and seeds the RNG) with a uint32_t.
    if (n_events > std::numeric_limits<std::uint32_t>::max()) {
        std::fprintf(stderr, "n_events out of range: %lu\n", n_events);
        return 1;
    }

    RNTupleFileService file_service{filename};
    SmokeTestWriters writers{file_service};

    constexpr SHiP::detector_id detectors[] = {
        SHiP::detector_id::UpstreamTagger, SHiP::detector_id::SurroundTagger,
        SHiP::detector_id::StrawTubes, SHiP::detector_id::Calorimeter,
        SHiP::detector_id::TimingDetector};

    for (std::uint32_t event = 0; event < n_events; ++event) {
        Shannon::PhiloxRng rng{0, 0x7E57DA7A, event};

        auto const n_tracks = 1 + static_cast<int>(rng.uniform(0.0, 3.0));
        for (int track = 0; track < n_tracks; ++track) {
            SHiP::SimParticle particle;
            particle.trackId = track;
            particle.parentId = -1;
            particle.pdgCode = 13;
            particle.momentum = {rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5),
                                 rng.uniform(10.0, 100.0)};
            particle.energy = particle.momentum[2];
            writers.get<SHiP::SimParticle>().write(particle);

            for (auto const detector : detectors) {
                SHiP::SimHit sim_hit;
                sim_hit.detectorId = static_cast<std::int32_t>(detector);
                sim_hit.trackId = track;
                sim_hit.pdgCode = particle.pdgCode;

                if (detector == SHiP::detector_id::StrawTubes) {
                    for (int stat = 0; stat < stat_z.size(); stat++) {
                        SHiP::SimHit sim_hit;
                        sim_hit.detectorId = static_cast<std::int32_t>(detector);
                        sim_hit.trackId = track;
                        sim_hit.pdgCode = particle.pdgCode;
                        sim_hit.position = {rng.uniform(-500.0, 500.0), rng.uniform(-500.0, 500.0),
                                            stat_z[stat]};
                        sim_hit.momentum = particle.momentum;
                        sim_hit.energyDeposit = rng.uniform(0.0, 0.1);
                        sim_hit.time = rng.uniform(0.0, 100.0);
                        sim_hit.pathLength = rng.uniform(0.0, 10.0);
                        writers.get<SHiP::SimHit>().write(sim_hit);
                        SHiP::StrawTubesHit hit;
                        hit.recHit = SHiP::fromSimHit(sim_hit);
                        writers.get<SHiP::StrawTubesHit>().write(hit);
                    }
                } else {
                    SHiP::SimHit sim_hit;
                    sim_hit.detectorId = static_cast<std::int32_t>(detector);
                    sim_hit.trackId = track;
                    sim_hit.pdgCode = particle.pdgCode;
                    sim_hit.position = {rng.uniform(-500.0, 500.0), rng.uniform(-500.0, 500.0),
                                        rng.uniform(0.0, 10000.0)};
                    sim_hit.momentum = particle.momentum;
                    sim_hit.energyDeposit = rng.uniform(0.0, 0.1);
                    sim_hit.time = rng.uniform(0.0, 100.0);
                    sim_hit.pathLength = rng.uniform(0.0, 10.0);
                    writers.get<SHiP::SimHit>().write(sim_hit);

                    switch (detector) {
                        case SHiP::detector_id::UpstreamTagger: {
                            SHiP::UBTHit hit;
                            hit.recHit = SHiP::fromSimHit(sim_hit);
                            writers.get<SHiP::UBTHit>().write(hit);
                            break;
                        }
                        case SHiP::detector_id::SurroundTagger: {
                            SHiP::SBTHit hit;
                            hit.recHit = SHiP::fromSimHit(sim_hit);
                            writers.get<SHiP::SBTHit>().write(hit);
                            break;
                        }
                        case SHiP::detector_id::Calorimeter: {
                            SHiP::CaloHit hit;
                            hit.recHit = SHiP::fromSimHit(sim_hit);
                            writers.get<SHiP::CaloHit>().write(hit);
                            break;
                        }
                        case SHiP::detector_id::TimingDetector: {
                            SHiP::TimeDetHit hit;
                            hit.recHit = SHiP::fromSimHit(sim_hit);
                            writers.get<SHiP::TimeDetHit>().write(hit);
                            break;
                        }
                    }
                }
            }
        }
    }

    std::printf("Wrote %lu events to %s\n", n_events, filename.c_str());
    return 0;
}
