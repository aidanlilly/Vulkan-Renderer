#pragma once

#include "core/Log.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/RendererSettings.hpp"
#include "scene/Camera.hpp"

#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace vr {

struct EditorActions {
    bool openModel{};
    bool resetCamera{};
    bool frameModel{};
    bool clearLog{};
    std::uint32_t viewportWidth{};
    std::uint32_t viewportHeight{};
};

class EditorUI {
public:
    EditorUI(GLFWwindow* window, const std::filesystem::path& iniPath);
    ~EditorUI();
    EditorUI(const EditorUI&) = delete;
    EditorUI& operator=(const EditorUI&) = delete;

    void beginFrame();
    EditorActions draw(std::uintptr_t viewportTexture, const std::string& modelName, bool modelLoaded,
        RendererSettings& settings, Camera& camera, const std::vector<LogEntry>& logEntries,
        const std::string& device, const std::string& presentMode, const RendererStats& stats, double cpuMilliseconds);
    [[nodiscard]] void* drawData() const;
    void setNotification(std::string message, LogLevel level = LogLevel::Info);

private:
    GLFWwindow* window_{};
    std::string iniPath_;
    std::string notification_;
    LogLevel notificationLevel_{LogLevel::Info};
    float notificationLifetime_{};
    bool initializeLayout_{};
    bool orbitDragging_{};
    bool panDragging_{};
};

} // namespace vr
