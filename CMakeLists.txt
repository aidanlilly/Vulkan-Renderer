cmake_minimum_required(VERSION 3.25)
project(VulkanRenderer VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "VulkanRenderer currently targets Windows. Platform-specific code is isolated under src/platform for future ports.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}")
endforeach()

include(cmake/Dependencies.cmake)
find_package(Vulkan 1.3 REQUIRED)
find_program(VULKAN_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)

set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")
set(SHADER_SOURCES
    assets/shaders/scene_blinn_phong.vert
    assets/shaders/scene_blinn_phong.frag)
set(SHADER_BINARIES)
foreach(shader IN LISTS SHADER_SOURCES)
    get_filename_component(shader_name "${shader}" NAME)
    set(shader_binary "${SHADER_OUTPUT_DIR}/${shader_name}.spv")
    add_custom_command(OUTPUT "${shader_binary}"
        COMMAND "${VULKAN_GLSLC}" --target-env=vulkan1.3 "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -o "${shader_binary}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        COMMENT "Compiling ${shader_name} to SPIR-V"
        VERBATIM)
    list(APPEND SHADER_BINARIES "${shader_binary}")
endforeach()
add_custom_target(VulkanRendererShaders DEPENDS ${SHADER_BINARIES})

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

add_executable(VulkanRenderer WIN32
    src/main.cpp
    src/app/Application.cpp
    src/core/Log.cpp
    src/core/Config.cpp
    src/platform/Window.cpp
    src/platform/FileDialog.cpp
    src/platform/Paths.cpp
    src/scene/Camera.cpp
    src/scene/Scene.cpp
    src/assets/ModelImporter.cpp
    src/renderer/RenderGraph.cpp
    src/renderer/shaders/ShaderLibrary.cpp
    src/renderer/Renderer.cpp
    src/editor/EditorUI.cpp)
add_dependencies(VulkanRenderer VulkanRendererShaders)
target_include_directories(VulkanRenderer PRIVATE src "${vma_SOURCE_DIR}/include")
target_link_libraries(VulkanRenderer PRIVATE
    Vulkan::Vulkan glfw glm::glm assimp::assimp nlohmann_json::nlohmann_json imgui
    comdlg32 shell32)
target_compile_definitions(VulkanRenderer PRIVATE
    GLFW_INCLUDE_VULKAN
    VK_USE_PLATFORM_WIN32_KHR
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    VULKAN_RENDERER_BUILD_SHADER_DIR="${SHADER_OUTPUT_DIR}")
add_custom_command(TARGET VulkanRenderer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SHADER_OUTPUT_DIR}" "$<TARGET_FILE_DIR:VulkanRenderer>/shaders"
    COMMENT "Copying compiled shaders beside the application"
    VERBATIM)
if(MSVC)
    target_compile_options(VulkanRenderer PRIVATE /W4 /permissive- /EHsc /Zc:preprocessor)
else()
    target_compile_options(VulkanRenderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS VulkanRenderer RUNTIME DESTINATION .)
install(FILES ${SHADER_BINARIES} DESTINATION shaders)
