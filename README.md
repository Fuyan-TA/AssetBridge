# AssetBridge

[中文说明 / Chinese documentation](README.zh-CN.md)

AssetBridge is a lightweight native Windows tool for inspecting 3D assets and
performing a narrow, validated OBJ-to-GLB conversion workflow. It is designed
around explicit format capabilities, preflight diagnostics, transactional
output, and round-trip verification.

AssetBridge v0.1.0 is not a general-purpose all-format converter and makes no
readiness claim beyond its verified boundary. The product exposes only the
OBJ-to-GLB route that has automated and local acceptance evidence.

![AssetBridge v0.1.0 desktop conversion](docs/images/assetbridge-v0.1.0.png)

Local file-system paths are redacted in documentation screenshots.

## v0.2 development status

The current development branch extends only the verified OBJ-to-GLB2 route.
It resolves mesh-referenced MTL materials and local `map_Kd` images, explicitly
embeds PNG/JPEG compressed bytes in the GLB BIN chunk, and validates the GLB
container before the existing Assimp reimport and geometry checks. Multiple
referenced materials, per-material Kd colors, distinct images, and shared-image
deduplication are covered by self-authored fixtures.

Companion files are restricted to the OBJ directory tree. Absolute paths, UNC
paths, URLs, normalized `../` escapes, missing files, non-regular files,
unsupported extensions, signature mismatches, transparency, map options, and
non-Base-Color texture semantics produce stable blocking diagnostics. This
scope does not include normal, bump, opacity, metallic, roughness, specular, or
emissive textures; image transcoding; texture transforms; or alpha semantics.

This is unreleased development work. The published v0.1.0 package and tag are
unchanged.

## v0.1.0 scope

The verified product scope is:

- Windows x64 native desktop and command-line applications.
- OBJ and MTL input, with GLB output.
- One static mesh or multiple non-empty static meshes represented by flat,
  identity sibling nodes.
- Vertex positions, normals, UV0, and the currently verified single referenced
  material with MTL diffuse color.
- Controlled triangulation of non-triangle faces during export preparation.
- UTF-16 Windows command-line paths and UTF-8 JSON, including Chinese OBJ, MTL,
  and output paths.
- GUI drag-and-drop, inspection, preflight diagnostics, and conversion.
- Transactional per-asset output, GLB reimport, validation checks, and a JSON
  conversion report.

The following remain outside the verified v0.1.0 boundary and are rejected when
detected:

- External or embedded textures.
- Multiple referenced-material semantics beyond the current single-material
  commitment.
- Meaningful node hierarchy, non-identity node transforms, or mesh instancing.
- Multiple UV channels, vertex colors, tangents, or unverified PBR data.
- Bones, skin weights, animation, or morph targets.
- Other input or output formats, even when the bundled Assimp build exposes a
  corresponding importer or exporter.

## Quick start

### Desktop

1. Extract the complete Windows x64 ZIP; keep the EXE and DLL files together.
2. Run `assetbridge-desktop.exe`.
3. Drop one OBJ file into the window or use **Choose OBJ**.
4. Review the asset summary and preflight result.
5. Choose an output directory and select **Convert to GLB**.
6. After successful reimport validation, open the output folder or JSON report.

The MVP accepts one OBJ at a time. Dropping multiple files or a non-OBJ file is
rejected before conversion.

### CLI

```powershell
assetbridge-cli --version
assetbridge-cli inspect .\model.obj
assetbridge-cli inspect .\model.obj --json
assetbridge-cli preflight .\model.obj --target glb --json
assetbridge-cli convert .\model.obj --to glb --output .\converted --json
assetbridge-cli capabilities --json
```

`capabilities` separates three facts: what the current Assimp runtime exposes,
what AssetBridge enables as a product, and what AssetBridge has verified with
tests. Runtime availability alone is not a support claim.

Successful conversion creates a non-overwriting per-asset directory:

```text
<output-root>/
└── <source-stem>/
    ├── <source-stem>.glb
    └── conversion-report.json
```

If the final directory exists, AssetBridge selects `<source-stem>_2`, `_3`, and
so on. Export and validation happen in a temporary sibling directory. The final
directory appears only after every validation check passes.

CLI exit codes are stable:

| Code | Meaning |
|---:|---|
| 0 | Command completed successfully. |
| 1 | File, import, preflight, export, or validation error. |
| 2 | Invalid command, arguments, or target format. |

## Build from source

Prerequisites:

- Windows 11 or a compatible Windows x64 development environment.
- Visual Studio 2022 Build Tools with MSVC v143 and a Windows SDK.
- CMake 3.25 or later and Ninja.
- vcpkg, with `VCPKG_ROOT` set to its installation directory.

Run these commands from an x64 Visual Studio Developer PowerShell:

```powershell
$env:VCPKG_ROOT = "<vcpkg-root>"

cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug

cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
```

Create the portable Release package and SHA-256 file with:

```powershell
cmake --build --preset msvc-release --target package
```

Outputs are written below `out/build/msvc-release/packages/`. CMake project
version `0.1.0` is the authoritative version source; a configured header feeds
the CLI and desktop UI.

## Validation model

The conversion path inspects the original asset, runs product preflight, builds
an export-ready triangulated scene, exports GLB, reimports it, and compares the
result before committing output. Mesh count, triangle count, index validity,
whole-scene and per-mesh AABBs, normal/UV0 presence, and the verified material
data are checked. Vertex count is diagnostic because legal triangulation and
format representation can change vertex splitting.

See [Architecture](docs/ARCHITECTURE.md) and
[Validation Pipeline](docs/VALIDATION_PIPELINE.md) for the engineering details.

## Lightweight metrics

These are measurements, not cross-machine guarantees. No system cache was
cleared and no Windows security policy was changed.

| Metric | v0.1.0 measurement |
|---|---:|
| Desktop EXE | 745,984 bytes |
| CLI EXE | 376,320 bytes |
| Portable directory | 10,363,188 bytes |
| ZIP | 4,347,762 bytes |
| First measured launch to responsive window | 215.983 ms |
| Cached launch median, 5 runs | 183.421 ms |
| Idle Working Set median, 5 runs | 71,897,088 bytes (68.57 MiB) |

Measurement machine: AMD Ryzen 5 9600X, 12 logical processors, Windows 11 Pro
10.0.26200. Launch timing is measured from process creation until a responsive
main window exists. Idle Working Set is sampled 1.5 seconds after that point
with no asset loaded. The source state is the `release/v0.1.0` preparation tree;
the commit containing this table is the release evidence commit, based on merge
commit `3647bddee68eede93f36b00bc78dd3fbafd5e6d5`.

The complete DLL and validation evidence is summarized in the
[portfolio case study](docs/PORTFOLIO_CASE_STUDY.md).

## Unsigned Windows build

The v0.1.0 portable preview is not Authenticode-signed. Some Windows security
policies may block unsigned executables. AssetBridge does not recommend turning
off or bypassing Smart App Control, antivirus software, or organizational code
integrity policy. If execution is blocked, inspect the source and build it
locally, or wait for a future signed build.

The accompanying SHA-256 file verifies archive integrity only. It is not an
identity signature and does not replace code signing.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Validation Pipeline](docs/VALIDATION_PIPELINE.md)
- [Portfolio Case Study](docs/PORTFOLIO_CASE_STUDY.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

## License

AssetBridge is licensed under the [MIT License](LICENSE). Third-party libraries
and redistributed runtime dependencies are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), with license texts in
`third_party/licenses/`.
