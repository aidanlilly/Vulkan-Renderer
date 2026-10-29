#pragma once

#include <GLFW/glfw3.h>

#include <string>

namespace vr::platform {

class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] GLFWwindow* native() const { return window_; }
    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] bool framebufferSize(int& width, int& height) const;
    void pollEvents() const;
    void waitEvents() const;

private:
    GLFWwindow* window_{};
};

} // namespace vr::platform
