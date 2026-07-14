# Contributing to AssetBridge

AssetBridge accepts changes that preserve explicit product boundaries and add
evidence with implementation. Opening an Assimp importer or exporter is not, by
itself, sufficient to claim a supported route.

## Development setup

Use Windows x64, MSVC v143, CMake 3.25 or later, Ninja, and the repository vcpkg
manifest. Set `VCPKG_ROOT` and build through the checked-in presets:

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Before submitting a release-related change, repeat the Release build and tests
and run the package target:

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
cmake --build --preset msvc-release --target package
```

## Product and architecture rules

- Keep `assetbridge-product` independent of Assimp scenes, CLI, and GUI code.
- Keep `assetbridge-core` independent of SDL, ImGui, DirectX, and Windows GUI
  headers.
- Add conversion support through route-level product data and tests.
- Do not infer product support from `runtime_available`.
- Preserve existing JSON schema fields and semantics; discuss versioned schema
  changes before implementation.
- The desktop must call Core directly and keep long-running work off the UI
  thread.

## Tests and assets

- Add pure unit tests for capability/loss rules and executable-level tests for
  CLI or Unicode boundaries.
- Add round-trip validation for every newly promised attribute.
- Keep generated GLB, ZIP, logs, and temporary directories under ignored build
  output.
- Commit only self-authored assets or assets with explicit redistribution terms.
  Document the source and license for any third-party fixture.
- Never add local acceptance assets with unknown redistribution rights.

CTest labels identify `unit`, `cli`, `desktop-state`, `gui`, `acceptance`, and
`package` coverage. GitHub-hosted Windows runners execute non-GUI tests with
`-LE gui`. DX11 window/present tests and desktop conversion acceptance require
the documented local Release run; exclusion in CI is not recorded as a pass.

## Pull request checklist

- State the exact product capability change, or state that capability is
  unchanged.
- Include tests for success, rejection, Unicode, cleanup, and report structure
  where relevant.
- Run Debug and Release CTest from a clean build directory.
- Confirm no local absolute path, private asset, generated model, or temporary
  directory is tracked.
- Update the capability documentation and third-party notices when applicable.
- Report any Code Integrity-blocked test as not executed; do not bypass Windows
  security policy.
