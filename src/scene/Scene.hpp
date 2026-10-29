#pragma once

#include <glm/mat4x4.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace vr {

struct SceneEntity {
    std::uint64_t id{};
    glm::mat4 transform{1.0f};
    std::uint32_t meshReference{};
    std::uint32_t materialReference{};
    bool visible{true};
};

struct SceneSnapshot {
    std::uint64_t revision{};
    std::vector<SceneEntity> entities;
};

struct ImportedModel;

class Scene {
public:
    void replaceModel(const ImportedModel& model);
    void clear();
    [[nodiscard]] SceneSnapshot snapshot() const;
    [[nodiscard]] bool hasModel() const { return !entities_.empty(); }
    [[nodiscard]] const std::string& modelName() const { return modelName_; }

private:
    std::uint64_t nextEntityId_{1};
    std::uint64_t revision_{};
    std::string modelName_;
    std::vector<SceneEntity> entities_;
};

} // namespace vr
