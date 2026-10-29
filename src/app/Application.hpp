#pragma once

#include "core/Config.hpp"
#include "editor/EditorUI.hpp"
#include "platform/Window.hpp"
#include "renderer/Renderer.hpp"
#include "scene/Scene.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace vr {

class Application {
public:
    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    int run();

private:
    void openModel();
    void saveConfiguration();

    AppConfig config_;
    std::unique_ptr<platform::Window> window_;
    std::unique_ptr<EditorUI> editor_;
    std::unique_ptr<Renderer> renderer_;
    Camera camera_;
    Scene scene_;
    std::optional<std::pair<glm::vec3, glm::vec3>> modelBounds_;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> pendingViewportSize_;
    std::uint32_t lastRequestedViewportWidth_{};
    std::uint32_t lastRequestedViewportHeight_{};
    float lastRequestedRenderScale_{-1.0f};
    float pendingRenderScale_{1.0f};
    std::chrono::steady_clock::time_point viewportRequestChangedAt_{};
    bool viewportResizePending_{};
    double previousCpuMilliseconds_{16.67};
};

} // namespace vr
