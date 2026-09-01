#include "phlex/core/product_selector.hpp"
#include "phlex/module.hpp"

#include <SHiP/detectors/SBTHit.hpp>
#include <iostream>
using namespace phlex;

PHLEX_REGISTER_ALGORITHMS(m, config) {
    auto const layer = config.get<std::string>("layer");

    // Identity transform: takes "value" in, produces "value" out
    m.transform(
         "surround_tagger_reco",
         [](std::vector<SHiP::SBTHit> const& particles) -> std::vector<SHiP::SBTHit> {
             return particles;
         },
         concurrency::unlimited)
        .input_family(product_selector{
            .creator = "rntuple_source", .layer = layer, .suffix = "sbt_hits"})
        .output_product_suffixes("surround_tagger_reco");
}