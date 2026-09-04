// SPDX-FileCopyrightText: 2026 CERN for the benefit of the SHiP Collaboration
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// philox_rng.hpp — counter-based RNG for reproducible digitisation
//
// Random123 Philox 4x32 is deterministic per seed with no shared state, so
// each event seeds a fresh instance and digitisation is reproducible and
// thread-safe by construction (ported from aegir).
//
// The counter advances sequentially (ctr[0]++ per 4-word block) and the
// Philox output block is buffered; uniform() returns successive words of the
// buffered output.

#pragma once

#include <Random123/philox.h>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace Shannon {

class PhiloxRng {
   public:
    // key_hi selects an independent stream, so different generators seeded
    // with the same seed draw uncorrelated sequences. ctr1 initializes the
    // second counter word, giving each (seed, key_hi, ctr1) triple a disjoint
    // counter range — use it for per-event sub-streams of one seed without
    // perturbing the key (a key derived as seed ^ event would collide across
    // seeds: XOR is not injective in (seed, event)).
    explicit PhiloxRng(std::uint32_t seed, std::uint32_t key_hi = 0xBEEFCAFE,
                       std::uint32_t ctr1 = 0)
        : key_{{seed, key_hi}}, ctr_{{0, ctr1, 0, 0}} {}

    double uniform() {
        if (idx_ >= 4) {
            buf_ = rng_(ctr_, key_);
            ctr_[0]++;
            idx_ = 0;
        }
        // Map a 32-bit word to [0, 1)
        return buf_[idx_++] * (1.0 / 4294967296.0);
    }

    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }

    // Higher-resolution draw: combines two consecutive 32-bit Philox words
    // into a 64-bit integer and keeps the top 53 bits, matching double's
    // full mantissa precision (cf. uniform(), which only has 32 bits of
    // resolution — too coarse when the sampled range spans many orders of
    // magnitude, e.g. ps-scale jitter within a multi-second spill).
    double uniform53() {
        if (idx_ > 2) {  // fewer than 2 words left in the buffer
            buf_ = rng_(ctr_, key_);
            ctr_[0]++;
            idx_ = 0;
        }
        std::uint64_t const hi = buf_[idx_++];
        std::uint64_t const lo = buf_[idx_++];
        std::uint64_t const bits = (hi << 32) | lo;
        return static_cast<double>(bits >> 11) * (1.0 / 9007199254740992.0);  // top 53 bits / 2^53
    }

    double uniform53(double lo, double hi) { return lo + (hi - lo) * uniform53(); }

    // Box–Muller transform; uses the cosine branch only, drawing two
    // uniforms per deviate. uniform() returns [0, 1), so flip the first
    // draw to (0, 1] to keep the log argument nonzero.
    double gaussian(double mean, double sigma) {
        double u1 = 1.0 - uniform();
        double u2 = uniform();
        return mean +
               sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);
    }

   private:
    r123::Philox4x32 rng_;
    r123::Philox4x32::key_type key_;
    r123::Philox4x32::ctr_type ctr_;
    r123::Philox4x32::ctr_type buf_{};
    int idx_ = 4;
};

}  // namespace Shannon
