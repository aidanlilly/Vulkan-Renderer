#pragma once

#include "renderer/RendererSettings.hpp"

#include <filesystem>
#include <optional>

namespace vr {

struct AppConfig {
    int windowWidth{1440};
    int windowHeight{900};
    RendererSettings renderer;
    std::optional<std::filesystem::path> recentModel;
};

class ConfigStore {
public:
    static AppConfig load();
    static void save(const AppConfig& config);
    static std::filesystem::path configDirectory();
    static std::filesystem::path imguiIniPath();
};

} // namespace vr
