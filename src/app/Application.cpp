#include "app/Application.hpp"

#include "assets/ModelImporter.hpp"
#include "core/Log.hpp"
#include "platform/FileDialog.hpp"
#include "platform/Paths.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <exception>
#include <utility>

namespace vr {

Application::Application() {
    config_ = ConfigStore::load();
    window_ = std::make_unique<platform::Window>(config_.windowWidth, config_.windowHeight, "Vulkan Renderer");
    editor_ = std::make_unique<EditorUI>(window_->native(), ConfigStore::imguiIniPath());
#ifdef NDEBUG
    constexpr bool validation = false;
#else
    constexpr bool validation = true;
#endif
    renderer_ = std::make_unique<Renderer>(window_->native(), platform::shaderDirectory(), validation, config_.renderer.vsync);
    camera_.reset();
    Logger::instance().info("Application", "Editor initialized. Open a model to populate the viewport.");
}

Application::~Application() {
    saveConfiguration();
    renderer_.reset();
    editor_.reset();
    window_.reset();
}

void Application::openModel() {
    const auto path = platform::openModelDialog(config_.recentModel);
    if (!path) return;
    ImportedModel imported;
    std::string error;
    if (!ModelImporter::import(*path, imported, error)) {
        Logger::instance().error("Asset", "Could not import " + path->filename().string() + ": " + error);
        editor_->setNotification("Import failed: " + error, LogLevel::Error);
        return;
    }
    try {
        Scene candidateScene = scene_;
        candidateScene.replaceModel(imported);
        renderer_->replaceModel(imported);
        scene_ = std::move(candidateScene);
        modelBounds_ = std::make_pair(imported.boundsMinimum, imported.boundsMaximum);
        camera_.frame(imported.boundsMinimum, imported.boundsMaximum);
        config_.recentModel = *path;
        Logger::instance().info("Application", "Loaded " + imported.displayName + " and framed the camera to its bounds.");
        editor_->setNotification("Loaded " + imported.displayName);
    } catch (const std::exception& error) {
        Logger::instance().error("Asset", "Could not publish " + imported.displayName + ": " + error.what());
        editor_->setNotification("Model load failed. The previous scene is still loaded.", LogLevel::Error);
    }
}

void Application::saveConfiguration() {
    if (window_ && window_->native()) {
        glfwGetWindowSize(window_->native(), &config_.windowWidth, &config_.windowHeight);
        config_.windowWidth = std::clamp(config_.windowWidth, 640, 7680);
        config_.windowHeight = std::clamp(config_.windowHeight, 480, 4320);
    }
    ConfigStore::save(config_);
}

int Application::run() {
    while (!window_->shouldClose()) {
        const auto frameStart = std::chrono::steady_clock::now();
        window_->pollEvents();
        if (!renderer_->prepareFrame(config_.renderer.vsync)) {
            window_->waitEvents();
            continue;
        }

        if (viewportResizePending_ && std::chrono::steady_clock::now() - viewportRequestChangedAt_ >= std::chrono::milliseconds(140)) {
            renderer_->resizeViewport(pendingViewportSize_->first, pendingViewportSize_->second, pendingRenderScale_);
            viewportResizePending_ = false;
        }

        editor_->beginFrame();
        const RendererStats previousStats = renderer_->stats();
        EditorActions actions = editor_->draw(renderer_->viewportTextureHandle(), renderer_->modelName(), renderer_->hasModel(),
            config_.renderer, camera_, Logger::instance().snapshot(), renderer_->deviceName(), renderer_->presentModeName(),
            previousStats, previousCpuMilliseconds_);
        if (actions.clearLog) Logger::instance().clear();
        if (actions.resetCamera) camera_.reset();
        if (actions.frameModel && modelBounds_) camera_.frame(modelBounds_->first, modelBounds_->second);
        if (actions.openModel) openModel();

        if (actions.viewportWidth > 0 && actions.viewportHeight > 0) {
            const auto requested = std::make_pair(actions.viewportWidth, actions.viewportHeight);
            if (lastRequestedViewportWidth_ != actions.viewportWidth || lastRequestedViewportHeight_ != actions.viewportHeight ||
                lastRequestedRenderScale_ != config_.renderer.renderScale) {
                pendingViewportSize_ = requested;
                pendingRenderScale_ = config_.renderer.renderScale;
                viewportRequestChangedAt_ = std::chrono::steady_clock::now();
                viewportResizePending_ = true;
                lastRequestedViewportWidth_ = actions.viewportWidth;
                lastRequestedViewportHeight_ = actions.viewportHeight;
                lastRequestedRenderScale_ = config_.renderer.renderScale;
            }
        }

        const SceneSnapshot sceneSnapshot = scene_.snapshot();
        renderer_->renderFrame(camera_, sceneSnapshot, config_.renderer, editor_->drawData(), previousCpuMilliseconds_);
        const auto frameFinish = std::chrono::steady_clock::now();
        previousCpuMilliseconds_ = std::chrono::duration<double, std::milli>(frameFinish - frameStart).count();
    }
    return 0;
}

} // namespace vr
