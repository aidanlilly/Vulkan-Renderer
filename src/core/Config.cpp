#include "core/Config.hpp"

#include "core/Log.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <utility>

namespace vr {
namespace {
using json = nlohmann::json;

glm::vec3 readVec3(const json& value, const glm::vec3& fallback) {
    if (!value.is_array() || value.size() != 3) return fallback;
    try {
        const glm::vec3 result{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) return fallback;
        return result;
    } catch (...) { return fallback; }
}

json writeVec3(const glm::vec3& value) { return json::array({value.x, value.y, value.z}); }

void readSettings(const json& source, RendererSettings& settings) {
    if (!source.is_object()) return;
    settings.sceneRendering = source.value("sceneRendering", settings.sceneRendering);
    settings.lightingEnabled = source.value("lightingEnabled", settings.lightingEnabled);
    settings.directionalEnabled = source.value("directionalEnabled", settings.directionalEnabled);
    settings.pointLightsEnabled = source.value("pointLightsEnabled", settings.pointLightsEnabled);
    settings.materialOverrideEnabled = source.value("materialOverrideEnabled", settings.materialOverrideEnabled);
    settings.vsync = source.value("vsync", settings.vsync);
    settings.showDiagnostics = source.value("showDiagnostics", settings.showDiagnostics);
    settings.renderScale = std::clamp(source.value("renderScale", settings.renderScale), 0.25f, 2.0f);
    settings.ambientStrength = std::clamp(source.value("ambientStrength", settings.ambientStrength), 0.0f, 2.0f);
    settings.ambientColor = readVec3(source.value("ambientColor", json{}), settings.ambientColor);
    settings.directionalDirection = readVec3(source.value("directionalDirection", json{}), settings.directionalDirection);
    settings.directionalColor = readVec3(source.value("directionalColor", json{}), settings.directionalColor);
    settings.directionalIntensity = std::clamp(source.value("directionalIntensity", settings.directionalIntensity), 0.0f, 20.0f);
    settings.materialDiffuse = readVec3(source.value("materialDiffuse", json{}), settings.materialDiffuse);
    settings.materialSpecular = readVec3(source.value("materialSpecular", json{}), settings.materialSpecular);
    settings.materialShininess = std::clamp(source.value("materialShininess", settings.materialShininess), 1.0f, 256.0f);
    settings.activePointLights = std::clamp(source.value("activePointLights", settings.activePointLights), 0, 8);
    if (source.contains("pointLights") && source["pointLights"].is_array()) {
        const auto& lights = source["pointLights"];
        for (std::size_t i = 0; i < std::min<std::size_t>(lights.size(), settings.pointLights.size()); ++i) {
            if (!lights[i].is_object()) continue;
            auto& light = settings.pointLights[i];
            light.position = readVec3(lights[i].value("position", json{}), light.position);
            light.color = readVec3(lights[i].value("color", json{}), light.color);
            light.intensity = std::clamp(lights[i].value("intensity", light.intensity), 0.0f, 100.0f);
            light.range = std::clamp(lights[i].value("range", light.range), 0.1f, 1000.0f);
        }
    }
}

json writeSettings(const RendererSettings& settings) {
    json lights = json::array();
    for (const auto& light : settings.pointLights) {
        lights.push_back({{"position", writeVec3(light.position)}, {"color", writeVec3(light.color)},
                          {"intensity", light.intensity}, {"range", light.range}});
    }
    return {{"sceneRendering", settings.sceneRendering}, {"lightingEnabled", settings.lightingEnabled},
            {"directionalEnabled", settings.directionalEnabled}, {"pointLightsEnabled", settings.pointLightsEnabled},
            {"materialOverrideEnabled", settings.materialOverrideEnabled},
            {"vsync", settings.vsync}, {"showDiagnostics", settings.showDiagnostics},
            {"renderScale", settings.renderScale}, {"ambientStrength", settings.ambientStrength},
            {"ambientColor", writeVec3(settings.ambientColor)},
            {"directionalDirection", writeVec3(settings.directionalDirection)},
            {"directionalColor", writeVec3(settings.directionalColor)},
            {"directionalIntensity", settings.directionalIntensity},
            {"materialDiffuse", writeVec3(settings.materialDiffuse)},
            {"materialSpecular", writeVec3(settings.materialSpecular)},
            {"materialShininess", settings.materialShininess},
            {"activePointLights", settings.activePointLights}, {"pointLights", std::move(lights)}};
}
} // namespace

std::filesystem::path ConfigStore::configDirectory() {
    const char* appData = std::getenv("APPDATA");
    const auto base = appData ? std::filesystem::path(appData) : std::filesystem::temp_directory_path();
    return base / "VulkanRenderer";
}

std::filesystem::path ConfigStore::imguiIniPath() { return configDirectory() / "imgui.ini"; }

AppConfig ConfigStore::load() {
    AppConfig config;
    const auto path = configDirectory() / "settings.json";
    try {
        if (!std::filesystem::exists(path)) return config;
        std::ifstream file(path);
        const json data = json::parse(file);
        if (data.value("version", 0) != 1) {
            Logger::instance().warning("Config", "Unknown settings version; using defaults.");
            return config;
        }
        config.windowWidth = std::clamp(data.value("windowWidth", config.windowWidth), 640, 7680);
        config.windowHeight = std::clamp(data.value("windowHeight", config.windowHeight), 480, 4320);
        if (data.contains("renderer")) readSettings(data["renderer"], config.renderer);
        const auto recent = data.value("recentModel", std::string{});
        if (!recent.empty()) config.recentModel = std::filesystem::u8path(recent);
    } catch (const std::exception& error) {
        Logger::instance().warning("Config", std::string("Could not read settings: ") + error.what());
    }
    return config;
}

void ConfigStore::save(const AppConfig& config) {
    try {
        std::filesystem::create_directories(configDirectory());
        std::string recent;
        if (config.recentModel) {
            const auto encoded = config.recentModel->u8string();
            recent.assign(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        }
        json data{{"version", 1}, {"windowWidth", config.windowWidth}, {"windowHeight", config.windowHeight},
                  {"renderer", writeSettings(config.renderer)},
                  {"recentModel", recent}};
        std::ofstream file(configDirectory() / "settings.json", std::ios::trunc);
        file << data.dump(2) << '\n';
    } catch (const std::exception& error) {
        Logger::instance().warning("Config", std::string("Could not save settings: ") + error.what());
    }
}

} // namespace vr
