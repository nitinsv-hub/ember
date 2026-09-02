# ember

A path tracer written in C++20. Ray traversal and shading run as OpenCL kernels;
Vulkan owns the shared framebuffer allocation and the resolve pass. Both APIs are
loaded dynamically at runtime, so there is no SDK to install, no import library
to link, and no third-party dependency of any kind.

![Cornell box](scenes/output/cornell.png)

## Usage

```bash
ember scenes/cornell.scene
```

renders next to the input as `scenes/cornell_output.png`.

```bash
ember scenes
```

renders every scene in the directory into `scenes/output/<name>.png`.

```
--list                    show OpenCL and Vulkan devices
--device N                OpenCL device index
--width N  --height N     override resolution
--samples N               override samples per pixel
--bounces N               override maximum path length
-c N, --cycles N          split the render into N accumulation passes
--exposure F              override tonemap exposure
--clamp F                 clamp per-sample radiance, suppresses fireflies
--hdr                     also write a .pfm alongside the png
--no-vulkan               skip Vulkan entirely, stay on staged copies
--compare A.pfm B.pfm     diff two hdr renders
```

## Build

Windows, no CMake needed:

```bash
build.bat
```

Linux and macOS:

```bash
./build.sh
```

Either platform, with CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

Requires a C++20 compiler and a GPU driver. That is the whole dependency list.

## Renderer

| | |
|---|---|
| Acceleration | binned SAH BVH, 16 bins, surface-area-heuristic leaf termination |
| Traversal | stackless-free explicit stack, slab test, Möller–Trumbore |
| Integrator | unidirectional path tracing, next event estimation, MIS with the power heuristic |
| Materials | Lambertian, GGX metal with Smith masking and Schlick Fresnel, smooth dielectric with total internal reflection |
| Lights | single-sided emissive triangles sampled by area with a CDF, plus a cone-sampled sun |
| Environment | vertical gradient sky |
| Sampling | PCG32, cosine hemisphere, GGX half-vector, uniform cone, unbiased Russian roulette |
| Output | ACES filmic PNG, optional 32-bit PFM |

## Scene format

`.obj` files load directly, with materials from the referenced `.mtl`. The camera
is framed automatically from the bounding box and the scene is lit by a default
sun and sky.

A `.scene` file gives full control. When both `foo.obj` and `foo.scene` exist in a
directory, only the `.scene` is rendered.

```
mesh cornell.obj
camera 278 273 -800  278 273 280  39.3
resolution 640 640
samples 512
bounces 10
cycles 64
exposure 1.0
clamp 40
environment 0 0 0
sky 0.12 0.22 0.50  0.42 0.50 0.66
sun 0.4 0.8 -0.35  2.6 2.45 2.2  1.5
```

`sun` takes a direction, an irradiance, and an angular radius in degrees. The
irradiance is what a surface facing the sun receives, which is far easier to
reason about than the disc's radiance. Unknown directives and wrong argument
counts are reported with a file and line number.

MTL mapping: `Ke` makes an emitter, `Ni` with `d < 1` or `illum 6/7` makes a
dielectric, a bright `Ks` or `illum 3/5` makes a metal, and `Ns` converts to
roughness. Everything else is Lambertian.

## Cycles

A render is accumulated over several passes rather than one long kernel launch.
The engine derives the pass count as `ceil(samples / batch)`, where batch
defaults to 4 samples per pass. `-c N` sets the pass count directly, so each
pass carries `ceil(samples / N)` samples, and the reported line reads
`N cycles of M spp`.

Pass size matters more than it looks. Measured on Iris Xe at 640x640, 256 spp:

| cycles | samples per pass | time |
|---|---|---|
| 8 | 32 | 10.99 s |
| 64 | 4 | 8.59 s |
| 256 | 1 | 8.30 s |

Smaller passes win because a shorter per-thread sample loop lowers register
pressure and lets more work items stay resident. The default of 4 captures most
of that without issuing thousands of dispatches; a discrete GPU with higher
launch overhead may prefer larger passes, so re-measure with `-c` when moving
hardware. Every pass count converges to the same image, but pass size feeds the
RNG seed, so two renders with different `-c` are not bit-identical.

## Interop tiers

The renderer negotiates how OpenCL and Vulkan share the accumulation buffer,
choosing the best tier both drivers actually support:

| Tier | Mechanism | Requires |
|---|---|---|
| 2 | NT handle / fd import | `cl_khr_external_memory_win32` and `cl_khr_semaphore` |
| 1 | shared host allocation | `VK_EXT_external_memory_host` |
| 0 | staged copy | nothing, always available |

Tier 1 allocates the accumulator at the alignment Vulkan reports, imports the
same pages into both APIs, and runs a Vulkan command buffer over the result that
OpenCL wrote. On unified-memory parts this is genuinely zero copy. `--no-vulkan`
forces tier 0, which is also the correctness reference: every tier must produce
identical output.

## Portability

Verified on Intel Iris Xe (Core i7-1360P), OpenCL 3.0 NEO driver 31.0.101.5186,
Vulkan 1.3.271, built with MSVC 14.50 at `/W4` with no warnings.

```
4440 triangles, 2803 bvh nodes, depth 19, built in 4 ms
640x640 at 4096 spp, 10 bounces, 1024 cycles of 4 spp, traced in 139 s
tier 1 (shared host allocation, zero copy)
```

Rules the kernel follows so it runs unmodified elsewhere:

- OpenCL C 1.2 only, built with `-cl-std=CL1.2` and retried without options if a
  driver rejects them. macOS caps out at 1.2, so this is the real floor.
- Compiled from source, never SPIR-V. NVIDIA does not expose `cl_khr_il_program`.
- `clCreateCommandQueue`, not the 2.0 replacement that 1.2 stacks lack.
- No `float3` in host-shared buffers. `Float4` is `static_assert`ed to 16 bytes.
- No `native_*` math and no fast-math, both of which reassociate per vendor.
- No `printf` in kernels.
- Workgroup shape derived from `CL_KERNEL_WORK_GROUP_SIZE`, never assumed.
- Integer-only PCG32, so two conformant devices produce bit-identical images.

The ICD is found at `OpenCL.dll` on Windows, `OpenCL.framework` on macOS, and
`libOpenCL.so.1` on Linux. Vulkan resolves to `vulkan-1.dll`, `libvulkan.1.dylib`
or MoltenVK, and `libvulkan.so.1`. A missing Vulkan loader is not fatal; the
renderer reports it and drops to tier 0. macOS builds define
`CL_SILENCE_DEPRECATION`, since Apple deprecated OpenCL in 10.14 while keeping it
functional.

## Cross-device validation

```bash
ember scene.scene --samples 256 --hdr
ember --compare a.pfm b.pfm
```

Same arguments on two devices must give `identical yes`. The tool exits 2 on a
mismatch so it drops into CI. Batch size feeds the RNG seed, so keep it constant
across the runs being compared.

Run-to-run determinism on one device is verified. Cross-device is not: this
machine exposes a single OpenCL platform.

## Known limitations

- Megakernel, not wavefront. Divergence is unaddressed.
- BVH is built on the CPU. A GPU LBVH build is the natural next step.
- No textures. Materials are constant across a surface.
- The sky is sampled only by BSDF rays, so a bright sky converges slowly.
  The sun is sampled explicitly and does not have this problem.
- Caustics through dielectrics are noisy. `clamp` trades a little bias for a
  large variance reduction.
- Convergence measured against an 8192 spp reference is 2.18x per 4x samples,
  which is the expected 1/sqrt(N). The residual noise concentrates in dark
  purely-indirect regions such as the Cornell ceiling, which the light faces
  away from, and in dielectric caustics. Stratified or low-discrepancy sampling
  is the next real improvement; delaying Russian roulette was measured and made
  quality per second worse, so it was not adopted.
- No hardware ray tracing path yet. `--list` reports whether `VK_KHR_ray_query`
  is available, which is where that will hook in.
