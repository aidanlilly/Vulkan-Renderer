#include "renderer/Renderer.hpp"

#include "core/Log.hpp"
#include "renderer/RenderGraph.hpp"
#include "renderer/shaders/ShaderLibrary.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace vr {
namespace {

constexpr std::uint32_t FramesInFlight = 2;
constexpr std::uint32_t MaxPointLights = 8;

void checkVk(VkResult result, std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
    }
}

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
    [[nodiscard]] bool complete() const { return graphics.has_value() && present.has_value(); }
};

bool formatHasStencil(VkFormat format) {
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct alignas(16) SceneUniforms {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraPosition{};
    glm::vec4 ambientColor{};
    glm::vec4 directionalDirection{};
    glm::vec4 directionalColor{};
    std::array<glm::vec4, MaxPointLights> pointPositionRange{};
    std::array<glm::vec4, MaxPointLights> pointColorIntensity{};
    glm::ivec4 lightOptions{};
};

struct DrawPushConstants {
    glm::mat4 model{1.0f};
    glm::vec4 diffuse{1.0f};
    glm::vec4 specular{1.0f};
    glm::vec4 materialOptions{32.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace

struct Renderer::Impl {
    struct Buffer {
        VmaAllocator owner{};
        VkBuffer handle{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};

        Buffer() = default;
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&& other) noexcept
            : owner(std::exchange(other.owner, nullptr)), handle(std::exchange(other.handle, VK_NULL_HANDLE)),
              allocation(std::exchange(other.allocation, VK_NULL_HANDLE)) {}
        Buffer& operator=(Buffer&& other) noexcept {
            if (this != &other) {
                reset();
                owner = std::exchange(other.owner, nullptr);
                handle = std::exchange(other.handle, VK_NULL_HANDLE);
                allocation = std::exchange(other.allocation, VK_NULL_HANDLE);
            }
            return *this;
        }
        ~Buffer() { reset(); }
        void reset() {
            if (owner != nullptr && handle != VK_NULL_HANDLE) vmaDestroyBuffer(owner, handle, allocation);
            owner = nullptr;
            handle = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
        }
    };
    struct GpuMesh {
        Buffer vertices;
        Buffer indices;
        std::uint32_t indexCount{};
        CpuMaterial material;
    };
    struct GpuModel {
        std::string name;
        std::vector<GpuMesh> meshes;
        std::uint64_t triangles{};
    };
    struct Frame {
        VkCommandPool commandPool{VK_NULL_HANDLE};
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkSemaphore imageAvailable{VK_NULL_HANDLE};
        Buffer uniformBuffer;
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        VkQueryPool timestampPool{VK_NULL_HANDLE};
        bool timestampSubmitted{};
    };
    struct ViewportTarget {
        VkImage colorImage{VK_NULL_HANDLE};
        VmaAllocation colorAllocation{VK_NULL_HANDLE};
        VkImageView colorView{VK_NULL_HANDLE};
        VkImage depthImage{VK_NULL_HANDLE};
        VmaAllocation depthAllocation{VK_NULL_HANDLE};
        VkImageView depthView{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        VkExtent2D extent{};
        VkImageLayout colorLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout depthLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    };

    GLFWwindow* window{};
    VkInstance instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties physicalProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};
    std::uint32_t graphicsFamily{};
    std::uint32_t presentFamily{};
    VkQueue graphicsQueue{VK_NULL_HANDLE};
    VkQueue presentQueue{VK_NULL_HANDLE};
    VkCommandPool uploadCommandPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout sceneDescriptorLayout{VK_NULL_HANDLE};
    VkDescriptorPool sceneDescriptorPool{VK_NULL_HANDLE};
    VkPipelineLayout scenePipelineLayout{VK_NULL_HANDLE};
    VkPipeline scenePipeline{VK_NULL_HANDLE};
    VkSampler viewportSampler{VK_NULL_HANDLE};
    VkFormat viewportColorFormat{VK_FORMAT_R8G8B8A8_SRGB};
    VkFormat depthFormat{VK_FORMAT_D32_SFLOAT};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkSurfaceFormatKHR swapchainFormat{};
    VkPresentModeKHR presentMode{VK_PRESENT_MODE_FIFO_KHR};
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<bool> swapchainImagePresented;
    std::array<Frame, FramesInFlight> frames{};
    std::uint32_t frameIndex{};
    bool validationEnabled{};
    bool imguiVulkanReady{};
    bool timestampQueriesEnabled{};
    std::uint32_t timestampValidBits{};
    float timestampPeriod{};
    ViewportTarget viewport;
    GpuModel model;
    RendererStats lastStats{};
    std::string selectedDevice;
    ShaderLibrary shaderLibrary;
    bool activeVsync{true};
    bool initialized{};
    VkExtent2D pendingSwapchainExtent{};
    std::chrono::steady_clock::time_point swapchainResizeChangedAt{};
    bool swapchainResizePending{};

    explicit Impl(GLFWwindow* nativeWindow, std::filesystem::path shaderDirectory, bool requestValidation, bool vsync)
        : window(nativeWindow), shaderLibrary(std::move(shaderDirectory)) {
        try {
            createInstance(requestValidation);
            checkVk(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
            selectPhysicalDevice();
            createDevice();
            createAllocator();
            createUploadPool();
            createSceneDescriptors();
            createFrames();
            chooseViewportFormats();
            createScenePipeline();
            createSwapchain(vsync);
            createViewportTarget(1280, 720);
            initialized = true;
            Logger::instance().info("Vulkan", "Selected device: " + selectedDevice + "; Vulkan 1.3 dynamic rendering and synchronization2 enabled.");
            if (!validationEnabled) Logger::instance().warning("Vulkan", "VK_LAYER_KHRONOS_validation is unavailable; validation messages will not be produced.");
        } catch (...) {
            destroy();
            throw;
        }
    }

    ~Impl() { destroy(); }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
        if (data == nullptr || data->pMessage == nullptr) return VK_FALSE;
        const auto level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 ? LogLevel::Error :
                           (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0 ? LogLevel::Warning : LogLevel::Info;
        Logger::instance().write(level, "Validation", data->pMessage);
        return VK_FALSE;
    }

    void createInstance(bool requestValidation) {
        std::uint32_t apiVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS || apiVersion < VK_API_VERSION_1_3) {
            throw std::runtime_error("The installed Vulkan loader does not expose Vulkan 1.3.");
        }
        std::uint32_t layerCount = 0;
        checkVk(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties");
        std::vector<VkLayerProperties> layers(layerCount);
        checkVk(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "vkEnumerateInstanceLayerProperties");
        validationEnabled = requestValidation && std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });

        std::uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        if (glfwExtensions == nullptr || glfwExtensionCount == 0) throw std::runtime_error("GLFW did not provide Vulkan surface extensions.");
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        std::uint32_t extensionCount = 0;
        checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr), "vkEnumerateInstanceExtensionProperties");
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()), "vkEnumerateInstanceExtensionProperties");
        const bool debugUtilsAvailable = std::any_of(availableExtensions.begin(), availableExtensions.end(), [](const auto& extension) {
            return std::strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
        });
        if (validationEnabled && debugUtilsAvailable) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        else if (validationEnabled) {
            validationEnabled = false;
            Logger::instance().warning("Vulkan", "VK_EXT_debug_utils is unavailable; validation was disabled.");
        }

        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "Vulkan Renderer";
        application.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        application.pEngineName = "Vulkan Renderer";
        application.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
        application.apiVersion = VK_API_VERSION_1_3;
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &application;
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        if (validationEnabled) {
            createInfo.enabledLayerCount = 1;
            createInfo.ppEnabledLayerNames = &validationLayer;
        }
        checkVk(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
        if (validationEnabled) {
            auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (createMessenger != nullptr) {
                VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
                messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                messengerInfo.pfnUserCallback = debugCallback;
                checkVk(createMessenger(instance, &messengerInfo, nullptr, &debugMessenger), "vkCreateDebugUtilsMessengerEXT");
            }
        }
    }

    QueueFamilies findQueueFamilies(VkPhysicalDevice candidate) const {
        QueueFamilies result;
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
        for (std::uint32_t i = 0; i < count; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && !result.graphics) result.graphics = i;
            VkBool32 supportsPresent = VK_FALSE;
            if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supportsPresent) == VK_SUCCESS && supportsPresent && !result.present) result.present = i;
        }
        return result;
    }

    SwapchainSupport querySwapchainSupport(VkPhysicalDevice candidate) const {
        SwapchainSupport support;
        checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &support.capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        std::uint32_t count = 0;
        checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        support.formats.resize(count);
        if (count) checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &count, support.formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        count = 0;
        checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &count, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        support.presentModes.resize(count);
        if (count) checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &count, support.presentModes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
        return support;
    }

    bool hasDeviceExtension(VkPhysicalDevice candidate, const char* name) const {
        std::uint32_t count = 0;
        if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr) != VK_SUCCESS) return false;
        std::vector<VkExtensionProperties> extensions(count);
        if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
        return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    }

    void selectPhysicalDevice() {
        std::uint32_t count = 0;
        checkVk(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        if (count == 0) throw std::runtime_error("No Vulkan physical devices were found.");
        std::vector<VkPhysicalDevice> devices(count);
        checkVk(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        int bestScore = std::numeric_limits<int>::min();
        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;
            if (!hasDeviceExtension(candidate, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) continue;
            if (properties.limits.maxUniformBufferRange < sizeof(SceneUniforms) ||
                properties.limits.maxPushConstantsSize < sizeof(DrawPushConstants) ||
                properties.limits.maxPerStageDescriptorUniformBuffers < 1 || properties.limits.maxDescriptorSetUniformBuffers < 1 ||
                properties.limits.maxPerStageDescriptorSamplers < 1 || properties.limits.maxDescriptorSetSamplers < 1 ||
                properties.limits.maxPerStageDescriptorSampledImages < 1 || properties.limits.maxDescriptorSetSampledImages < 1) continue;
            VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;
            if (!hasDeviceExtension(candidate, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
            const QueueFamilies queues = findQueueFamilies(candidate);
            if (!queues.complete()) continue;
            const SwapchainSupport support = querySwapchainSupport(candidate);
            if (support.formats.empty() || support.presentModes.empty()) continue;
            if (support.capabilities.maxImageCount > 0 && support.capabilities.maxImageCount < FramesInFlight) continue;
            bool colorFormatFound = false;
            for (const VkFormat format : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}) {
                VkFormatProperties formatProperties{};
                vkGetPhysicalDeviceFormatProperties(candidate, format, &formatProperties);
                const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
                if ((formatProperties.optimalTilingFeatures & required) == required) { colorFormatFound = true; break; }
            }
            bool depthFormatFound = false;
            for (const VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
                VkFormatProperties formatProperties{};
                vkGetPhysicalDeviceFormatProperties(candidate, format, &formatProperties);
                if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) { depthFormatFound = true; break; }
            }
            if (!colorFormatFound || !depthFormatFound) continue;
            int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 :
                        properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500 : 100;
            score += static_cast<int>(properties.limits.maxImageDimension2D / 1024);
            if (score > bestScore) {
                bestScore = score;
                physicalDevice = candidate;
                physicalProperties = properties;
                graphicsFamily = *queues.graphics;
                presentFamily = *queues.present;
            }
        }
        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("No compatible GPU supports Vulkan 1.3 dynamic rendering, synchronization2, graphics and presentation queues, and swapchains.");
        }
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        selectedDevice = physicalProperties.deviceName;
        timestampPeriod = physicalProperties.limits.timestampPeriod;
    }

    void createDevice() {
        const std::array<std::uint32_t, 2> families{graphicsFamily, presentFamily};
        std::vector<std::uint32_t> uniqueFamilies(families.begin(), families.end());
        std::sort(uniqueFamilies.begin(), uniqueFamilies.end());
        uniqueFamilies.erase(std::unique(uniqueFamilies.begin(), uniqueFamilies.end()), uniqueFamilies.end());
        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (const auto family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            queues.push_back(queue);
        }
        std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};
        if (hasDeviceExtension(physicalDevice, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) extensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.pNext = &features13;
        createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
        createInfo.pQueueCreateInfos = queues.data();
        createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        checkVk(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);

        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queuesProperties(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queuesProperties.data());
        timestampValidBits = queuesProperties[graphicsFamily].timestampValidBits;
        timestampQueriesEnabled = timestampValidBits > 0 && physicalProperties.limits.timestampComputeAndGraphics;
        if (!timestampQueriesEnabled) Logger::instance().info("Vulkan", "GPU timestamp queries are unavailable on the selected graphics queue.");
    }

    void createAllocator() {
        VmaAllocatorCreateInfo createInfo{};
        createInfo.instance = instance;
        createInfo.physicalDevice = physicalDevice;
        createInfo.device = device;
        createInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        checkVk(vmaCreateAllocator(&createInfo, &allocator), "vmaCreateAllocator");
    }

    void createUploadPool() {
        VkCommandPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = graphicsFamily;
        checkVk(vkCreateCommandPool(device, &createInfo, nullptr, &uploadCommandPool), "vkCreateCommandPool(upload)");
    }

    void createSceneDescriptors() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        checkVk(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &sceneDescriptorLayout), "vkCreateDescriptorSetLayout");

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FramesInFlight};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = FramesInFlight;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        checkVk(vkCreateDescriptorPool(device, &poolInfo, nullptr, &sceneDescriptorPool), "vkCreateDescriptorPool(scene)");

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(DrawPushConstants);
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &sceneDescriptorLayout;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &pushRange;
        checkVk(vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &scenePipelineLayout), "vkCreatePipelineLayout");

        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = VK_FILTER_LINEAR;
        sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 1.0f;
        checkVk(vkCreateSampler(device, &sampler, nullptr, &viewportSampler), "vkCreateSampler(viewport)");
    }

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags = 0) {
        Buffer buffer;
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = memoryUsage;
        allocationInfo.flags = flags;
        checkVk(vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &buffer.handle, &buffer.allocation, nullptr), "vmaCreateBuffer");
        buffer.owner = allocator;
        return buffer;
    }

    void destroyBuffer(Buffer& buffer) {
        buffer.reset();
    }

    void immediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = uploadCommandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        VkCommandBuffer command = VK_NULL_HANDLE;
        checkVk(vkAllocateCommandBuffers(device, &allocate, &command), "vkAllocateCommandBuffers(upload)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        try {
            checkVk(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer(upload)");
            record(command);
            checkVk(vkEndCommandBuffer(command), "vkEndCommandBuffer(upload)");
            VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
            commandInfo.commandBuffer = command;
            VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &commandInfo;
            checkVk(vkQueueSubmit2(graphicsQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2(upload)");
            checkVk(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle(upload)");
        } catch (...) {
            vkFreeCommandBuffers(device, uploadCommandPool, 1, &command);
            throw;
        }
        vkFreeCommandBuffers(device, uploadCommandPool, 1, &command);
    }

    Buffer uploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags finalUsage) {
        Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        Buffer destination{};
        bool mapped = false;
        try {
            void* dataPointer = nullptr;
            checkVk(vmaMapMemory(allocator, staging.allocation, &dataPointer), "vmaMapMemory(staging)");
            mapped = true;
            std::memcpy(dataPointer, data, static_cast<std::size_t>(size));
            checkVk(vmaFlushAllocation(allocator, staging.allocation, 0, size), "vmaFlushAllocation(staging)");
            vmaUnmapMemory(allocator, staging.allocation);
            mapped = false;
            destination = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | finalUsage, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
            immediateSubmit([&](VkCommandBuffer command) {
                VkBufferCopy copy{};
                copy.size = size;
                vkCmdCopyBuffer(command, staging.handle, destination.handle, 1, &copy);
            });
        } catch (...) {
            if (mapped) vmaUnmapMemory(allocator, staging.allocation);
            destroyBuffer(staging);
            destroyBuffer(destination);
            throw;
        }
        destroyBuffer(staging);
        return destination;
    }

    void createFrames() {
        std::array<VkDescriptorSetLayout, FramesInFlight> layouts{};
        layouts.fill(sceneDescriptorLayout);
        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = sceneDescriptorPool;
        setInfo.descriptorSetCount = FramesInFlight;
        setInfo.pSetLayouts = layouts.data();
        std::array<VkDescriptorSet, FramesInFlight> sets{};
        checkVk(vkAllocateDescriptorSets(device, &setInfo, sets.data()), "vkAllocateDescriptorSets(scene)");
        for (std::uint32_t i = 0; i < FramesInFlight; ++i) {
            Frame& frame = frames[i];
            frame.descriptorSet = sets[i];
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = graphicsFamily;
            checkVk(vkCreateCommandPool(device, &pool, nullptr, &frame.commandPool), "vkCreateCommandPool(frame)");
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool = frame.commandPool;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            checkVk(vkAllocateCommandBuffers(device, &allocate, &frame.commandBuffer), "vkAllocateCommandBuffers(frame)");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            checkVk(vkCreateFence(device, &fence, nullptr, &frame.fence), "vkCreateFence");
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            checkVk(vkCreateSemaphore(device, &semaphore, nullptr, &frame.imageAvailable), "vkCreateSemaphore(imageAvailable)");
            frame.uniformBuffer = createBuffer(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            VkDescriptorBufferInfo bufferInfo{frame.uniformBuffer.handle, 0, sizeof(SceneUniforms)};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = frame.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &bufferInfo;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            if (timestampQueriesEnabled) {
                VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                query.queryType = VK_QUERY_TYPE_TIMESTAMP;
                query.queryCount = 2;
                checkVk(vkCreateQueryPool(device, &query, nullptr, &frame.timestampPool), "vkCreateQueryPool");
            }
        }
    }

    void chooseViewportFormats() {
        const VkFormatFeatureFlags colorRequirements = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        VkFormatProperties properties{};
        bool colorFound = false;
        for (const VkFormat candidate : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}) {
            vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
            if ((properties.optimalTilingFeatures & colorRequirements) == colorRequirements) {
                viewportColorFormat = candidate;
                colorFound = true;
                break;
            }
        }
        if (!colorFound) throw std::runtime_error("The selected GPU has no supported sampled color attachment format for the viewport.");
        if (viewportColorFormat == VK_FORMAT_R8G8B8A8_UNORM || viewportColorFormat == VK_FORMAT_B8G8R8A8_UNORM) {
            Logger::instance().warning("Vulkan", "sRGB viewport images are unavailable; using UNORM viewport output.");
        }
        bool depthFound = false;
        for (const VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
            depthFormat = candidate;
            vkGetPhysicalDeviceFormatProperties(physicalDevice, depthFormat, &properties);
            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                depthFound = true;
                break;
            }
        }
        if (!depthFound) throw std::runtime_error("No supported depth attachment format was found.");
        if (physicalProperties.limits.maxUniformBufferRange < sizeof(SceneUniforms) ||
            physicalProperties.limits.maxPushConstantsSize < sizeof(DrawPushConstants)) {
            throw std::runtime_error("The selected GPU descriptor or push-constant limits are below the renderer's minimum requirements.");
        }
    }

    VkShaderModule createShaderModule(const std::filesystem::path& path) {
        const auto code = shaderLibrary.loadSpirv(path.filename().string());
        VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = code.size() * sizeof(std::uint32_t);
        createInfo.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        checkVk(vkCreateShaderModule(device, &createInfo, nullptr, &module), "vkCreateShaderModule(" + path.filename().string() + ")");
        return module;
    }

    void createScenePipeline() {
        VkShaderModule vertex = createShaderModule("scene_blinn_phong.vert.spv");
        VkShaderModule fragment = VK_NULL_HANDLE;
        try {
            fragment = createShaderModule("scene_blinn_phong.frag.spv");
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertex;
            stages[0].pName = "main";
            stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragment;
            stages[1].pName = "main";

            VkVertexInputBindingDescription binding{0, sizeof(CpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
            std::array<VkVertexInputAttributeDescription, 3> attributes{{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(CpuVertex, position))},
                {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(CpuVertex, normal))},
                {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(CpuVertex, texCoord))}}};
            VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            vertexInput.vertexBindingDescriptionCount = 1;
            vertexInput.pVertexBindingDescriptions = &binding;
            vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
            vertexInput.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depth.depthTestEnable = VK_TRUE;
            depth.depthWriteEnable = VK_TRUE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.attachmentCount = 1;
            blend.pAttachments = &blendAttachment;
            const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
            dynamic.pDynamicStates = dynamicStates.data();
            VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachmentFormats = &viewportColorFormat;
            rendering.depthAttachmentFormat = depthFormat;
            VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            pipeline.pNext = &rendering;
            pipeline.stageCount = static_cast<std::uint32_t>(stages.size());
            pipeline.pStages = stages.data();
            pipeline.pVertexInputState = &vertexInput;
            pipeline.pInputAssemblyState = &assembly;
            pipeline.pViewportState = &viewportState;
            pipeline.pRasterizationState = &raster;
            pipeline.pMultisampleState = &multisample;
            pipeline.pDepthStencilState = &depth;
            pipeline.pColorBlendState = &blend;
            pipeline.pDynamicState = &dynamic;
            pipeline.layout = scenePipelineLayout;
            checkVk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &scenePipeline), "vkCreateGraphicsPipelines(scene)");
        } catch (...) {
            if (fragment != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
    }

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
        if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
            return {VK_FORMAT_B8G8R8A8_SRGB, formats.front().colorSpace};
        }
        for (const auto& format : formats) if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return format;
        for (const auto& format : formats) if (format.format == VK_FORMAT_R8G8B8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return format;
        Logger::instance().warning("Vulkan", "The surface has no sRGB swapchain format; using its first supported format as a display-space fallback.");
        return formats.front();
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) return capabilities.currentExtent;
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        VkExtent2D extent{static_cast<std::uint32_t>(std::max(width, 0)), static_cast<std::uint32_t>(std::max(height, 0))};
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return extent;
    }

    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) const {
        if (vsync) return VK_PRESENT_MODE_FIFO_KHR;
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) return VK_PRESENT_MODE_MAILBOX_KHR;
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) return VK_PRESENT_MODE_IMMEDIATE_KHR;
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    void removeViewportDescriptor() {
        if (imguiVulkanReady && viewport.descriptorSet != VK_NULL_HANDLE) ImGui_ImplVulkan_RemoveTexture(viewport.descriptorSet);
        viewport.descriptorSet = VK_NULL_HANDLE;
    }

    void addViewportDescriptor() {
        if (!imguiVulkanReady || viewport.colorView == VK_NULL_HANDLE) return;
        viewport.descriptorSet = ImGui_ImplVulkan_AddTexture(viewportSampler, viewport.colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (viewport.descriptorSet == VK_NULL_HANDLE) throw std::runtime_error("Dear ImGui could not allocate a viewport texture descriptor.");
    }

    void initializeImGuiVulkan() {
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_3;
        info.Instance = instance;
        info.PhysicalDevice = physicalDevice;
        info.Device = device;
        info.QueueFamily = graphicsFamily;
        info.Queue = graphicsQueue;
        info.MinImageCount = std::max(2u, querySwapchainSupport(physicalDevice).capabilities.minImageCount);
        info.ImageCount = static_cast<std::uint32_t>(swapchainImages.size());
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        info.DescriptorPoolSize = 128;
        info.UseDynamicRendering = true;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &swapchainFormat.format;
        info.PipelineRenderingCreateInfo = rendering;
        info.CheckVkResultFn = [](VkResult result) { if (result < 0) Logger::instance().error("ImGui/Vulkan", "Backend returned VkResult " + std::to_string(result)); };
        if (!ImGui_ImplVulkan_Init(&info)) throw std::runtime_error("Dear ImGui Vulkan backend initialization failed.");
        imguiVulkanReady = true;
        if (!ImGui_ImplVulkan_CreateFontsTexture()) throw std::runtime_error("Dear ImGui font texture upload failed.");
    }

    void shutdownImGuiVulkan() {
        removeViewportDescriptor();
        if (imguiVulkanReady) {
            ImGui_ImplVulkan_Shutdown();
            imguiVulkanReady = false;
        }
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange.aspectMask = aspect;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;
        VkImageView result = VK_NULL_HANDLE;
        checkVk(vkCreateImageView(device, &view, nullptr, &result), "vkCreateImageView");
        return result;
    }

    void destroyViewportTarget() {
        if (viewport.colorView != VK_NULL_HANDLE) vkDestroyImageView(device, viewport.colorView, nullptr);
        if (viewport.depthView != VK_NULL_HANDLE) vkDestroyImageView(device, viewport.depthView, nullptr);
        if (viewport.colorImage != VK_NULL_HANDLE) vmaDestroyImage(allocator, viewport.colorImage, viewport.colorAllocation);
        if (viewport.depthImage != VK_NULL_HANDLE) vmaDestroyImage(allocator, viewport.depthImage, viewport.depthAllocation);
        viewport = {};
    }

    VkImageLayout depthAttachmentLayout() const {
        return formatHasStencil(depthFormat) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }

    void createViewportTarget(std::uint32_t width, std::uint32_t height) {
        width = std::clamp(width, 1u, physicalProperties.limits.maxImageDimension2D);
        height = std::clamp(height, 1u, physicalProperties.limits.maxImageDimension2D);
        viewport.extent = {width, height};
        VkImageCreateInfo color{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        color.imageType = VK_IMAGE_TYPE_2D;
        color.format = viewportColorFormat;
        color.extent = {width, height, 1};
        color.mipLevels = 1;
        color.arrayLayers = 1;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.tiling = VK_IMAGE_TILING_OPTIMAL;
        color.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        color.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo allocation{};
        allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        checkVk(vmaCreateImage(allocator, &color, &allocation, &viewport.colorImage, &viewport.colorAllocation, nullptr), "vmaCreateImage(viewport color)");
        viewport.colorView = createImageView(viewport.colorImage, viewportColorFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageCreateInfo depth{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depth.imageType = VK_IMAGE_TYPE_2D;
        depth.format = depthFormat;
        depth.extent = {width, height, 1};
        depth.mipLevels = 1;
        depth.arrayLayers = 1;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        checkVk(vmaCreateImage(allocator, &depth, &allocation, &viewport.depthImage, &viewport.depthAllocation, nullptr), "vmaCreateImage(viewport depth)");
        const VkImageAspectFlags depthAspect = formatHasStencil(depthFormat) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
        viewport.depthView = createImageView(viewport.depthImage, depthFormat, depthAspect);
        addViewportDescriptor();
        lastStats.viewportWidth = width;
        lastStats.viewportHeight = height;
    }

    void createSwapchain(bool vsync) {
        SwapchainSupport support = querySwapchainSupport(physicalDevice);
        if (support.formats.empty() || support.presentModes.empty()) throw std::runtime_error("The presentation surface has no compatible formats or present modes.");
        if ((support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            throw std::runtime_error("The presentation surface does not support color-attachment swapchain images.");
        }
        const VkSurfaceFormatKHR chosenFormat = chooseSurfaceFormat(support.formats);
        const VkPresentModeKHR chosenMode = choosePresentMode(support.presentModes, vsync);
        const VkExtent2D extent = chooseExtent(support.capabilities);
        if (extent.width == 0 || extent.height == 0) return;
        std::uint32_t imageCount = std::max(FramesInFlight, support.capabilities.minImageCount + 1);
        if (support.capabilities.maxImageCount > 0) imageCount = std::min(imageCount, support.capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = chosenFormat.format;
        createInfo.imageColorSpace = chosenFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const std::array queueFamilies{graphicsFamily, presentFamily};
        if (graphicsFamily != presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilies.data();
        } else createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = support.capabilities.currentTransform;
        const std::array alphaModes{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (const auto alpha : alphaModes) if ((support.capabilities.supportedCompositeAlpha & alpha) != 0) { createInfo.compositeAlpha = alpha; break; }
        createInfo.presentMode = chosenMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = swapchain;
        VkSwapchainKHR replacement = VK_NULL_HANDLE;
        checkVk(vkCreateSwapchainKHR(device, &createInfo, nullptr, &replacement), "vkCreateSwapchainKHR");
        std::uint32_t actualCount = 0;
        checkVk(vkGetSwapchainImagesKHR(device, replacement, &actualCount, nullptr), "vkGetSwapchainImagesKHR");
        std::vector<VkImage> replacementImages(actualCount);
        checkVk(vkGetSwapchainImagesKHR(device, replacement, &actualCount, replacementImages.data()), "vkGetSwapchainImagesKHR");
        std::vector<VkImageView> replacementViews;
        replacementViews.reserve(actualCount);
        try {
            for (const auto image : replacementImages) replacementViews.push_back(createImageView(image, chosenFormat.format, VK_IMAGE_ASPECT_COLOR_BIT));
        } catch (...) {
            for (const auto view : replacementViews) vkDestroyImageView(device, view, nullptr);
            vkDestroySwapchainKHR(device, replacement, nullptr);
            throw;
        }

        const bool hadSwapchain = swapchain != VK_NULL_HANDLE;
        const bool formatChanged = hadSwapchain && swapchainFormat.format != chosenFormat.format;
        if (imguiVulkanReady) shutdownImGuiVulkan();
        for (const auto view : swapchainViews) vkDestroyImageView(device, view, nullptr);
        if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
        for (const auto semaphore : renderFinishedSemaphores) vkDestroySemaphore(device, semaphore, nullptr);
        swapchain = replacement;
        swapchainImages = std::move(replacementImages);
        swapchainViews = std::move(replacementViews);
        swapchainFormat = chosenFormat;
        swapchainExtent = extent;
        presentMode = chosenMode;
        activeVsync = vsync;
        swapchainImagePresented.assign(swapchainImages.size(), false);
        renderFinishedSemaphores.clear();
        renderFinishedSemaphores.reserve(swapchainImages.size());
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (std::size_t i = 0; i < swapchainImages.size(); ++i) {
            VkSemaphore created = VK_NULL_HANDLE;
            checkVk(vkCreateSemaphore(device, &semaphore, nullptr, &created), "vkCreateSemaphore(renderFinished)");
            renderFinishedSemaphores.push_back(created);
        }
        if (hadSwapchain) Logger::instance().info("Vulkan", std::string("Swapchain rebuilt (format ") + (formatChanged ? "changed" : "unchanged") + ", " + std::to_string(extent.width) + "x" + std::to_string(extent.height) + ").");
        if (imguiContextExists()) initializeImGuiVulkan();
        addViewportDescriptor();
    }

    bool imguiContextExists() const { return ImGui::GetCurrentContext() != nullptr; }

    bool prepareFrame(bool vsync) {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) return false;
        const VkExtent2D requestedExtent = chooseExtent(querySwapchainSupport(physicalDevice).capabilities);
        if (swapchain == VK_NULL_HANDLE || vsync != activeVsync) {
            swapchainResizePending = false;
            checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(swapchain rebuild)");
            createSwapchain(vsync);
            return swapchain != VK_NULL_HANDLE;
        }
        const bool extentChanged = requestedExtent.width != swapchainExtent.width || requestedExtent.height != swapchainExtent.height;
        if (extentChanged) {
            if (!swapchainResizePending || requestedExtent.width != pendingSwapchainExtent.width || requestedExtent.height != pendingSwapchainExtent.height) {
                pendingSwapchainExtent = requestedExtent;
                swapchainResizeChangedAt = std::chrono::steady_clock::now();
                swapchainResizePending = true;
                return false;
            }
            if (std::chrono::steady_clock::now() - swapchainResizeChangedAt < std::chrono::milliseconds(100)) return false;
            checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(swapchain resize)");
            createSwapchain(vsync);
            swapchainResizePending = false;
        } else swapchainResizePending = false;
        return swapchain != VK_NULL_HANDLE;
    }

    void resizeViewport(std::uint32_t logicalWidth, std::uint32_t logicalHeight, float scale) {
        if (logicalWidth == 0 || logicalHeight == 0) return;
        int windowWidth = 0, windowHeight = 0, framebufferWidth = 0, framebufferHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        const float dpiX = windowWidth > 0 ? static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth) : 1.0f;
        const float dpiY = windowHeight > 0 ? static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight) : 1.0f;
        const auto width = static_cast<std::uint32_t>(std::clamp(std::lround(logicalWidth * dpiX * scale), 1l, static_cast<long>(physicalProperties.limits.maxImageDimension2D)));
        const auto height = static_cast<std::uint32_t>(std::clamp(std::lround(logicalHeight * dpiY * scale), 1l, static_cast<long>(physicalProperties.limits.maxImageDimension2D)));
        if (viewport.extent.width == width && viewport.extent.height == height) return;
        checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(viewport resize)");
        removeViewportDescriptor();
        destroyViewportTarget();
        createViewportTarget(width, height);
        Logger::instance().info("Renderer", "Viewport target resized to " + std::to_string(width) + "x" + std::to_string(height) + ".");
    }

    void destroyModel(GpuModel& target) {
        for (auto& mesh : target.meshes) {
            destroyBuffer(mesh.vertices);
            destroyBuffer(mesh.indices);
        }
        target.meshes.clear();
        target.name.clear();
        target.triangles = 0;
    }

    void replaceModel(const ImportedModel& imported) {
        checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(model replacement)");
        GpuModel candidate;
        candidate.name = imported.displayName;
        candidate.triangles = imported.triangleCount;
        try {
            candidate.meshes.reserve(imported.meshes.size());
            for (const auto& source : imported.meshes) {
                if (source.vertices.empty() || source.indices.empty()) continue;
                GpuMesh mesh;
                try {
                    mesh.vertices = uploadBuffer(source.vertices.data(), source.vertices.size() * sizeof(CpuVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                    mesh.indices = uploadBuffer(source.indices.data(), source.indices.size() * sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
                    mesh.indexCount = static_cast<std::uint32_t>(source.indices.size());
                    mesh.material = source.materialIndex < imported.materials.size() ? imported.materials[source.materialIndex] : CpuMaterial{};
                    candidate.meshes.push_back(std::move(mesh));
                } catch (...) {
                    destroyBuffer(mesh.vertices);
                    destroyBuffer(mesh.indices);
                    throw;
                }
            }
            if (candidate.meshes.empty()) throw std::runtime_error("The imported model did not contain GPU-ready mesh buffers.");
        } catch (...) {
            destroyModel(candidate);
            throw;
        }
        destroyModel(model);
        model = std::move(candidate);
        Logger::instance().info("Renderer", "GPU upload complete for " + model.name + ".");
    }

    void updateUniforms(Frame& frame, const Camera& camera, const RendererSettings& settings) {
        SceneUniforms uniforms{};
        const float aspect = viewport.extent.height > 0 ? static_cast<float>(viewport.extent.width) / static_cast<float>(viewport.extent.height) : 1.0f;
        uniforms.viewProjection = camera.viewProjection(aspect);
        uniforms.cameraPosition = glm::vec4(camera.position(), 1.0f);
        uniforms.ambientColor = glm::vec4(settings.ambientColor * settings.ambientStrength, 1.0f);
        const float directionLength = glm::length(settings.directionalDirection);
        const glm::vec3 safeDirection = directionLength > 1e-6f ? settings.directionalDirection / directionLength : glm::vec3(-0.5f, -1.0f, -0.3f);
        uniforms.directionalDirection = glm::vec4(safeDirection, 0.0f);
        uniforms.directionalColor = glm::vec4(settings.directionalColor * settings.directionalIntensity, 1.0f);
        const int count = std::clamp(settings.activePointLights, 0, static_cast<int>(MaxPointLights));
        for (int i = 0; i < count; ++i) {
            const auto& light = settings.pointLights[static_cast<std::size_t>(i)];
            uniforms.pointPositionRange[static_cast<std::size_t>(i)] = glm::vec4(light.position, light.range);
            uniforms.pointColorIntensity[static_cast<std::size_t>(i)] = glm::vec4(light.color, light.intensity);
        }
        uniforms.lightOptions = glm::ivec4(settings.lightingEnabled ? 1 : 0, settings.directionalEnabled ? 1 : 0,
            settings.pointLightsEnabled ? 1 : 0, count);
        void* mapped = nullptr;
        checkVk(vmaMapMemory(allocator, frame.uniformBuffer.allocation, &mapped), "vmaMapMemory(scene uniforms)");
        try {
            std::memcpy(mapped, &uniforms, sizeof(uniforms));
            checkVk(vmaFlushAllocation(allocator, frame.uniformBuffer.allocation, 0, sizeof(uniforms)), "vmaFlushAllocation(scene uniforms)");
        } catch (...) {
            vmaUnmapMemory(allocator, frame.uniformBuffer.allocation);
            throw;
        }
        vmaUnmapMemory(allocator, frame.uniformBuffer.allocation);
    }

    void imageBarrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
        VkImageAspectFlags aspect) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
    }

    void recordScenePass(VkCommandBuffer command, Frame& frame, const SceneSnapshot& scene, const RendererSettings& settings) {
        imageBarrier(command, viewport.colorImage, viewport.colorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            viewport.colorLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            viewport.colorLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        imageBarrier(command, viewport.depthImage, viewport.depthLayout, depthAttachmentLayout(),
            viewport.depthLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            viewport.depthLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            formatHasStencil(depthFormat) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_DEPTH_BIT);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = viewport.colorView;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.075f, 0.082f, 0.095f, 1.0f}};
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth.imageView = viewport.depthView;
        depth.imageLayout = depthAttachmentLayout();
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, viewport.extent};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(command, &rendering);
        VkViewport viewportState{0.0f, 0.0f, static_cast<float>(viewport.extent.width), static_cast<float>(viewport.extent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, viewport.extent};
        vkCmdSetViewport(command, 0, 1, &viewportState);
        vkCmdSetScissor(command, 0, 1, &scissor);

        lastStats.drawCount = 0;
        lastStats.triangleCount = 0;
        if (settings.sceneRendering && !model.meshes.empty()) {
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout, 0, 1, &frame.descriptorSet, 0, nullptr);
            for (const auto& entity : scene.entities) {
                if (!entity.visible || entity.meshReference >= model.meshes.size()) continue;
                const auto& mesh = model.meshes[entity.meshReference];
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command, 0, 1, &mesh.vertices.handle, &offset);
                vkCmdBindIndexBuffer(command, mesh.indices.handle, 0, VK_INDEX_TYPE_UINT32);
                DrawPushConstants push;
                push.model = entity.transform;
                push.diffuse = glm::vec4(settings.materialOverrideEnabled ? settings.materialDiffuse : mesh.material.diffuse, 1.0f);
                push.specular = glm::vec4(settings.materialOverrideEnabled ? settings.materialSpecular : mesh.material.specular, 1.0f);
                push.materialOptions.x = settings.materialOverrideEnabled ? settings.materialShininess : mesh.material.shininess;
                vkCmdPushConstants(command, scenePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
                vkCmdDrawIndexed(command, mesh.indexCount, 1, 0, 0, 0);
                ++lastStats.drawCount;
                lastStats.triangleCount += mesh.indexCount / 3;
            }
        }
        vkCmdEndRendering(command);
        viewport.colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        viewport.depthLayout = depthAttachmentLayout();
        imageBarrier(command, viewport.colorImage, viewport.colorLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        viewport.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void recordCompositionPass(VkCommandBuffer command, std::uint32_t imageIndex, void* drawData) {
        imageBarrier(command, swapchainImages[imageIndex], swapchainImagePresented[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = swapchainViews[imageIndex];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{0.055f, 0.060f, 0.070f, 1.0f}};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, swapchainExtent};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(command, &rendering);
        ImGui_ImplVulkan_RenderDrawData(static_cast<ImDrawData*>(drawData), command);
        vkCmdEndRendering(command);
    }

    void recordPresentPass(VkCommandBuffer command, std::uint32_t imageIndex) {
        imageBarrier(command, swapchainImages[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    void retrieveTimestamp(Frame& frame) {
        if (!timestampQueriesEnabled || frame.timestampPool == VK_NULL_HANDLE) {
            lastStats.gpuFrameMilliseconds = -1.0;
            frame.timestampSubmitted = false;
            return;
        }
        std::array<std::uint64_t, 2> timestamps{};
        const VkResult result = vkGetQueryPoolResults(device, frame.timestampPool, 0, 2, sizeof(timestamps), timestamps.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result == VK_SUCCESS) {
            std::uint64_t elapsed = timestamps[1] - timestamps[0];
            if (timestampValidBits < 64) elapsed &= (std::uint64_t{1} << timestampValidBits) - 1;
            lastStats.gpuFrameMilliseconds = static_cast<double>(elapsed) * timestampPeriod / 1'000'000.0;
        } else lastStats.gpuFrameMilliseconds = -1.0;
        frame.timestampSubmitted = false;
    }

    void renderFrame(const Camera& camera, const SceneSnapshot& scene, const RendererSettings& settings, void* drawData, double cpuMilliseconds) {
        Frame& frame = frames[frameIndex];
        checkVk(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(frame)");
        if (frame.timestampSubmitted) retrieveTimestamp(frame);
        std::uint32_t imageIndex = 0;
        const VkResult acquired = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(acquire recovery)");
            createSwapchain(settings.vsync);
            return;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) checkVk(acquired, "vkAcquireNextImageKHR");
        checkVk(vkResetFences(device, 1, &frame.fence), "vkResetFences(frame)");
        checkVk(vkResetCommandPool(device, frame.commandPool, 0), "vkResetCommandPool(frame)");
        updateUniforms(frame, camera, settings);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checkVk(vkBeginCommandBuffer(frame.commandBuffer, &begin), "vkBeginCommandBuffer(frame)");
        if (timestampQueriesEnabled) {
            vkCmdResetQueryPool(frame.commandBuffer, frame.timestampPool, 0, 2);
            vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame.timestampPool, 0);
        }

        RenderGraph graph;
        graph.addPass("Scene", {{"ViewportColor", GraphAccess::Write}, {"ViewportDepth", GraphAccess::Write}}, [&] {
            recordScenePass(frame.commandBuffer, frame, scene, settings);
            if (timestampQueriesEnabled) vkCmdWriteTimestamp2(frame.commandBuffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, frame.timestampPool, 1);
        });
        graph.addPass("Editor composition", {{"ViewportColor", GraphAccess::Read}, {"SwapchainColor", GraphAccess::Write}}, [&] {
            recordCompositionPass(frame.commandBuffer, imageIndex, drawData);
        });
        graph.addPass("Present transition", {{"SwapchainColor", GraphAccess::Read}}, [&] { recordPresentPass(frame.commandBuffer, imageIndex); });
        std::string graphError;
        if (!graph.execute(graphError)) throw std::runtime_error(graphError);
        checkVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer(frame)");

        VkSemaphoreSubmitInfo waitSemaphore{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        waitSemaphore.semaphore = frame.imageAvailable;
        waitSemaphore.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = frame.commandBuffer;
        VkSemaphoreSubmitInfo signalSemaphore{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signalSemaphore.semaphore = renderFinishedSemaphores[imageIndex];
        signalSemaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &waitSemaphore;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commandInfo;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signalSemaphore;
        checkVk(vkQueueSubmit2(graphicsQueue, 1, &submit, frame.fence), "vkQueueSubmit2(frame)");
        frame.timestampSubmitted = timestampQueriesEnabled;

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinishedSemaphores[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        const VkResult presented = vkQueuePresentKHR(presentQueue, &present);
        swapchainImagePresented[imageIndex] = true;
        lastStats.viewportWidth = viewport.extent.width;
        lastStats.viewportHeight = viewport.extent.height;
        lastStats.gpuFrameMilliseconds = timestampQueriesEnabled ? lastStats.gpuFrameMilliseconds : -1.0;
        (void)cpuMilliseconds;
        frameIndex = (frameIndex + 1) % FramesInFlight;
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || acquired == VK_SUBOPTIMAL_KHR) {
            checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(present recovery)");
            createSwapchain(settings.vsync);
        } else checkVk(presented, "vkQueuePresentKHR");
    }

    void clearModel() {
        checkVk(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(model clear)");
        destroyModel(model);
    }

    std::string presentModeName() const {
        switch (presentMode) {
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO (VSync)";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "Mailbox";
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "Immediate";
        default: return "Other";
        }
    }

    void destroy() {
        if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
        if (device != VK_NULL_HANDLE && imguiVulkanReady) shutdownImGuiVulkan();
        if (device != VK_NULL_HANDLE) {
            destroyViewportTarget();
            destroyModel(model);
            for (const auto view : swapchainViews) vkDestroyImageView(device, view, nullptr);
            swapchainViews.clear();
            for (const auto semaphore : renderFinishedSemaphores) vkDestroySemaphore(device, semaphore, nullptr);
            renderFinishedSemaphores.clear();
            if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
            for (auto& frame : frames) {
                destroyBuffer(frame.uniformBuffer);
                if (frame.timestampPool != VK_NULL_HANDLE) vkDestroyQueryPool(device, frame.timestampPool, nullptr);
                if (frame.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(device, frame.imageAvailable, nullptr);
                if (frame.fence != VK_NULL_HANDLE) vkDestroyFence(device, frame.fence, nullptr);
                if (frame.commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, frame.commandPool, nullptr);
            }
            if (scenePipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, scenePipeline, nullptr);
            if (scenePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, scenePipelineLayout, nullptr);
            if (sceneDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, sceneDescriptorPool, nullptr);
            if (sceneDescriptorLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, sceneDescriptorLayout, nullptr);
            if (viewportSampler != VK_NULL_HANDLE) vkDestroySampler(device, viewportSampler, nullptr);
            if (uploadCommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, uploadCommandPool, nullptr);
            if (allocator != VK_NULL_HANDLE) vmaDestroyAllocator(allocator);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debugMessenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyMessenger != nullptr) destroyMessenger(instance, debugMessenger, nullptr);
        }
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
};

Renderer::Renderer(GLFWwindow* window, std::filesystem::path shaderDirectory, bool enableValidation, bool vsync)
    : impl_(std::make_unique<Impl>(window, std::move(shaderDirectory), enableValidation, vsync)) {}
Renderer::~Renderer() = default;

bool Renderer::prepareFrame(bool vsync) { return impl_->prepareFrame(vsync); }
void Renderer::resizeViewport(std::uint32_t width, std::uint32_t height, float scale) { impl_->resizeViewport(width, height, scale); }
void Renderer::renderFrame(const Camera& camera, const SceneSnapshot& scene, const RendererSettings& settings, void* drawData, double cpuMilliseconds) { impl_->renderFrame(camera, scene, settings, drawData, cpuMilliseconds); }
void Renderer::replaceModel(const ImportedModel& model) { impl_->replaceModel(model); }
void Renderer::clearModel() { impl_->clearModel(); }
std::uintptr_t Renderer::viewportTextureHandle() const { return reinterpret_cast<std::uintptr_t>(impl_->viewport.descriptorSet); }
std::string Renderer::deviceName() const { return impl_->selectedDevice; }
std::string Renderer::presentModeName() const { return impl_->presentModeName(); }
std::string Renderer::modelName() const { return impl_->model.name; }
bool Renderer::hasModel() const { return !impl_->model.meshes.empty(); }
RendererStats Renderer::stats() const { return impl_->lastStats; }

} // namespace vr
