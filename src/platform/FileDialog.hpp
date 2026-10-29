#pragma once

#include <filesystem>
#include <optional>

namespace vr::platform {

std::optional<std::filesystem::path> openModelDialog(const std::optional<std::filesystem::path>& recentPath = std::nullopt);

} // namespace vr::platform
