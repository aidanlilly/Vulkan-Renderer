#include "assets/ModelImporter.hpp"

#include "core/Log.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <utility>
#include <limits>

namespace vr {
namespace {
glm::vec3 toGlm(const aiColor3D& v) { return {v.r, v.g, v.b}; }
glm::vec3 toGlm(const aiVector3D& v) { return {v.x, v.y, v.z}; }

bool hasTextures(const aiMaterial* material, aiTextureType type) {
    return material->GetTextureCount(type) > 0;
}
} // namespace

bool ModelImporter::import(const std::filesystem::path& path, ImportedModel& output, std::string& error) {
    Assimp::Importer importer;
    const unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
                           aiProcess_PreTransformVertices | aiProcess_ImproveCacheLocality |
                           aiProcess_SortByPType | aiProcess_ValidateDataStructure;
    const aiScene* scene = importer.ReadFile(path.string(), flags);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr) {
        error = importer.GetErrorString();
        if (error.empty()) error = "The importer returned an incomplete scene.";
        return false;
    }

    ImportedModel candidate;
    candidate.sourcePath = path;
    candidate.displayName = path.filename().string();
    candidate.boundsMinimum = glm::vec3(std::numeric_limits<float>::max());
    candidate.boundsMaximum = glm::vec3(std::numeric_limits<float>::lowest());

    candidate.materials.reserve(std::max(scene->mNumMaterials, 1u));
    for (unsigned index = 0; index < scene->mNumMaterials; ++index) {
        const aiMaterial* source = scene->mMaterials[index];
        CpuMaterial material;
        aiColor3D color;
        if (source->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) material.diffuse = toGlm(color);
        if (source->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) material.specular = toGlm(color);
        if (!std::isfinite(material.diffuse.x) || !std::isfinite(material.diffuse.y) || !std::isfinite(material.diffuse.z)) material.diffuse = CpuMaterial{}.diffuse;
        if (!std::isfinite(material.specular.x) || !std::isfinite(material.specular.y) || !std::isfinite(material.specular.z)) material.specular = CpuMaterial{}.specular;
        float shininess = material.shininess;
        if (source->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && std::isfinite(shininess)) material.shininess = std::clamp(shininess, 1.0f, 256.0f);
        if (hasTextures(source, aiTextureType_DIFFUSE) || hasTextures(source, aiTextureType_BASE_COLOR) ||
            hasTextures(source, aiTextureType_SPECULAR)) {
            Logger::instance().warning("Asset", "Material textures are not enabled in this release; imported diffuse/specular material values will be used.");
        }
        candidate.materials.push_back(material);
    }
    if (candidate.materials.empty()) candidate.materials.emplace_back();

    candidate.meshes.reserve(scene->mNumMeshes);
    for (unsigned meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* source = scene->mMeshes[meshIndex];
        if (source->mNumVertices == 0 || (source->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) continue;
        CpuMesh mesh;
        mesh.materialIndex = source->mMaterialIndex < candidate.materials.size() ? source->mMaterialIndex : 0;
        mesh.vertices.reserve(source->mNumVertices);
        for (unsigned vertexIndex = 0; vertexIndex < source->mNumVertices; ++vertexIndex) {
            CpuVertex vertex;
            vertex.position = toGlm(source->mVertices[vertexIndex]);
            if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z)) {
                error = "The imported model contains a non-finite vertex position.";
                return false;
            }
            if (source->HasNormals()) {
                const glm::vec3 importedNormal = toGlm(source->mNormals[vertexIndex]);
                const float length = glm::length(importedNormal);
                vertex.normal = length > 1e-6f ? importedNormal / length : glm::vec3(0.0f, 1.0f, 0.0f);
            }
            if (source->HasTextureCoords(0)) vertex.texCoord = {source->mTextureCoords[0][vertexIndex].x, source->mTextureCoords[0][vertexIndex].y};
            mesh.vertices.push_back(vertex);
            candidate.boundsMinimum = glm::min(candidate.boundsMinimum, vertex.position);
            candidate.boundsMaximum = glm::max(candidate.boundsMaximum, vertex.position);
        }
        for (unsigned faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex) {
            const aiFace& face = source->mFaces[faceIndex];
            if (face.mNumIndices != 3) continue;
            mesh.indices.insert(mesh.indices.end(), face.mIndices, face.mIndices + 3);
        }
        candidate.triangleCount += mesh.indices.size() / 3;
        if (!mesh.indices.empty()) candidate.meshes.push_back(std::move(mesh));
    }

    if (candidate.meshes.empty() || candidate.triangleCount == 0) {
        error = "The selected file contains no indexed triangle meshes.";
        return false;
    }
    if (!std::isfinite(candidate.boundsMinimum.x) || !std::isfinite(candidate.boundsMinimum.y) || !std::isfinite(candidate.boundsMinimum.z) ||
        !std::isfinite(candidate.boundsMaximum.x) || !std::isfinite(candidate.boundsMaximum.y) || !std::isfinite(candidate.boundsMaximum.z)) {
        error = "The imported model contains invalid vertex positions.";
        return false;
    }
    output = std::move(candidate);
    Logger::instance().info("Asset", "Imported " + output.displayName + " (" + std::to_string(output.meshes.size()) + " meshes, " +
        std::to_string(output.triangleCount) + " triangles).");
    return true;
}

} // namespace vr
