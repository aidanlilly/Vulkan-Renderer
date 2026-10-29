#include "platform/Window.hpp"

#include "core/Log.hpp"

#include <stdexcept>

namespace vr::platform {
namespace {
void glfwErrorCallback(int code, const char* message) {
    Logger::instance().error("GLFW", std::to_string(code) + ": " + (message ? message : "unknown error"));
}
}

Window::Window(int width, int height, const char* title) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE) throw std::runtime_error("GLFW initialization failed.");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("GLFW could not create the application window.");
    }
}

Window::~Window() {
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_) == GLFW_TRUE; }
bool Window::framebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(window_, &width, &height);
    return width > 0 && height > 0;
}
void Window::pollEvents() const { glfwPollEvents(); }
void Window::waitEvents() const { glfwWaitEventsTimeout(0.05); }

} // namespace vr::platform
