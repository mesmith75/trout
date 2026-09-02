#include "phlex/core/product_selector.hpp"
#include "phlex/module.hpp"

#include <SHiP/detectors/TimeDetHit.hpp>
#include <iostream>
using namespace phlex;

PHLEX_REGISTER_ALGORITHMS(m, config) {
    auto const layer = config.get<std::string>("layer");

    // Identity transform: takes "value" in, produces "value" out
    m.transform(
         "timing_detector_reco",
         [](std::vector<SHiP::TimeDetHit> const& particles) -> std::vector<SHiP::TimeDetHit> {
             return particles;
         },
         concurrency::unlimited)
        .input_family(product_selector{
            .creator = "rntuple_source", .layer = layer, .suffix = "timing_detector_hits"})
        .output_product_suffixes("timing_detector_reco");
}
