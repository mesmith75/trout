#include "phlex/core/product_selector.hpp"
#include "phlex/module.hpp"

#include <SHiP/detectors/CaloHit.hpp>
#include <iostream>
using namespace phlex;

PHLEX_REGISTER_ALGORITHMS(m, config) {
    auto const layer = config.get<std::string>("layer");

    // Identity transform: takes "value" in, produces "value" out
    m.transform(
         "calorimeter_reco",
         [](std::vector<SHiP::CaloHit> const& particles) -> std::vector<SHiP::CaloHit> {
             return particles;
         },
         concurrency::unlimited)
        .input_family(product_selector{
            .creator = "rntuple_source", .layer = layer, .suffix = "calorimeter_hits"})
        .output_product_suffixes("calorimeter_reco");
}