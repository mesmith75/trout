#include "phlex/core/product_selector.hpp"
#include "phlex/module.hpp"

#include <SHiP/detectors/UBTHit.hpp>
#include <iostream>
using namespace phlex;

PHLEX_REGISTER_ALGORITHMS(m, config) {
    auto const layer = config.get<std::string>("layer");

    // Identity transform: takes "value" in, produces "value" out
    m.transform(
         "upstream_tagger_reco",
         [](std::vector<SHiP::UBTHit> const& particles) -> std::vector<SHiP::UBTHit> {
             return particles;
         },
         concurrency::unlimited)
        .input_family(
            product_selector{.creator = "rntuple_source", .layer = layer, .suffix = "ubt_hits"})
        .output_product_suffixes("upstream_tagger_reco");
}
