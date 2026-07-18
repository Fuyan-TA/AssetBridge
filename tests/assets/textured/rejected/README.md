# Rejected textured OBJ fixtures

These text-only fixtures were created for AssetBridge and are redistributed
under the repository MIT license. They intentionally describe unsafe or
unsupported companion-file behavior; no external model or texture is included.

- `path_traversal` verifies that a `map_Kd` path cannot escape the OBJ directory.
- `unsupported_semantic` verifies that bump/normal texture semantics remain
  outside the verified Base Color-only route.
