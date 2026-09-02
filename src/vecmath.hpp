#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ember {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(Vec3 v) {
    const float len = length(v);
    return len > 0.0f ? v / len : Vec3{0.0f, 1.0f, 0.0f};
}

inline Vec3 min(Vec3 a, Vec3 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

inline Vec3 max(Vec3 a, Vec3 b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

inline float max_component(Vec3 v) { return std::max(v.x, std::max(v.y, v.z)); }

inline float luminance(Vec3 v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; }

struct Aabb {
    Vec3 lo{3.4e38f, 3.4e38f, 3.4e38f};
    Vec3 hi{-3.4e38f, -3.4e38f, -3.4e38f};

    void grow(Vec3 p) {
        lo = ember::min(lo, p);
        hi = ember::max(hi, p);
    }

    void grow(const Aabb& b) {
        lo = ember::min(lo, b.lo);
        hi = ember::max(hi, b.hi);
    }

    Vec3 extent() const { return hi - lo; }
    Vec3 center() const { return (lo + hi) * 0.5f; }

    float surface_area() const {
        const Vec3 e = extent();
        if (e.x < 0.0f || e.y < 0.0f || e.z < 0.0f) return 0.0f;
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }

    bool valid() const { return hi.x >= lo.x && hi.y >= lo.y && hi.z >= lo.z; }
};

struct alignas(16) Float4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

static_assert(sizeof(Float4) == 16);

inline Float4 to_float4(Vec3 v, float w = 0.0f) { return Float4{v.x, v.y, v.z, w}; }

inline float int_bits_to_float(std::int32_t i) {
    float f;
    std::memcpy(&f, &i, sizeof(f));
    return f;
}

}
