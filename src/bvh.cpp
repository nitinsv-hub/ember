#include "bvh.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>

namespace ember {
namespace {

constexpr int kBinCount = 16;
constexpr int kMaxLeafSize = 4;
constexpr float kTraversalCost = 1.0f;
constexpr float kIntersectionCost = 1.5f;

struct Primitive {
    Aabb bounds;
    Vec3 centroid;
    std::int32_t index = 0;
};

struct Bin {
    Aabb bounds;
    int count = 0;
};

struct Builder {
    const std::vector<Primitive>& primitives;
    std::vector<std::int32_t>& order;
    std::vector<BvhNode>& nodes;
    int max_depth = 0;
    int leaf_count = 0;

    Aabb bounds_of(size_t first, size_t count) const {
        Aabb box;
        for (size_t i = 0; i < count; ++i) box.grow(primitives[static_cast<size_t>(order[first + i])].bounds);
        return box;
    }

    Aabb centroid_bounds_of(size_t first, size_t count) const {
        Aabb box;
        for (size_t i = 0; i < count; ++i)
            box.grow(primitives[static_cast<size_t>(order[first + i])].centroid);
        return box;
    }

    void make_leaf(BvhNode& node, const Aabb& box, size_t first, size_t count) {
        node.lo = to_float4(box.lo, int_bits_to_float(static_cast<std::int32_t>(first)));
        node.hi = to_float4(box.hi, int_bits_to_float(static_cast<std::int32_t>(count)));
        ++leaf_count;
    }

    std::int32_t build(size_t first, size_t count, int depth) {
        max_depth = std::max(max_depth, depth);

        const std::int32_t node_index = static_cast<std::int32_t>(nodes.size());
        nodes.emplace_back();
        const Aabb box = bounds_of(first, count);

        if (count <= static_cast<size_t>(kMaxLeafSize)) {
            make_leaf(nodes[static_cast<size_t>(node_index)], box, first, count);
            return node_index;
        }

        const Aabb centroids = centroid_bounds_of(first, count);
        const Vec3 extent = centroids.extent();
        int axis = 0;
        if (extent.y > extent.x) axis = 1;
        if (extent.z > (axis == 0 ? extent.x : extent.y)) axis = 2;
        const float axis_extent = (&extent.x)[axis];

        if (axis_extent < 1e-9f) {
            make_leaf(nodes[static_cast<size_t>(node_index)], box, first, count);
            return node_index;
        }

        const float axis_min = (&centroids.lo.x)[axis];
        const float scale = static_cast<float>(kBinCount) / axis_extent;

        Bin bins[kBinCount];
        for (size_t i = 0; i < count; ++i) {
            const Primitive& p = primitives[static_cast<size_t>(order[first + i])];
            int slot = static_cast<int>(((&p.centroid.x)[axis] - axis_min) * scale);
            slot = std::clamp(slot, 0, kBinCount - 1);
            bins[slot].bounds.grow(p.bounds);
            ++bins[slot].count;
        }

        float left_area[kBinCount - 1];
        int left_count[kBinCount - 1];
        Aabb running;
        int running_count = 0;
        for (int i = 0; i < kBinCount - 1; ++i) {
            running.grow(bins[i].bounds);
            running_count += bins[i].count;
            left_area[i] = running.surface_area();
            left_count[i] = running_count;
        }

        float best_cost = 3.4e38f;
        int best_split = -1;
        Aabb right_running;
        int right_count = 0;
        for (int i = kBinCount - 2; i >= 0; --i) {
            right_running.grow(bins[i + 1].bounds);
            right_count += bins[i + 1].count;
            if (left_count[i] == 0 || right_count == 0) continue;
            const float cost = kTraversalCost +
                               kIntersectionCost *
                                   (left_area[i] * static_cast<float>(left_count[i]) +
                                    right_running.surface_area() * static_cast<float>(right_count)) /
                                   std::max(box.surface_area(), 1e-9f);
            if (cost < best_cost) {
                best_cost = cost;
                best_split = i;
            }
        }

        const float leaf_cost = kIntersectionCost * static_cast<float>(count);
        if (best_split < 0 || (count <= 8 && leaf_cost <= best_cost)) {
            make_leaf(nodes[static_cast<size_t>(node_index)], box, first, count);
            return node_index;
        }

        const auto begin = order.begin() + static_cast<std::ptrdiff_t>(first);
        const auto end = begin + static_cast<std::ptrdiff_t>(count);
        const auto middle = std::partition(begin, end, [&](std::int32_t index) {
            const Primitive& p = primitives[static_cast<size_t>(index)];
            int slot = static_cast<int>(((&p.centroid.x)[axis] - axis_min) * scale);
            slot = std::clamp(slot, 0, kBinCount - 1);
            return slot <= best_split;
        });

        const size_t left_size = static_cast<size_t>(std::distance(begin, middle));
        if (left_size == 0 || left_size == count) {
            make_leaf(nodes[static_cast<size_t>(node_index)], box, first, count);
            return node_index;
        }

        build(first, left_size, depth + 1);
        const std::int32_t right_index = build(first + left_size, count - left_size, depth + 1);

        BvhNode& node = nodes[static_cast<size_t>(node_index)];
        node.lo = to_float4(box.lo, int_bits_to_float(right_index));
        node.hi = to_float4(box.hi, int_bits_to_float(0));
        return node_index;
    }
};

}

Bvh build_bvh(const Scene& scene) {
    const auto start = std::chrono::steady_clock::now();

    Bvh bvh;
    const size_t count = scene.triangle_count();
    if (count == 0) return bvh;

    std::vector<Primitive> primitives(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& tri = scene.positions[i];
        Primitive& p = primitives[i];
        p.index = static_cast<std::int32_t>(i);
        p.bounds.grow(tri[0]);
        p.bounds.grow(tri[1]);
        p.bounds.grow(tri[2]);
        p.centroid = (tri[0] + tri[1] + tri[2]) / 3.0f;
    }

    bvh.triangle_indices.resize(count);
    std::iota(bvh.triangle_indices.begin(), bvh.triangle_indices.end(), 0);
    bvh.nodes.reserve(count * 2);

    Builder builder{primitives, bvh.triangle_indices, bvh.nodes, 0, 0};
    builder.build(0, count, 1);

    bvh.max_depth = builder.max_depth;
    bvh.leaf_count = builder.leaf_count;
    bvh.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return bvh;
}

}
