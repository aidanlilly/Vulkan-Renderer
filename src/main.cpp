#include "app/Application.hpp"

#include <windows.h>
#include <exception>
#include <string>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        vr::Application application;
        return application.run();
    } catch (const std::exception& error) {
        const std::string message = error.what();
        MessageBoxA(nullptr, message.c_str(), "Vulkan Renderer", MB_OK | MB_ICONERROR);
        return 1;
    }
}
