#include "Shader.h"
#include "Loader/ShaderLoader.h"

namespace Quasar {
b8 Shader::create(const String& config_file) // TODO: config file use
{
    _config.stage_count = 2;

    _config.stages[0].stage = ShaderStage::SHADER_STAGE_VERTEX;
    _config.stages[0].file_path = "Shaders/Builtin.World.vert.spv";
    {
        ShaderLoader loader;
        if (!loader.load(_config.stages[0].file_path)) {
            LOG_ERROR("Failed to load shader: %s", _config.stages[0].file_path.c_str());
            return false;
        }
        _config.stages[0].shader_code = loader.get_shadercode();
        loader.unload();
    }
    _config.stages[0].entry_point = "main";

    _config.stages[1].stage = ShaderStage::SHADER_STAGE_FRAGMENT;
    _config.stages[1].file_path = "Shaders/Builtin.World.frag.spv";
    {
        ShaderLoader loader;
        if (!loader.load(_config.stages[1].file_path)) {
            LOG_ERROR("Failed to load shader: %s", _config.stages[1].file_path.c_str());
            return false;
        }
        _config.stages[1].shader_code = loader.get_shadercode();
        loader.unload();
    }
    _config.stages[1].entry_point = "main";

    if (!QS_RENDERER.shader_create(_config, this)) {
        LOG_ERROR("Failed to create shader!");
        return false;
    }

    return true;
}

void Shader::destroy()
{

}
}