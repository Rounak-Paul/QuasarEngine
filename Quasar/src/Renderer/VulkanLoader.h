#pragma once

#include <qspch.h>
#include "VulkanTypes.h"

#include <stb_image.h>
#include <filesystem>

namespace Quasar
{
struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
};

struct MeshAsset {
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};

class Renderer;

std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(Renderer* engine, std::filesystem::path filePath);
} // namespace Quasar
