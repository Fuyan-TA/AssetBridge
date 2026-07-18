# Verified textured OBJ fixtures

These small fixtures were created specifically for AssetBridge automated tests.
They may be redistributed with the project under the repository MIT license.

`shared_texture.obj` uses two mesh-referenced materials that intentionally point
to the same PNG. It verifies embedded-image deduplication without merging meshes
or material bindings.
