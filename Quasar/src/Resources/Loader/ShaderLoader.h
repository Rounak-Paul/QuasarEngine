#pragma once

#include "Loader.h"
#include <Platform/File.h>

namespace Quasar {

class ShaderLoader : public Loader {
private:
    DynamicArray<uint32_t> shaderCode;
    String filePath;

public:
    ShaderLoader() {
        unload();
    }
    b8 load(const String& path) override {
        filePath = path;
        File f;
        if (!f.open(path, File::Mode::READ, File::Type::BINARY)) {
            LOG_ERROR("Failed to open shader file: %s" , path.c_str());
            return false;
        }

        u32 fileSize = f.get_size();
        if (fileSize % sizeof(u32) != 0) {
            LOG_ERROR("Invalid SPIR-V file size: %s" , path.c_str());
            return false;
        }

        shaderCode.resize(fileSize / sizeof(u32));
        if (!f.read_all_binary(reinterpret_cast<u8*>(shaderCode.get_data()))) {
            LOG_ERROR("Failed to read shader file: %s" , path.c_str());
            return false;
        }
        f.close();

        return true;
    }

    void unload() override {
        shaderCode.clear();
    }

    const DynamicArray<u32>& get_shadercode() const {
        return shaderCode;
    }
};

} // namespace Quasar