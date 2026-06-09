#include <iostream>

#include "phlex/module.hpp"

#include <SHiP/SimParticle.hpp>
#include <SHiP/RecParticle.hpp>

using namespace phlex;

double calculateIP(const std::array<double, 3>& r, const std::array<double, 3>& p) {
    // placeholder for impact parameter - calculated with respect to the origin
    const double p2 = p[0]*p[0] + p[1]*p[1] + p[2]*p[2];
    if (p2 < 1e-30) return 0.0;

    // Cross product r × p
    const double cx = r[1]*p[2] - r[2]*p[1];
    const double cy = r[2]*p[0] - r[0]*p[2];
    const double cz = r[0]*p[1] - r[1]*p[0];

    return std::sqrt((cx*cx + cy*cy + cz*cz) / p2);
}



PHLEX_REGISTER_ALGORITHMS(m, config)
{
  auto const layer = config.get<std::string>("layer");

  // Identity transform: takes "value" in, produces "processed_value" out
  m.transform(
     "ip_selector",[]( std::vector<SHiP::SimParticle> const& ip ) -> std::vector<SHiP::RecParticle> { 
        std::vector<SHiP::RecParticle> op;
        for(auto &p : ip) {
            op.emplace_back(SHiP::fromSimParticle(p));
            op.back().ipPV = calculateIP(p.vertex, p.momentum);
        }
        return op;
     },
     concurrency::unlimited)
    .input_family(product_query{.creator = "rntuple_source", .layer = layer, .suffix="sim_particles"})
    .output_product_suffixes("processed_value");
}
