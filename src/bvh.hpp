#pragma once

#include "scene.hpp"
#include "vecmath.hpp"

#include <cstdint>
#include <vector>

namespace ember {

struct BvhNode {
    Float4 lo;
    Float4 hi;
};

struct Bvh {
    std::vector<BvhNode> nodes;
    std::vector<std::int32_t> triangle_indices;
    int max_depth = 0;
    int leaf_count = 0;
    double build_seconds = 0.0;
};

Bvh build_bvh(const Scene& scene);

}
