#include "phlex/module.hpp"

#include <SHiP/SimParticle.hpp>
#include <iostream>

using namespace phlex;

PHLEX_REGISTER_ALGORITHMS(m, config) {
    m.observe("debug_observer",
              [](std::vector<SHiP::SimParticle> const& product) { std::cout << "Got product!\n"; })
        .input_family(product_query{
            .creator = "rntuple_unfold", .layer = "spill", .suffix = "sim_particles"});
}
