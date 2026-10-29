#include "renderer/shaders/ShaderLibrary.hpp"

#include "core/Log.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace vr {

ShaderLibrary::ShaderLibrary(std::filesystem::path directory) : directory_(std::move(directory)) {}

std::vector<std::uint32_t> ShaderLibrary::loadSpirv(const std::string& name) const {
    const auto path = directory_ / name;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        Logger::instance().error("Shader", "Could not open SPIR-V shader " + path.string() + ".");
        throw std::runtime_error("Could not open SPIR-V shader: " + path.string());
    }
    const auto byteCount = static_cast<std::streamoff>(file.tellg());
    if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        Logger::instance().error("Shader", "SPIR-V shader has an invalid byte size: " + path.string());
        throw std::runtime_error("SPIR-V shader has an invalid byte size: " + path.string());
    }
    std::vector<std::uint32_t> code(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byteCount));
    if (!file || code.empty() || code.front() != 0x07230203u) {
        Logger::instance().error("Shader", "SPIR-V shader failed header validation: " + path.string());
        throw std::runtime_error("SPIR-V shader failed header validation: " + path.string());
    }
    return code;
}

} // namespace vr
