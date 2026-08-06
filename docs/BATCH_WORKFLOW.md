# Batch workflow contract

AssetBridge v0.3 adds orchestration around the existing verified OBJ-to-GLB2
conversion route. It does not add a new asset-format capability. The batch
layer is independent of Dear ImGui, SDL3, DirectX, Assimp, and the Windows file
dialog. A caller injects one executor and receives immutable job-state copies.

## State model

Every job has a monotonically allocated ID formatted as `job-000001`. A list
index is never used as persistent identity. The stable states are:

`queued`, `inspecting`, `preflighting`, `resolving_textures`, `converting`,
`embedding_textures`, `validating_geometry`, `validating_textures`, `success`,
`not_supported`, `failed`, and `canceled`.

Only one executor invocation may be active. Failure of one job does not stop
later jobs. `Cancel After Current` lets the active executor finish its
transaction and marks jobs that have not started as `canceled`. It never
terminates the worker thread.

## `assetbridge.batch.v1`

The batch report is a root-level summary. Existing per-asset
`conversion-report.json` files remain `assetbridge.conversion.v1` and are not
embedded into the root report.

```json
{
  "schema": "assetbridge.batch.v1",
  "status": "success",
  "target_format": "glb2",
  "output_root": "converted",
  "summary": {
    "total": 1,
    "succeeded": 1,
    "not_supported": 0,
    "failed": 0,
    "canceled": 0
  },
  "jobs": [
    {
      "id": "job-000001",
      "input_path": "asset.obj",
      "status": "success",
      "route": {
        "source_format_id": "obj",
        "target_format_id": "glb2"
      },
      "output": {
        "directory": "converted/asset",
        "glb": "converted/asset/asset.glb",
        "conversion_report": "converted/asset/conversion-report.json"
      },
      "error": null,
      "duration_ms": 1.0,
      "diagnostics": {
        "triangle_count": 1,
        "material_count": 1,
        "embedded_image_count": 0,
        "validation_failure_count": 0
      }
    }
  ]
}
```

Batch status is `success` only when every job succeeds. `partial` means at
least one success and at least one non-success. `failed` means no job succeeded
and at least one job failed or was not supported. `canceled` means no job
succeeded or failed and at least one queued job was canceled.

Successful jobs use `error: null`. Non-success jobs use an object with stable
`code` and human-readable `message`; output paths that do not exist are `null`.
All numeric fields are finite JSON numbers. JSON mode reserves stdout for one
JSON document and writes diagnostics to stderr.
