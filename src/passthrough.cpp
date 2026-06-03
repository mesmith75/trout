#include "phlex/module.hpp"

using namespace phlex;

PHLEX_REGISTER_ALGORITHMS(m, config)
{
  auto const layer = config.get<std::string>("layer");

  // Identity transform: takes "value" in, produces "processed_value" out
  m.transform(
     "passthrough",
     [](double value) -> double { return value; },
     concurrency::unlimited)
    .input_family(product_query{.creator = "file_source", .layer = layer, .suffix = "value"})
    .output_product_suffixes("processed_value");
}
