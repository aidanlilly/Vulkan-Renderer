#include "editor/EditorUI.hpp"

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>
#include <stdexcept>

namespace vr {
namespace {

ImVec4 levelColor(LogLevel level) {
    if (level == LogLevel::Error) return {0.95f, 0.36f, 0.34f, 1.0f};
    if (level == LogLevel::Warning) return {0.92f, 0.68f, 0.25f, 1.0f};
    return {0.70f, 0.75f, 0.82f, 1.0f};
}

std::string formatTime(std::chrono::system_clock::time_point time) {
    const auto value = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    localtime_s(&local, &value);
    std::ostringstream stream;
    stream << std::put_time(&local, "%H:%M:%S");
    return stream.str();
}

void applyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.WindowPadding = {10.0f, 9.0f};
    style.FramePadding = {8.0f, 5.0f};
    style.ItemSpacing = {8.0f, 6.0f};
    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = {0.075f, 0.082f, 0.094f, 1.0f};
    colors[ImGuiCol_ChildBg] = {0.065f, 0.071f, 0.082f, 1.0f};
    colors[ImGuiCol_PopupBg] = {0.09f, 0.097f, 0.11f, 0.98f};
    colors[ImGuiCol_Border] = {0.20f, 0.22f, 0.25f, 0.62f};
    colors[ImGuiCol_FrameBg] = {0.14f, 0.155f, 0.18f, 1.0f};
    colors[ImGuiCol_FrameBgHovered] = {0.20f, 0.23f, 0.27f, 1.0f};
    colors[ImGuiCol_FrameBgActive] = {0.23f, 0.27f, 0.31f, 1.0f};
    colors[ImGuiCol_TitleBg] = {0.065f, 0.071f, 0.082f, 1.0f};
    colors[ImGuiCol_TitleBgActive] = {0.095f, 0.105f, 0.12f, 1.0f};
    colors[ImGuiCol_Button] = {0.16f, 0.29f, 0.38f, 1.0f};
    colors[ImGuiCol_ButtonHovered] = {0.19f, 0.38f, 0.49f, 1.0f};
    colors[ImGuiCol_ButtonActive] = {0.14f, 0.32f, 0.43f, 1.0f};
    colors[ImGuiCol_Header] = {0.15f, 0.27f, 0.35f, 0.8f};
    colors[ImGuiCol_HeaderHovered] = {0.18f, 0.34f, 0.43f, 0.9f};
    colors[ImGuiCol_HeaderActive] = {0.18f, 0.35f, 0.45f, 1.0f};
    colors[ImGuiCol_CheckMark] = {0.36f, 0.72f, 0.83f, 1.0f};
    colors[ImGuiCol_SliderGrab] = {0.30f, 0.62f, 0.74f, 1.0f};
    colors[ImGuiCol_SliderGrabActive] = {0.41f, 0.78f, 0.88f, 1.0f};
    colors[ImGuiCol_Separator] = {0.22f, 0.24f, 0.28f, 0.75f};
}

} // namespace

EditorUI::EditorUI(GLFWwindow* window, const std::filesystem::path& iniPath) : window_(window), iniPath_(iniPath.string()) {
    initializeLayout_ = !std::filesystem::exists(iniPath);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = iniPath_.c_str();
    applyStyle();
    if (!ImGui_ImplGlfw_InitForVulkan(window_, true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Dear ImGui GLFW backend initialization failed.");
    }
}

EditorUI::~EditorUI() {
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

void EditorUI::beginFrame() {
    // The Vulkan backend is restarted when the swapchain present mode changes.
    // Keep the GLFW backend's main-viewport handle intact across that restart
    // before its NewFrame() code polls GLFW window state.
    ImGui::GetMainViewport()->PlatformHandle = window_;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

EditorActions EditorUI::draw(std::uintptr_t viewportTexture, const std::string& modelName, bool modelLoaded,
    RendererSettings& settings, Camera& camera, const std::vector<LogEntry>& logEntries,
    const std::string& device, const std::string& presentMode, const RendererStats& stats, double cpuMilliseconds) {
    EditorActions actions;
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Model...", "Ctrl+O")) actions.openModel = true;
            if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Camera")) actions.resetCamera = true;
            if (ImGui::MenuItem("Frame Model", nullptr, false, modelLoaded)) actions.frameModel = true;
            ImGui::EndMenu();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s  |  %s", device.c_str(), presentMode.c_str());
        ImGui::EndMainMenuBar();
    }
    if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_O, false)) actions.openModel = true;

    ImGui::SetNextWindowPos(mainViewport->WorkPos);
    ImGui::SetNextWindowSize(mainViewport->WorkSize);
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    constexpr ImGuiWindowFlags dockHostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking;
    ImGui::Begin("VulkanRenderer Dock Host", nullptr, dockHostFlags);
    const ImGuiID dockspaceId = ImGui::GetID("VulkanRendererDockspace");
    if (initializeLayout_) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, mainViewport->WorkSize);
        ImGuiID center = dockspaceId, left = 0, right = 0, bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, &bottom, &center);
        ImGui::DockBuilderDockWindow("Scene", left);
        ImGui::DockBuilderDockWindow("Renderer", right);
        ImGui::DockBuilderDockWindow("Log / Status", bottom);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dockspaceId);
        initializeLayout_ = false;
    }
    ImGui::DockSpace(dockspaceId, {0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (notificationLifetime_ > 0.0f) {
        notificationLifetime_ -= ImGui::GetIO().DeltaTime;
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGui::SetNextWindowPos({mainViewport->WorkPos.x + mainViewport->WorkSize.x - 18.0f, mainViewport->WorkPos.y + 14.0f}, ImGuiCond_Always, {1.0f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.93f);
        ImGui::SetNextWindowSizeConstraints({280.0f, 0.0f}, {520.0f, 120.0f});
        ImGui::Begin("##Notification", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::PushStyleColor(ImGuiCol_Text, levelColor(notificationLevel_));
        ImGui::TextWrapped("%s", notification_.c_str());
        ImGui::PopStyleColor();
        ImGui::End();
    }

    if (ImGui::Begin("Scene")) {
        if (modelLoaded) {
            ImGui::TextUnformatted("Loaded model");
            ImGui::Separator();
            ImGui::TextWrapped("%s", modelName.c_str());
            ImGui::Spacing();
            ImGui::Text("Draw items: %u", stats.drawCount);
            ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(stats.triangleCount));
            if (ImGui::Button("Frame Model", {-1.0f, 0.0f})) actions.frameModel = true;
            if (ImGui::Button("Reset Camera", {-1.0f, 0.0f})) actions.resetCamera = true;
        } else {
            ImGui::TextDisabled("No model loaded");
            ImGui::TextWrapped("Open a glTF model or another supported 3D file to begin.");
            if (ImGui::Button("Open Model", {-1.0f, 0.0f})) actions.openModel = true;
        }
        if (settings.showDiagnostics) {
            ImGui::Spacing();
            ImGui::SeparatorText("Performance");
            const double fps = cpuMilliseconds > 0.0 ? 1000.0 / cpuMilliseconds : 0.0;
            ImGui::Text("CPU frame: %.2f ms (%.0f FPS)", cpuMilliseconds, fps);
            if (stats.gpuFrameMilliseconds >= 0.0) ImGui::Text("GPU scene: %.2f ms", stats.gpuFrameMilliseconds);
            else ImGui::TextDisabled("GPU scene: unavailable");
            ImGui::Text("Viewport: %u x %u", stats.viewportWidth, stats.viewportHeight);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Renderer")) {
        ImGui::TextDisabled("Scene");
        ImGui::Checkbox("Scene rendering", &settings.sceneRendering);
        ImGui::Checkbox("Blinn-Phong lighting", &settings.lightingEnabled);
        ImGui::Separator();
        ImGui::TextDisabled("Lighting");
        ImGui::Checkbox("Directional light", &settings.directionalEnabled);
        if (settings.directionalEnabled) {
            ImGui::ColorEdit3("Direction color", glm::value_ptr(settings.directionalColor), ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat("Direction intensity", &settings.directionalIntensity, 0.0f, 8.0f, "%.2f");
            ImGui::DragFloat3("Direction", glm::value_ptr(settings.directionalDirection), 0.02f, -1.0f, 1.0f, "%.2f");
        }
        ImGui::ColorEdit3("Ambient color", glm::value_ptr(settings.ambientColor), ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("Ambient strength", &settings.ambientStrength, 0.0f, 1.0f, "%.3f");
        ImGui::Checkbox("Point lights", &settings.pointLightsEnabled);
        if (settings.pointLightsEnabled) {
            ImGui::SliderInt("Active point lights", &settings.activePointLights, 0, 8);
            for (int i = 0; i < settings.activePointLights; ++i) {
                auto& light = settings.pointLights[static_cast<std::size_t>(i)];
                const std::string label = "Point light " + std::to_string(i + 1);
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f, -100.0f, 100.0f, "%.2f");
                    ImGui::ColorEdit3("Color", glm::value_ptr(light.color), ImGuiColorEditFlags_NoInputs);
                    ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 30.0f, "%.2f");
                    ImGui::SliderFloat("Range", &light.range, 0.1f, 50.0f, "%.1f");
                    ImGui::TreePop();
                }
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Material");
        ImGui::Checkbox("Override imported material", &settings.materialOverrideEnabled);
        if (settings.materialOverrideEnabled) {
            ImGui::ColorEdit3("Diffuse", glm::value_ptr(settings.materialDiffuse), ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit3("Specular", glm::value_ptr(settings.materialSpecular), ImGuiColorEditFlags_NoInputs);
            ImGui::SliderFloat("Shininess", &settings.materialShininess, 1.0f, 256.0f, "%.0f");
        }
        ImGui::Separator();
        ImGui::TextDisabled("Viewport and presentation");
        ImGui::SliderFloat("Render scale", &settings.renderScale, 0.25f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Vertical sync", &settings.vsync);
        ImGui::Checkbox("Show diagnostics", &settings.showDiagnostics);
    }
    ImGui::End();

    if (ImGui::Begin("Viewport")) {
        if (!modelLoaded) {
            const float width = ImGui::GetContentRegionAvail().x;
            const float offset = std::max((width - 180.0f) * 0.5f, 0.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);
            ImGui::TextDisabled("Vulkan Renderer");
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
            if (ImGui::Button("Open Model", {180.0f, 34.0f})) actions.openModel = true;
            ImGui::TextDisabled("glTF 2.0, OBJ, FBX and other Assimp formats");
        } else {
            ImVec2 available = ImGui::GetContentRegionAvail();
            available.x = std::max(available.x, 1.0f);
            available.y = std::max(available.y, 1.0f);
            actions.viewportWidth = static_cast<std::uint32_t>(available.x);
            actions.viewportHeight = static_cast<std::uint32_t>(available.y);
            const ImTextureID texture = (ImTextureID)(intptr_t)viewportTexture;
            ImGui::Image(texture, available, {0.0f, 1.0f}, {1.0f, 0.0f});
            const bool hovered = ImGui::IsItemHovered();
            ImGuiIO& io = ImGui::GetIO();
            if (hovered && !io.WantTextInput) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) orbitDragging_ = true;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panDragging_ = true;
                if (io.MouseWheel != 0.0f) camera.zoom(io.MouseWheel);
            }
            if (io.WantTextInput) orbitDragging_ = panDragging_ = false;
            if (orbitDragging_) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                else orbitDragging_ = false;
            }
            if (panDragging_) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) camera.pan(io.MouseDelta.x, io.MouseDelta.y, available.y);
                else panDragging_ = false;
            }
            if (hovered) {
                ImGui::SetNextWindowBgAlpha(0.78f);
                ImGui::SetTooltip("Orbit: right-drag  |  Pan: middle-drag  |  Zoom: scroll");
            }
        }
    }
    ImGui::End();

    if (ImGui::Begin("Log / Status")) {
        static bool showInfo = true, showWarnings = true, showErrors = true, autoScroll = true;
        ImGui::Checkbox("Info", &showInfo); ImGui::SameLine();
        ImGui::Checkbox("Warnings", &showWarnings); ImGui::SameLine();
        ImGui::Checkbox("Errors", &showErrors); ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll); ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) actions.clearLog = true;
        ImGui::BeginChild("##log-scroll", {0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& entry : logEntries) {
            if ((entry.level == LogLevel::Info && !showInfo) || (entry.level == LogLevel::Warning && !showWarnings) || (entry.level == LogLevel::Error && !showErrors)) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
            ImGui::TextUnformatted(formatTime(entry.time).c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", entry.subsystem.c_str());
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
            ImGui::TextWrapped("%s", entry.message.c_str());
            ImGui::PopStyleColor();
        }
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::Render();
    return actions;
}

void* EditorUI::drawData() const { return ImGui::GetDrawData(); }

void EditorUI::setNotification(std::string message, LogLevel level) {
    notification_ = std::move(message);
    notificationLevel_ = level;
    notificationLifetime_ = 5.0f;
}

} // namespace vr
