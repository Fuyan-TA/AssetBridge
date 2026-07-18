# Portfolio Case Study

## Problem

Downloaded 3D assets frequently arrive in formats or structures that do not fit
an Unreal Engine, Maya, or DCC ingestion path. A common workaround is opening a
full DCC application, importing the model, and exporting it again. That adds
startup cost, manual steps, inconsistent settings, and no machine-readable
record of what changed.

AssetBridge explores a narrower Technical Art tooling problem: can one native
desktop utility make a specific OBJ→GLB handoff repeatable, explain why an asset
is rejected, and prove that accepted output remains structurally usable?

## Product Constraints

- Native C++20 Windows x64 application; no Electron, Chromium, Python runtime,
  Qt, Blender, Maya, Unreal Engine, or other installed DCC dependency.
- Core conversion code remains reusable by CLI and independent of GUI code.
- Product support is narrower than Assimp's importer/exporter list.
- v0.1.0 supports only tested static OBJ→GLB2 cases.
- No 3D viewport, batch queue, telemetry, updater, or background service.
- Portable ZIP target below 50 MB, cached launch below one second, and idle
  Working Set below 100 MB.

## Architecture Decisions

The key decision was separating format semantics, product promises, and runtime
library capability. A route registry allows OBJ→GLB2 to be enabled without
claiming that every Assimp exporter is an AssetBridge feature. The GUI calls
Core directly on a worker thread. Output uses a temporary sibling directory and
is committed only after GLB reimport and validation.

Windows paths remain UTF-16/`std::filesystem::path` until an explicit UTF-8
boundary. This made Chinese OBJ, MTL, output paths, and JSON part of the tested
contract rather than an incidental locale behavior.

## Failure Cases Discovered

Development exposed failure modes that a simple exporter wrapper would miss:

- Assimp's generated root and flat OBJ wrapper nodes initially looked like a
  hierarchy; the semantic test had to distinguish wrappers from meaningful
  nesting, transforms, and instancing.
- `mTicksPerSecond == 0` makes animation seconds unknown; treating it as a
  divisor would create NaN or infinity.
- Vertex counts can change legally across representations, so strict equality
  is not a reliable geometry contract.
- A quad-only OBJ reports zero source triangle faces even though it contains
  geometry. Export-ready triangle diagnostics were added after controlled
  triangulation.
- Multiple mesh order cannot be assumed stable after GLB export/reimport.
  Matching now uses geometry evidence, names when useful, and a deterministic
  tie-break.
- A portable package can compile successfully while missing transitive Assimp
  or MSVC runtime DLLs. Release staging now resolves runtime dependencies and
  tests the staged executables before packaging.

## Real-world Multi-Mesh Test

An anonymous local acceptance asset was used after the self-authored regression
assets passed:

- 29 meshes
- 87,428 source faces
- 174,856 export-ready triangles
- Approximately 5.27 MB GLB
- Approximately 461 ms Release conversion
- 18/18 validation checks passed

**Local real-world acceptance asset; not redistributed due to unknown
redistribution rights.** No asset file, screenshot, source identity, or private
path is included in the repository.

## Verification Strategy

The automated suite covers pure product rules, scene semantics, structured JSON,
Unicode CLI boundaries, transaction cleanup, Core conversion, desktop state,
DX11 smoke, DPI smoke, and desktop acceptance. Generated GLB files stay in
ignored build/test directories. Self-authored OBJ/MTL fixtures cover single and
multiple meshes, a quad, Chinese filenames, missing or invalid geometry, and an
external-texture rejection.

Conversion evidence compares the export-ready scene with the reimported GLB:
mesh count, per-mesh and total triangles, index validity, finite data, scene and
per-mesh AABBs, normals, UV0, referenced material presence, and diffuse color.

## v0.2 Textured-Asset Extension

OBJ texture support required a different reliability boundary from geometry.
An MTL can point at files outside the asset directory, use unsupported map
options, or describe texture semantics that the current route does not promise.
Assimp 6.0.4 also does not automatically turn imported external `map_Kd`
references into GLB-embedded images. Treating exporter success as proof of a
self-contained asset would therefore be incorrect.

The v0.2 development path resolves only mesh-used materials, restricts MTL and
image access to the OBJ directory tree, validates PNG/JPEG signatures and
resource limits, and blocks transparency or non-Base-Color semantics. Core then
creates compressed `aiTexture` payloads explicitly and rewrites material
references before export. The GLB validator verifies bufferView embedding,
material-to-image binding, shared-image deduplication, and byte identity. The
existing geometry and transaction checks remain in place.

This expands one route, not the general format matrix. Normal, bump, opacity,
metallic, roughness, specular, emissive, transformed, or transcoded textures
remain unsupported until separate product rules and validation evidence exist.

## Development Method

The implementation was developed with AI-assisted coding under a human-defined
scope, architecture, acceptance criteria, and release process. Capability claims
are based on committed fixtures, retained Assimp behavior probes, executable
validation, and CI logs rather than on generated descriptions. This record does
not imply a team size or manual implementation process that did not exist.

## Lightweight Results

Measured on an AMD Ryzen 5 9600X system with Windows 11 Pro 10.0.26200:

| Evidence | Result |
|---|---:|
| Desktop EXE | 745,984 bytes |
| CLI EXE | 376,320 bytes |
| Portable directory | 10,363,188 bytes |
| ZIP | 4,347,762 bytes |
| First measured responsive window | 215.983 ms |
| Five cached launches, median | 183.421 ms |
| Five idle Working Set samples, median | 68.57 MiB |

No system cache or security setting was manipulated for the measurements. The
first measured run is recorded separately; the cached result is the median of
five subsequent launches. The v0.1.0 package includes the MSVC runtime and all
redistributed license texts rather than reducing size by omitting dependencies.

## What I Learned

The strongest TA value in this project is not calling an exporter. It is
turning an ambiguous content handoff into a bounded product contract with
diagnostics, rollback, evidence, and art-facing feedback. Format support must be
route-specific. Scene statistics need semantic definitions. Unicode and package
dependencies must be tested at executable boundaries. A small tool becomes
credible only when failure behavior is as deliberate as the success path.

## Next Steps

Future work remains gated by new capability rules and test assets rather than
being implied as current support. Candidate investigations include additional
texture semantics, Authenticode signing, and additional source formats.
Each would require its own route commitment, loss rules, fixtures, round-trip
checks, UI diagnostics, package evidence, and size/performance review before it
could be marked enabled or verified.
