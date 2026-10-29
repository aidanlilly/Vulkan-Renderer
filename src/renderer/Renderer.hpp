#pragma once

#include "assets/ModelImporter.hpp"
#include "renderer/RendererSettings.hpp"
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct GLFWwindow;

namespace vr {

struct RendererStats {
    std::uint32_t drawCount{};
    std::uint64_t triangleCount{};
    std::uint32_t viewportWidth{};
    std::uint32_t viewportHeight{};
    double gpuFrameMilliseconds{-1.0};
};

class Renderer {
public:
    explicit Renderer(GLFWwindow* window, std::filesystem::path shaderDirectory, bool enableValidation = true, bool vsync = true);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool prepareFrame(bool vsync);
    void resizeViewport(std::uint32_t logicalWidth, std::uint32_t logicalHeight, float renderScale);
    void renderFrame(const Camera& camera, const SceneSnapshot& scene, const RendererSettings& settings, void* imguiDrawData, double cpuFrameMilliseconds);
    void replaceModel(const ImportedModel& model);
    void clearModel();

    [[nodiscard]] std::uintptr_t viewportTextureHandle() const;
    [[nodiscard]] std::string deviceName() const;
    [[nodiscard]] std::string presentModeName() const;
    [[nodiscard]] std::string modelName() const;
    [[nodiscard]] bool hasModel() const;
    [[nodiscard]] RendererStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vr
