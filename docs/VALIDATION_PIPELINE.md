# Validation Pipeline

AssetBridge treats conversion as a transaction with evidence, not as a file
extension change or a successful exporter return code.

```mermaid
flowchart LR
    Inspect["Inspect original OBJ"] --> Resolve["Resolve MTL and map_Kd<br/>inside asset root"]
    Resolve --> Preflight["Capability and route preflight"]
    Preflight --> Ready["Build export-ready scene<br/>triangulate + embed textures"]
    Ready --> Export["Assimp GLB2 export"]
    Export --> Container["Validate GLB JSON/BIN<br/>and embedded bytes"]
    Container --> Reimport["Reimport generated GLB"]
    Reimport --> Compare["Geometry and attribute validation"]
    Compare -->|pass| Commit["Write report and atomically commit directory"]
    Compare -->|fail| Rollback["Delete temporary directory"]
```

## 1. Original asset inspection

The input remains a `std::filesystem::path` at the Windows UTF-16 boundary.
Inspection verifies that the file exists, imports it through Assimp, and records
the source summary and actually present features. Preflight does not report
loss for features the source does not contain.

## 2. Capability and route preflight

The product registry evaluates the canonical OBJ→GLB2 route independently from
Assimp runtime enumeration. A route is convertible only when it is enabled and
verified and every detected source feature is inside the tested commitment.
Unsupported or unverified features produce stable diagnostic codes before an
output transaction begins.

For textured OBJ input, Core supplies a pure evidence flag only after companion
analysis completed. Product policy can then distinguish a verified local
Base-Color reference from an arbitrary external-texture feature. Resolver
issues are appended as exact blocking codes, so a missing file is not collapsed
into a generic unsupported-feature message.

## 3. Export-ready source

The raw OBJ summary is not used as the expected output geometry. The conversion
import applies only the declared preparation steps:

- `aiProcess_Triangulate`
- `aiProcess_ValidateDataStructure`

It does not generate normals or tangents, merge meshes, optimize meshes, change
coordinate handedness, flip UVs, or change units. A validation snapshot is
created from the exact scene about to be exported.

### Source triangles versus export-ready triangles

`source_triangle_face_count` counts faces that were already triangles in the
original OBJ. A quad is one source face and zero source triangle faces. After
controlled triangulation, that quad contributes two export-ready triangles.
Reports preserve both values so a quad-only model is not misrepresented as
having no geometry.

## 4. GLB export and reimport

Before export, each unique safe PNG/JPEG payload becomes a compressed
`aiTexture`; materials reference it through `*N`. Shared source paths map to one
embedded image. Assimp's `glb2` exporter writes the candidate file inside the
temporary asset directory.

AssetBridge parses the GLB v2 header and chunks directly. It rejects external
image URIs and local absolute paths, validates all bufferView bounds, confirms
material/texture/image indices, checks PNG/JPEG signatures, and compares the
embedded compressed bytes with the resolved source bytes. AssetBridge then
imports the generated GLB as a new scene; successful export or container parsing
alone is not enough.

## 5. Validation checks

The reimported scene is checked for:

- Non-empty mesh count exactly matching the export-ready source.
- No silent collapse of independent meshes.
- Valid three-index faces and in-range indices.
- Per-mesh and total triangle counts.
- Finite vertex, normal, UV, and transform values.
- Whole-scene AABB center and size within defined tolerance.
- Per-mesh AABB, normal presence, and UV0 presence.
- Referenced material existence and the currently verified diffuse color.
- Per-geometry-matched mesh material name, Kd color, and Base Color texture
  presence.
- The exact set of mesh-used material names and their embedded image bindings.

Mesh order is not assumed stable. Matching uses triangle count and world-space
AABB, prefers exact non-empty names, and uses the lowest unmatched output
ordinal as a deterministic tie-break for duplicate or empty names. The report
records the strategy.

## Why vertex count is diagnostic

OBJ and GLB can represent attribute seams differently. Legal triangulation or
an exporter may split vertices at UV, normal, or material boundaries without
changing the rendered surface. Requiring strict vertex equality would create
false failures and encourage unsafe mesh welding. AssetBridge records source
and output vertex counts, but its geometric contract uses topology validity,
triangle counts, bounds, and required attribute presence.

## 6. Atomic commit

After every check passes, AssetBridge writes `conversion-report.json` and
renames the temporary directory to the selected final directory. Validation or
filesystem failure rolls back the temporary directory. Tests assert that
successful and failed conversions leave no `.assetbridge-tmp-*` residue.
