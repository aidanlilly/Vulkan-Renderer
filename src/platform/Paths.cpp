#include "platform/Paths.hpp"

#include <windows.h>

#include <array>
#include <filesystem>

namespace vr::platform {

std::filesystem::path shaderDirectory() {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length > 0 && length < static_cast<DWORD>(executable.size())) {
        const auto installedDirectory = std::filesystem::path(executable.data()).parent_path() / "shaders";
        if (std::filesystem::exists(installedDirectory)) return installedDirectory;
    }
    return std::filesystem::path(VULKAN_RENDERER_BUILD_SHADER_DIR);
}

} // namespace vr::platform
