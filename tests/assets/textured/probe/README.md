# Assimp texture probe fixtures

These minimal OBJ, MTL, PNG, and JPEG files were created specifically for
AssetBridge. They are not downloaded assets.

The 2 x 2 RGB images contain four solid pixels (red, green, blue, and white).
They were generated locally from a 24-bit RGB bitmap. The PNG contains no
alpha or text chunks. The JPEG contains a JFIF header and no EXIF block. The
fixtures intentionally cover same-directory, child-directory, Unicode, and
space-safe companion path behavior.

The probe writes generated GLB files only below the build tree; generated
models are never source-controlled.
