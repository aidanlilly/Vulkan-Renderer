#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace vr {

class Camera {
public:
    [[nodiscard]] glm::mat4 viewProjection(float aspect) const;
    [[nodiscard]] glm::vec3 position() const;
    void orbit(float deltaX, float deltaY);
    void pan(float deltaX, float deltaY, float viewportHeight);
    void zoom(float wheelDelta);
    void frame(const glm::vec3& minimum, const glm::vec3& maximum);
    void reset();

private:
    glm::vec3 target_{0.0f};
    float yaw_{0.65f};
    float pitch_{0.35f};
    float distance_{5.0f};
};

} // namespace vr
