#pragma once

struct aiNode;

namespace assetbridge {

// Reports hierarchy only when the imported node tree carries semantics that a
// flat mesh list cannot express: nesting below a non-root node, a non-identity
// transform, or repeated references to the same mesh (instancing). Root-level
// identity wrapper nodes and flat sibling mesh nodes are intentionally ignored.
[[nodiscard]] bool has_meaningful_node_hierarchy(const aiNode* root) noexcept;

} // namespace assetbridge
