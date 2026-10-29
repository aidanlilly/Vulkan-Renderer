#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vr {

class ShaderLibrary {
public:
    explicit ShaderLibrary(std::filesystem::path directory);
    [[nodiscard]] std::vector<std::uint32_t> loadSpirv(const std::string& name) const;

private:
    std::filesystem::path directory_;
};

} // namespace vr
