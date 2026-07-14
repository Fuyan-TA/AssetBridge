#pragma once

#include <string>
#include <vector>

namespace assetbridge {

struct FormatAvailability {
    std::string extension;
    bool runtime_available = false;
    bool product_enabled = false;
    bool verified = false;
};

struct ImporterCapability {
    std::string name;
    std::vector<FormatAvailability> formats;
};

struct ExporterCapability {
    std::string id;
    std::string description;
    std::string extension;
    bool runtime_available = false;
    bool product_enabled = false;
    bool verified = false;
};

struct RuntimeCapabilities {
    std::string assimp_version;
    std::vector<ImporterCapability> importers;
    std::vector<ExporterCapability> exporters;
};

[[nodiscard]] RuntimeCapabilities query_runtime_capabilities();

} // namespace assetbridge
