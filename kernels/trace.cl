#define PI 3.14159265358979323846f
#define INV_PI 0.31830988618379067154f
#define RAY_EPS 1e-4f
#define T_FAR 1e30f
#define STACK_SIZE 32

#define BSDF_DIFFUSE 0
#define BSDF_METAL 1
#define BSDF_DIELECTRIC 2

typedef struct {
    float3 position;
    float3 normal;
    float3 geometric_normal;
    float t;
    int triangle;
    int material;
} Hit;

typedef struct {
    float3 albedo;
    float3 emission;
    float roughness;
    float ior;
    int type;
} Surface;

inline uint pcg_next(uint *state) {
    uint s = *state;
    *state = s * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

inline float rnd(uint *state) {
    return (float)(pcg_next(state) >> 8) * (1.0f / 16777216.0f);
}

inline uint seed_for(uint x, uint y, uint frame) {
    uint h = x * 0x9E3779B1u;
    h ^= (y + 0x85EBCA6Bu + (h << 6) + (h >> 2));
    h ^= (frame + 0xC2B2AE35u + (h << 6) + (h >> 2));
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h | 1u;
}

inline float3 basis_transform(float3 n, float3 v) {
    float s = (n.z >= 0.0f) ? 1.0f : -1.0f;
    float a = -1.0f / (s + n.z);
    float b = n.x * n.y * a;
    float3 t = (float3)(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
    float3 bt = (float3)(b, s + n.y * n.y * a, -n.y);
    return v.x * t + v.y * bt + v.z * n;
}

inline float3 sample_cosine(float3 n, uint *rs) {
    float u1 = rnd(rs);
    float u2 = rnd(rs);
    float r = sqrt(u1);
    float phi = 2.0f * PI * u2;
    float3 direction = (float3)(r * cos(phi), r * sin(phi), sqrt(fmax(0.0f, 1.0f - u1)));
    return basis_transform(n, direction);
}

inline float ggx_distribution(float n_dot_h, float alpha) {
    float a2 = alpha * alpha;
    float d = n_dot_h * n_dot_h * (a2 - 1.0f) + 1.0f;
    return a2 / fmax(PI * d * d, 1e-9f);
}

inline float smith_g1(float n_dot_v, float alpha) {
    float k = alpha * 0.5f;
    return n_dot_v / fmax(n_dot_v * (1.0f - k) + k, 1e-9f);
}

inline float3 fresnel_schlick_color(float3 f0, float cos_theta) {
    float m = clamp(1.0f - cos_theta, 0.0f, 1.0f);
    float m2 = m * m;
    return f0 + (1.0f - f0) * (m2 * m2 * m);
}

inline float fresnel_dielectric(float cos_i, float ior) {
    float r0 = (1.0f - ior) / (1.0f + ior);
    r0 = r0 * r0;
    float m = 1.0f - cos_i;
    float m2 = m * m;
    return r0 + (1.0f - r0) * (m2 * m2 * m);
}

inline float3 sample_ggx_half(float3 n, float alpha, uint *rs) {
    float u1 = rnd(rs);
    float u2 = rnd(rs);
    float phi = 2.0f * PI * u1;
    float cos_theta = sqrt((1.0f - u2) / fmax(1.0f + (alpha * alpha - 1.0f) * u2, 1e-9f));
    float sin_theta = sqrt(fmax(0.0f, 1.0f - cos_theta * cos_theta));
    float3 direction = (float3)(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
    return basis_transform(n, direction);
}

inline float3 sky_radiance(float3 dir, float4 zenith, float4 horizon) {
    float t = clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
    return mix(horizon.xyz, zenith.xyz, t * t);
}

inline float3 sample_cone(float3 axis, float cos_max, uint *rs, float *pdf) {
    float u1 = rnd(rs);
    float u2 = rnd(rs);
    float cos_theta = 1.0f - u1 * (1.0f - cos_max);
    float sin_theta = sqrt(fmax(0.0f, 1.0f - cos_theta * cos_theta));
    float phi = 2.0f * PI * u2;
    float3 offset = (float3)(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
    *pdf = 1.0f / fmax(2.0f * PI * (1.0f - cos_max), 1e-9f);
    return normalize(basis_transform(axis, offset));
}

inline float power_heuristic(float a, float b) {
    float aa = a * a;
    float bb = b * b;
    return aa / fmax(aa + bb, 1e-9f);
}

inline bool slab_test(float3 lo, float3 hi, float3 origin, float3 inv_dir, float t_max, float *t_enter) {
    float3 t0 = (lo - origin) * inv_dir;
    float3 t1 = (hi - origin) * inv_dir;
    float3 tsmall = fmin(t0, t1);
    float3 tbig = fmax(t0, t1);
    float near = fmax(fmax(tsmall.x, tsmall.y), fmax(tsmall.z, 0.0f));
    float far = fmin(fmin(tbig.x, tbig.y), fmin(tbig.z, t_max));
    *t_enter = near;
    return near <= far;
}

inline bool intersect_triangle(float3 a, float3 b, float3 c, float3 origin, float3 dir,
                               float t_max, float *out_t, float *out_u, float *out_v) {
    float3 e1 = b - a;
    float3 e2 = c - a;
    float3 p = cross(dir, e2);
    float det = dot(e1, p);
    if (fabs(det) < 1e-12f) return false;
    float inv_det = 1.0f / det;
    float3 tv = origin - a;
    float u = dot(tv, p) * inv_det;
    if (u < -1e-6f || u > 1.0f + 1e-6f) return false;
    float3 q = cross(tv, e1);
    float v = dot(dir, q) * inv_det;
    if (v < -1e-6f || u + v > 1.0f + 1e-6f) return false;
    float t = dot(e2, q) * inv_det;
    if (t <= RAY_EPS || t >= t_max) return false;
    *out_t = t;
    *out_u = u;
    *out_v = v;
    return true;
}

inline bool traverse(__global const float4 *nodes,
                     __global const float4 *vertices,
                     __global const float4 *normals,
                     __global const int *triangle_materials,
                     __global const int *triangle_order,
                     float3 origin, float3 dir, float t_max, bool any_hit, Hit *hit) {
    float3 inv_dir = (float3)(1.0f / (fabs(dir.x) < 1e-12f ? copysign(1e-12f, dir.x) : dir.x),
                              1.0f / (fabs(dir.y) < 1e-12f ? copysign(1e-12f, dir.y) : dir.y),
                              1.0f / (fabs(dir.z) < 1e-12f ? copysign(1e-12f, dir.z) : dir.z));

    int stack[STACK_SIZE];
    int sp = 0;
    int node = 0;
    bool found = false;
    float best_u = 0.0f;
    float best_v = 0.0f;

    hit->t = t_max;
    hit->triangle = -1;

    while (true) {
        float4 lo = nodes[node * 2 + 0];
        float4 hi = nodes[node * 2 + 1];
        float t_enter;
        if (slab_test(lo.xyz, hi.xyz, origin, inv_dir, hit->t, &t_enter)) {
            int count = as_int(hi.w);
            if (count > 0) {
                int first = as_int(lo.w);
                for (int i = 0; i < count; ++i) {
                    int tri = triangle_order[first + i];
                    float3 a = vertices[tri * 3 + 0].xyz;
                    float3 b = vertices[tri * 3 + 1].xyz;
                    float3 c = vertices[tri * 3 + 2].xyz;
                    float t, u, v;
                    if (intersect_triangle(a, b, c, origin, dir, hit->t, &t, &u, &v)) {
                        hit->t = t;
                        hit->triangle = tri;
                        best_u = u;
                        best_v = v;
                        found = true;
                        if (any_hit) return true;
                    }
                }
            } else {
                if (sp < STACK_SIZE) stack[sp++] = as_int(lo.w);
                node = node + 1;
                continue;
            }
        }
        if (sp == 0) break;
        node = stack[--sp];
    }

    if (!found) return false;

    int tri = hit->triangle;
    float3 a = vertices[tri * 3 + 0].xyz;
    float3 b = vertices[tri * 3 + 1].xyz;
    float3 c = vertices[tri * 3 + 2].xyz;
    float3 na = normals[tri * 3 + 0].xyz;
    float3 nb = normals[tri * 3 + 1].xyz;
    float3 nc = normals[tri * 3 + 2].xyz;

    hit->position = origin + dir * hit->t;
    hit->geometric_normal = normalize(cross(b - a, c - a));
    float3 shading = na * (1.0f - best_u - best_v) + nb * best_u + nc * best_v;
    hit->normal = (dot(shading, shading) > 1e-12f) ? normalize(shading) : hit->geometric_normal;
    hit->material = triangle_materials[tri];
    return true;
}

inline Surface fetch_surface(__global const float4 *materials, int index) {
    Surface s;
    float4 a = materials[index * 3 + 0];
    float4 e = materials[index * 3 + 1];
    float4 p = materials[index * 3 + 2];
    s.albedo = a.xyz;
    s.type = as_int(a.w);
    s.emission = e.xyz;
    s.roughness = e.w;
    s.ior = p.x;
    return s;
}

inline float triangle_area(__global const float4 *vertices, int tri) {
    float3 a = vertices[tri * 3 + 0].xyz;
    float3 b = vertices[tri * 3 + 1].xyz;
    float3 c = vertices[tri * 3 + 2].xyz;
    return 0.5f * length(cross(b - a, c - a));
}

inline int pick_light(__global const float *light_cdf, int light_count, float u) {
    int lo = 0;
    int hi = light_count - 1;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (light_cdf[mid] < u) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

inline float3 evaluate_diffuse(Surface s) { return s.albedo * INV_PI; }

inline float3 evaluate_metal(Surface s, float3 n, float3 wo, float3 wi, float *pdf) {
    float3 h = wo + wi;
    if (dot(h, h) < 1e-12f) { *pdf = 0.0f; return (float3)(0.0f, 0.0f, 0.0f); }
    h = normalize(h);
    float alpha = fmax(s.roughness * s.roughness, 1e-3f);
    float n_dot_h = fmax(dot(n, h), 0.0f);
    float n_dot_v = fmax(dot(n, wo), 1e-5f);
    float n_dot_l = fmax(dot(n, wi), 1e-5f);
    float v_dot_h = fmax(dot(wo, h), 1e-5f);
    float d = ggx_distribution(n_dot_h, alpha);
    float g = smith_g1(n_dot_v, alpha) * smith_g1(n_dot_l, alpha);
    float3 f = fresnel_schlick_color(s.albedo, v_dot_h);
    *pdf = d * n_dot_h / (4.0f * v_dot_h);
    return f * (d * g / (4.0f * n_dot_v * n_dot_l));
}

__kernel void trace(__global float4 *accumulator,
                    __global const float4 *nodes,
                    __global const float4 *vertices,
                    __global const float4 *normals,
                    __global const int *triangle_materials,
                    __global const int *triangle_order,
                    __global const float4 *materials,
                    __global const int *light_triangles,
                    __global const float *light_cdf,
                    const int light_count,
                    const float light_area,
                    const float4 sky_zenith,
                    const float4 sky_horizon,
                    const float4 sun_direction,
                    const float4 sun_radiance,
                    const int width,
                    const int height,
                    const int frame,
                    const int samples,
                    const int max_bounces,
                    const float4 camera_position,
                    const float4 camera_forward,
                    const float4 camera_right,
                    const float4 camera_up,
                    const float tan_half_fov,
                    const float radiance_clamp) {
    int px = get_global_id(0);
    int py = get_global_id(1);
    if (px >= width || py >= height) return;

    uint rs = seed_for((uint)px, (uint)py, (uint)frame);
    float aspect = (float)width / (float)height;
    float3 total = (float3)(0.0f, 0.0f, 0.0f);

    for (int s = 0; s < samples; ++s) {
        float sx = ((float)px + rnd(&rs)) / (float)width * 2.0f - 1.0f;
        float sy = 1.0f - ((float)py + rnd(&rs)) / (float)height * 2.0f;
        float3 origin = camera_position.xyz;
        float3 dir = normalize(camera_forward.xyz + camera_right.xyz * (sx * aspect * tan_half_fov) +
                               camera_up.xyz * (sy * tan_half_fov));

        float3 radiance = (float3)(0.0f, 0.0f, 0.0f);
        float3 throughput = (float3)(1.0f, 1.0f, 1.0f);
        float prev_pdf = 0.0f;
        bool specular = true;

        for (int bounce = 0; bounce < max_bounces; ++bounce) {
            Hit hit;
            if (!traverse(nodes, vertices, normals, triangle_materials, triangle_order, origin, dir,
                          T_FAR, false, &hit)) {
                radiance += throughput * sky_radiance(dir, sky_zenith, sky_horizon);
                if (sun_radiance.w > 0.0f) {
                    float cos_max = sun_direction.w;
                    if (dot(dir, sun_direction.xyz) >= cos_max) {
                        float pdf_sun = 1.0f / fmax(2.0f * PI * (1.0f - cos_max), 1e-9f);
                        float weight = specular ? 1.0f : power_heuristic(prev_pdf, pdf_sun);
                        radiance += throughput * sun_radiance.xyz * weight;
                    }
                }
                break;
            }

            Surface surface = fetch_surface(materials, hit.material);
            float3 shading_normal = hit.normal;
            if (dot(shading_normal, dir) > 0.0f && surface.type != BSDF_DIELECTRIC)
                shading_normal = -shading_normal;

            if (surface.emission.x > 0.0f || surface.emission.y > 0.0f || surface.emission.z > 0.0f) {
                float cos_light = fabs(dot(hit.geometric_normal, dir));
                if (specular || light_count == 0 || cos_light < 1e-6f) {
                    radiance += throughput * surface.emission;
                } else {
                    float pdf_light = (hit.t * hit.t) / fmax(cos_light * light_area, 1e-9f);
                    radiance += throughput * surface.emission * power_heuristic(prev_pdf, pdf_light);
                }
            }

            float3 wo = -dir;

            if (surface.type == BSDF_DIELECTRIC) {
                float ior = surface.ior > 1.0f ? surface.ior : 1.5f;
                bool entering = dot(dir, hit.normal) < 0.0f;
                float3 oriented = entering ? hit.normal : -hit.normal;
                float eta = entering ? (1.0f / ior) : ior;
                float cos_i = fabs(dot(dir, oriented));
                float k = 1.0f - eta * eta * (1.0f - cos_i * cos_i);
                float3 reflected = dir - 2.0f * dot(dir, oriented) * oriented;
                if (k < 0.0f) {
                    dir = reflected;
                } else {
                    float3 refracted = normalize(eta * dir + (eta * cos_i - sqrt(k)) * oriented);
                    dir = (rnd(&rs) < fresnel_dielectric(cos_i, ior)) ? reflected : refracted;
                }
                origin = hit.position + dir * RAY_EPS;
                throughput *= surface.albedo;
                specular = true;
            } else {
                bool is_metal = (surface.type == BSDF_METAL);
                float alpha = fmax(surface.roughness * surface.roughness, 1e-3f);
                bool smooth_metal = is_metal && surface.roughness < 0.08f;

                if (!smooth_metal && sun_radiance.w > 0.0f) {
                    float pdf_sun;
                    float3 wi = sample_cone(sun_direction.xyz, sun_direction.w, &rs, &pdf_sun);
                    float cos_surface = dot(shading_normal, wi);
                    if (cos_surface > 0.0f) {
                        Hit shadow;
                        float3 shadow_origin = hit.position + shading_normal * RAY_EPS;
                        if (!traverse(nodes, vertices, normals, triangle_materials, triangle_order,
                                      shadow_origin, wi, T_FAR, true, &shadow)) {
                            float3 f;
                            float pdf_bsdf;
                            if (is_metal) {
                                f = evaluate_metal(surface, shading_normal, wo, wi, &pdf_bsdf);
                            } else {
                                f = evaluate_diffuse(surface);
                                pdf_bsdf = cos_surface * INV_PI;
                            }
                            float weight = power_heuristic(pdf_sun, pdf_bsdf);
                            radiance += throughput * f * sun_radiance.xyz *
                                        (cos_surface * weight / pdf_sun);
                        }
                    }
                }

                if (!smooth_metal && light_count > 0) {
                    int slot = pick_light(light_cdf, light_count, rnd(&rs));
                    int tri = light_triangles[slot];
                    float u1 = rnd(&rs);
                    float u2 = rnd(&rs);
                    float su = sqrt(u1);
                    float b0 = 1.0f - su;
                    float b1 = u2 * su;
                    float b2 = 1.0f - b0 - b1;
                    float3 a = vertices[tri * 3 + 0].xyz;
                    float3 b = vertices[tri * 3 + 1].xyz;
                    float3 c = vertices[tri * 3 + 2].xyz;
                    float3 point = a * b0 + b * b1 + c * b2;
                    float3 light_normal = normalize(cross(b - a, c - a));
                    float3 to_light = point - hit.position;
                    float distance2 = dot(to_light, to_light);
                    float distance = sqrt(distance2);
                    float3 wi = to_light / distance;
                    float cos_surface = dot(shading_normal, wi);
                    float cos_light = fabs(dot(light_normal, wi));

                    if (cos_surface > 0.0f && cos_light > 1e-6f) {
                        Hit shadow;
                        float3 shadow_origin = hit.position + shading_normal * RAY_EPS;
                        bool blocked = traverse(nodes, vertices, normals, triangle_materials,
                                                triangle_order, shadow_origin, wi,
                                                distance * (1.0f - 1e-3f), true, &shadow);
                        if (!blocked) {
                            Surface light_surface = fetch_surface(materials, triangle_materials[tri]);
                            float pdf_light = distance2 / fmax(cos_light * light_area, 1e-9f);
                            float3 f;
                            float pdf_bsdf;
                            if (is_metal) {
                                f = evaluate_metal(surface, shading_normal, wo, wi, &pdf_bsdf);
                            } else {
                                f = evaluate_diffuse(surface);
                                pdf_bsdf = cos_surface * INV_PI;
                            }
                            float weight = power_heuristic(pdf_light, pdf_bsdf);
                            radiance += throughput * f * light_surface.emission *
                                        (cos_surface * weight / pdf_light);
                        }
                    }
                }

                float3 wi;
                float pdf;
                float3 weight;

                if (is_metal) {
                    float3 h = sample_ggx_half(shading_normal, alpha, &rs);
                    wi = 2.0f * dot(wo, h) * h - wo;
                    if (dot(wi, shading_normal) <= 0.0f) break;
                    float3 f = evaluate_metal(surface, shading_normal, wo, wi, &pdf);
                    if (pdf < 1e-9f) break;
                    weight = f * (dot(shading_normal, wi) / pdf);
                } else {
                    wi = sample_cosine(shading_normal, &rs);
                    float cos_i = dot(shading_normal, wi);
                    if (cos_i <= 0.0f) break;
                    pdf = cos_i * INV_PI;
                    weight = surface.albedo;
                }

                throughput *= weight;
                prev_pdf = pdf;
                specular = smooth_metal;
                dir = wi;
                origin = hit.position + shading_normal * RAY_EPS;
            }

            if (bounce >= 3) {
                float q = fmin(0.95f, fmax(fmax(throughput.x, throughput.y), throughput.z));
                if (rnd(&rs) >= q) break;
                throughput /= q;
            }
        }

        if (radiance_clamp > 0.0f) {
            float peak = fmax(fmax(radiance.x, radiance.y), radiance.z);
            if (peak > radiance_clamp) radiance *= radiance_clamp / peak;
        }
        total += radiance;
    }

    int index = py * width + px;
    float4 previous = accumulator[index];
    accumulator[index] = (float4)(previous.x + total.x, previous.y + total.y, previous.z + total.z,
                                  previous.w + (float)samples);
}
