# Assimp 6.0.4 OBJ texture behavior

This record is backed by `assetbridge-assimp-texture-probe`. The probe uses
only self-made 2 x 2 RGB PNG/JPEG fixtures and writes generated GLB files below
the build tree.

## Import behavior

- OBJ `map_Kd` imports as exactly one `aiTextureType_DIFFUSE` reference.
- For an MTL beside the OBJ, `aiMaterial::GetTexture` returns the relative text
  path, for example `textures/probe_rgb.png`.
- For an MTL in `materials/`, Assimp prefixes the MTL directory but does not
  normalize dot segments. A source value of `../textures/probe_rgb.png` is
  returned as `materials/../textures/probe_rgb.png`.
- Unicode MTL and texture names are preserved as UTF-8 at the Assimp boundary.
  The probe observes `材质/../贴图/颜色.png`.
- A two-material OBJ is imported as two meshes. Each mesh references the
  corresponding imported material index.

These observations mean AssetBridge must not resolve `map_Kd` relative to the
OBJ directory by guesswork. It must parse and validate the actual companion
chain and normalize the returned MTL-prefixed path inside the asset root.

## Direct GLB export behavior

Passing the imported OBJ scene directly to the Assimp `glb2` exporter does not
embed external PNG/JPEG files. The generated GLB JSON contains image `uri`
members and no image `bufferView` members. Reimport does not produce embedded
`aiScene::mTextures` entries.

This implicit exporter behavior is not sufficient for AssetBridge's verified
single-file GLB promise.

## Explicit embedding behavior

When the export-ready scene contains compressed `aiTexture` entries and the
material texture paths are rewritten to `*N`:

- GLB images use BIN-backed `bufferView` entries and have no external URI.
- PNG and JPEG MIME types are emitted from the `aiTexture` format hint.
- The source compressed PNG/JPEG bytes are preserved exactly in the image
  buffer views; no image decode or re-encode occurs in this path.
- GLB reimport exposes the images through `aiScene::mTextures`.
- GLB material textures reimport as `aiTextureType_BASE_COLOR`, not the OBJ
  importer's `aiTextureType_DIFFUSE` classification.
- Multiple materials referencing the same `*N` path are represented by one
  embedded image/texture record.

AssetBridge therefore uses an explicit, validated texture embedding plan. It
does not rely on the exporter to discover or read arbitrary external paths.

## Product consequence

Exporter availability only proves that a GLB writer exists at runtime. It does
not prove that external OBJ companion files are safely resolved, embedded, or
validated. Product support is enabled only after the resolver, embedding plan,
GLB container checks, reimport checks, and route tests all pass.
