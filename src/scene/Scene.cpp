#include "scene/Scene.hpp"

#include "assets/ModelImporter.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace vr {

void Scene::replaceModel(const ImportedModel& model) {
    std::vector<SceneEntity> candidate;
    candidate.reserve(model.meshes.size());
    for (std::size_t index = 0; index < model.meshes.size(); ++index) {
        SceneEntity entity;
        entity.id = nextEntityId_++;
        entity.meshReference = static_cast<std::uint32_t>(index);
        entity.materialReference = model.meshes[index].materialIndex;
        candidate.push_back(entity);
    }
    std::stable_sort(candidate.begin(), candidate.end(), [](const SceneEntity& left, const SceneEntity& right) {
        return left.materialReference < right.materialReference;
    });
    entities_ = std::move(candidate);
    modelName_ = model.displayName;
    ++revision_;
}

void Scene::clear() {
    entities_.clear();
    modelName_.clear();
    ++revision_;
}

SceneSnapshot Scene::snapshot() const { return {revision_, entities_}; }

} // namespace vr
