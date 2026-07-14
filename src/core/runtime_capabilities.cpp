#include "assetbridge/core/runtime_capabilities.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/importerdesc.h>
#include <assimp/version.h>

namespace assetbridge {
namespace {

std::string normalize_extension(std::string extension) {
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });

    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }

    return extension;
}

FormatAvailability make_input_availability(std::string extension) {
    extension = normalize_extension(std::move(extension));
    const bool is_verified_obj = extension == ".obj";
    return {
        std::move(extension),
        true,
        is_verified_obj,
        is_verified_obj
    };
}

} // namespace

RuntimeCapabilities query_runtime_capabilities() {
    RuntimeCapabilities capabilities;
    capabilities.assimp_version =
        std::to_string(aiGetVersionMajor()) + "."
        + std::to_string(aiGetVersionMinor()) + "."
        + std::to_string(aiGetVersionPatch());

    Assimp::Importer importer;
    const std::size_t importer_count = importer.GetImporterCount();
    capabilities.importers.reserve(importer_count);
    for (std::size_t index = 0; index < importer_count; ++index) {
        const aiImporterDesc* description = importer.GetImporterInfo(index);
        if (description == nullptr) {
            continue;
        }

        ImporterCapability capability;
        capability.name = description->mName == nullptr ? "" : description->mName;

        std::istringstream extensions(
            description->mFileExtensions == nullptr ? "" : description->mFileExtensions);
        std::string extension;
        while (extensions >> extension) {
            capability.formats.push_back(make_input_availability(std::move(extension)));
        }

        capabilities.importers.push_back(std::move(capability));
    }

    Assimp::Exporter exporter;
    const std::size_t exporter_count = exporter.GetExportFormatCount();
    capabilities.exporters.reserve(exporter_count);
    for (std::size_t index = 0; index < exporter_count; ++index) {
        const aiExportFormatDesc* description = exporter.GetExportFormatDescription(index);
        if (description == nullptr) {
            continue;
        }

        capabilities.exporters.push_back({
            description->id == nullptr ? "" : description->id,
            description->description == nullptr ? "" : description->description,
            normalize_extension(
                description->fileExtension == nullptr ? "" : description->fileExtension),
            true,
            false,
            false
        });
    }

    return capabilities;
}

} // namespace assetbridge
