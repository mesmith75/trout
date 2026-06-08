#include "phlex/module.hpp"
#include <SHiP/SimParticle.hpp>
#include <iostream>
using namespace phlex;

PHLEX_REGISTER_ALGORITHMS(m, config)
{
  auto const layer = config.get<std::string>("layer");

  // Identity transform: takes "value" in, produces "processed_value" out
  m.transform(
     "passthrough",
     [](std::vector<SHiP::SimParticle> const& particles ) -> std::vector<SHiP::SimParticle> { 
      std::cout<<"wassup! "<<particles.size()<<std::endl;
      for(auto &p : particles)
        std::cout<<"energy: "<<p.energy<<std::endl;

      return particles;
    },
     concurrency::unlimited)
    .input_family(product_query{.creator = "rntuple_source", .layer = layer, .suffix="sim_particles"})
    .output_product_suffixes("processed_value");
}
