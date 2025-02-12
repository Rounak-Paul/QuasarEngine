#pragma once

#include <qspch.h>

namespace Quasar
{
#define MAX_SHADER_STAGES 5

typedef enum ShaderStage {
    SHADER_STAGE_VERTEX,
    SHADER_STAGE_FRAGMENT,
    SHADER_STAGE_GEOMETRY,
    SHADER_STAGE_TESSELLATION_CONTROL,
    SHADER_STAGE_TESSELLATION_EVALUATION
} ShaderStage;

typedef struct ShaderStageInfo {
    ShaderStage stage;
    String file_path;
    DynamicArray<u32> shader_code;
    String entry_point;
} ShaderStageInfo;

typedef struct ShaderConfig {
    u8 stage_count; // number of active stages
    ShaderStageInfo stages[MAX_SHADER_STAGES];
} ShaderConfig;

class Shader {
    public:
    Shader() = default;
    ~Shader() = default;
    b8 create(const String& config_file);
    void destroy();


    ShaderConfig _config;

    /**
     * @brief Renderer specific Shader data
     * 
     * Vulkan: VulkanShader
     */
    void* _internal_data;
};
} // namespace Quasar
