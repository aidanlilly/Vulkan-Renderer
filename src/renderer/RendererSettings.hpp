#pragma once

#include <array>
#include <glm/vec3.hpp>

namespace vr {

struct PointLightSettings {
    glm::vec3 position{2.0f, 2.0f, 2.0f};
    glm::vec3 color{1.0f, 0.82f, 0.62f};
    float intensity{5.0f};
    float range{12.0f};
};

struct RendererSettings {
    bool sceneRendering{true};
    bool lightingEnabled{true};
    bool directionalEnabled{true};
    bool pointLightsEnabled{true};
    bool materialOverrideEnabled{};
    bool vsync{true};
    bool showDiagnostics{true};
    float renderScale{1.0f};
    float ambientStrength{0.12f};
    glm::vec3 ambientColor{1.0f, 1.0f, 1.0f};
    glm::vec3 directionalDirection{-0.5f, -1.0f, -0.3f};
    glm::vec3 directionalColor{1.0f, 0.96f, 0.88f};
    float directionalIntensity{1.2f};
    glm::vec3 materialDiffuse{0.72f, 0.74f, 0.78f};
    glm::vec3 materialSpecular{0.18f, 0.18f, 0.18f};
    float materialShininess{32.0f};
    std::array<PointLightSettings, 8> pointLights{};
    int activePointLights{1};
};

} // namespace vr
