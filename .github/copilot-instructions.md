# Copilot Instructions for VulkanPathTracing

## Build System

This project uses **CMake 4.1+** with the **Ninja** generator. C++23 is required. Clang is the primary compiler; MSVC configs exist but are secondary and less tested.

**Generate and build (Debug):**
```bash
cmake -S . -B Build -G Ninja -DCMAKE_BUILD_TYPE=clang_Debug
cmake --build Build
```

**Available build types:**
- `clang_Debug` — unoptimized, sanitizers, assertions enabled
- `clang_Debug_Optimized` — `-O2`, LTO, fast-math, asserts on (`DEBUG_O`, `EN_ENABLE_ASSERTS`)
- `clang_Dist` — `-O3`, LTO, no debug symbols (`DIST`, `NDEBUG`)
- `msc_Debug`, `msc_Debug_Optimized`, `msc_Dist` — MSVC equivalents (less tested)

**Premake5** (optional, for Visual Studio solution generation):
```bash
.\makeProjAndSln.bat        # VS2022
.\makeProjAndSln2026.bat    # VS2026
.\cleanAndMakeProjAndSln.bat
```

There are no tests or linting configurations in this project.

## Architecture

### Application Startup

1. `StartProj/src/main.cpp` — A thin console EXE that detects CPU SIMD support at runtime (SSE4.2 → AVX2 → AVX512), loads the matching `GraphicsProgrammingWithCpp{SSE4_2|AVX2|AVX512}.dll`, and calls `EntryPoint()`.
2. `OOPWithCpp/src/Core/EntryPoint.cpp` — `EntryPoint()` creates an `Application` instance, which owns a `Window` (SDL3) and a `LayerStack`.
3. The `EntryPoint` function supports an application restart loop via a `std::bitset<2>` flag (bit 0 = running, bit 1 = restart).

### Layer System

All application logic lives in **layers** that extend `OWC::Layer` (defined in `Core/Layers/Layer.hpp`). Layers are managed by `LayerStack`, which calls `OnUpdate()`, `ImGuiRender()`, and `OnEvent()` on each active layer in order.

- **Layers** (pushed via `PushLayer`) sit at the front of the stack and receive events first.
- **Overlays** (pushed via `PushOverlay`) sit at the back — used for `ImGuiLayer`.

The main orchestrator is `MainLayer` (`OOPWithCpp/src/MainLayer/`), which instantiates and manages the active rendering mode (CPU ray tracer vs GPU ray tracer).

### Vulkan Renderer Abstraction

`OWC::Graphics` namespace provides a platform-agnostic rendering API (Vulkan-only in practice):

- **`GraphicsContext`** — Abstract base. `VulkanContext` is the sole implementation; handles device creation, swapchain, command buffers, and ImGui initialization.
- **`Renderer`** — Static facade class. All rendering calls go through static methods that delegate to the current `GraphicsContext`.
- **`RenderPassData`** — Abstract representation of a render pass. Factory methods on `Renderer` create typed passes: `GetStaticRenderPass()`, `GetDynamicPass()`, `GetStaticRayTracingPass()`, `GetDynamicRayTracingPass()`. Dynamic passes allow per-frame command buffer rebuilding; static passes pre-record commands.
- **`BaseShader`** — Abstract. `VulkanShader` wraps SPIR-V bytecode. Created via `BaseShader::CreateShader()` (raster) or `BaseShader::CreateRTShader()` (ray tracing). Shaders specify their descriptor bindings via `BindingDescription` structs, which allows automatic descriptor set layout creation.
- **Buffer types** — `UniformBuffer`, `TextureBuffer`, `DynamicTextureBuffer` (per-frame updates), `GeneralBuffer`. All have static `Create*` factory methods.

### CPU Ray Tracer

Located in `OOPWithCpp/src/CPURayTracer/`. Classic path tracer with:
- **RayTracer/** — Core tracing: `Camera`, `Ray`, `Interval`, Hitable hierarchy (`BaseHittable` → `Sphere`, `Hittables` list, `SplitBVH`), Material hierarchy (`BaseMaterial` → `Lambertian`, `Metal`, `Dielectric`, `DefusedLight`), Texture hierarchy (`BaseTexture` → `SolidTexture`, `ImageTexture`)
- **Scenes/** — Each scene class implements `Scene` interface. Selected scene builds the hitable world.
- **AABB/** — Axis-aligned bounding box utilities.

The CPU ray tracer writes pixel data into `InterLayerData::imageData` (a `std::vector<Vec4>`). `CPURayTracerRenderer` (a Layer) uploads this to a `DynamicTextureBuffer` and renders it via a full-screen triangle through a Vulkan raster pass.

### GPU Ray Tracer

Located in `OOPWithCpp/src/GPURayTracer/`. Uses Vulkan ray tracing extensions (`VK_KHR_ray_tracing_pipeline`) with shaders written in **Slang** and pre-compiled to SPIR-V (committed `.spv` files in `ShaderSrc/GPURayTracerShaders/`).

- `GPURayTracerRenderer` — A Layer that sets up the ray tracing pipeline, manages camera, accumulates samples, and dispatches `vkCmdTraceRaysKHR`.
- **TLAS/** — `BaseTLAS` abstract, `VulkanTLAS` builds the top-level acceleration structure with instancing support.
- **Mesh/** — `SceneMesh` abstract, `VulkanSceneMesh` builds BLAS per mesh from GLTF data.
- **Scene/** — `BaseGPUScene` abstract. `SponzaPalace` loads the Sponza GLTF scene via tinygltf and creates BLAS instances.

Shader files use `.slang` extension. SPIR-V outputs are committed alongside sources (.spv files). The ray tracing pipeline uses ray generation, miss, and closest-hit shaders. Data shared between C++ and Slang is defined in `gltfData.h.slang`.

### Event System

Events (`Events/BaseEvent.hpp`) use type-safe dispatch via `EventDispatcher::Dispatch<T>()`. Each event subclass has a static `GetStaticType()` returning its `EventType` enum. Layers override `OnEvent(BaseEvent&)` to handle specific event types. Window events (close, resize, minimize, restore) and keyboard events are currently implemented.

### Key Conventions

- **All code is in the `OWC` namespace.** Rendering abstractions are in `OWC::Graphics`.
- **Type aliases** in `Core/Core.hpp` — `u32`, `i32`, `f32`, `Vec3`, `Vec4`, `Mat4`, `Colour`, `Point`, `Vec2u`, etc. Prefer these over raw GLM types.
- **`OWC_FORCE_INLINE`** macro defined in `Core.hpp` — use for inline functions that must be force-inlined.
- **Factory pattern** for graphics objects — all buffer/shader/pass creation uses static `Create*` methods that return `std::shared_ptr` or `std::unique_ptr`.
- **Abstract base + Vulkan impl** pattern — `GraphicsContext`/`VulkanContext`, `BaseShader`/`VulkanShader`, `BaseTLAS`/`VulkanTLAS`, `SceneMesh`/`VulkanSceneMesh`.
- **Three SIMD library variants** are built from the same source with different compile flags (`SSE4_2`, `AVX2`, `AVX512`). SIMD-specific code uses preprocessor defines (`SSE4_2`, `AVX2`, `AVX512`). GLM uses `GLM_FORCE_SSE42` or `GLM_FORCE_AVX2` accordingly.
- **Build output** goes to `Bin/{Windows|Linux}/{Debug|Debug-Optimized|Dist}/{clang|msc}/`.
- **Resource paths** — DLLs are loaded relative to the executable (`./../GraphicsProgrammingWithCpp/`). Shader SPIR-V files are expected at paths derived from the `ShaderSrc/` tree.
- **`InterLayerData`** is the shared data structure between the CPU ray tracer and its Vulkan renderer — image pixels, sample count, screen size, and update flags.
- **No exceptions in GPU hot paths** — the project defines `TINYGLTF_JSON_NO_EXCEPTIONS` and uses `TINYGLTF3_JSON_SIMD_*` for SIMD-accelerated JSON parsing.
