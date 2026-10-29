#include "scene/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace vr {

glm::vec3 Camera::position() const {
    const float cp = std::cos(pitch_);
    return target_ + glm::vec3{distance_ * cp * std::sin(yaw_), distance_ * std::sin(pitch_), distance_ * cp * std::cos(yaw_)};
}

glm::mat4 Camera::viewProjection(float aspect) const {
    const float safeAspect = std::max(aspect, 0.01f);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), safeAspect, 0.01f, std::max(distance_ * 100.0f, 100.0f));
    projection[1][1] *= -1.0f;
    const float pitchCycle = std::remainder(pitch_, glm::two_pi<float>());
    const float upSign = std::cos(pitchCycle) < 0.0f ? -1.0f : 1.0f;
    return projection * glm::lookAt(position(), target_, glm::vec3(0.0f, upSign, 0.0f));
}

void Camera::orbit(float deltaX, float deltaY) {
    constexpr float rotationSpeed = 0.006f;
    yaw_ = std::remainder(yaw_ + deltaX * rotationSpeed, glm::two_pi<float>());
    pitch_ -= deltaY * rotationSpeed;
}

void Camera::pan(float deltaX, float deltaY, float viewportHeight) {
    const glm::vec3 forward = glm::normalize(target_ - position());
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const float scale = distance_ * 1.8f / std::max(viewportHeight, 1.0f);
    target_ += (-right * deltaX + up * deltaY) * scale;
}

void Camera::zoom(float wheelDelta) { distance_ = std::clamp(distance_ * std::pow(0.88f, wheelDelta), 0.05f, 100000.0f); }

void Camera::frame(const glm::vec3& minimum, const glm::vec3& maximum) {
    target_ = (minimum + maximum) * 0.5f;
    const float radius = glm::length(maximum - minimum) * 0.5f;
    distance_ = std::clamp(std::max(radius / std::sin(glm::radians(22.5f)) * 1.15f, 0.25f), 0.25f, 100000.0f);
}

void Camera::reset() {
    target_ = glm::vec3(0.0f);
    yaw_ = 0.65f;
    pitch_ = 0.35f;
    distance_ = 5.0f;
}

} // namespace vr
