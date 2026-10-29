#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vr {

struct CpuVertex {
    glm::vec3 position{};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 texCoord{};
};

struct CpuMaterial {
    glm::vec3 diffuse{0.72f, 0.74f, 0.78f};
    glm::vec3 specular{0.18f, 0.18f, 0.18f};
    float shininess{32.0f};
};

struct CpuMesh {
    std::vector<CpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t materialIndex{};
};

struct ImportedModel {
    std::filesystem::path sourcePath;
    std::string displayName;
    std::vector<CpuMesh> meshes;
    std::vector<CpuMaterial> materials;
    glm::vec3 boundsMinimum{};
    glm::vec3 boundsMaximum{};
    std::uint64_t triangleCount{};
};

class ModelImporter {
public:
    static bool import(const std::filesystem::path& path, ImportedModel& output, std::string& error);
};

} // namespace vr
