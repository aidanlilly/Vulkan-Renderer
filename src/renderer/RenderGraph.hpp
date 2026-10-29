#pragma once

#include <functional>
#include <string>
#include <vector>

namespace vr {

enum class GraphAccess { Read, Write };

struct GraphUse {
    std::string resource;
    GraphAccess access{};
};

class RenderGraph {
public:
    using Callback = std::function<void()>;
    void addPass(std::string name, std::vector<GraphUse> uses, Callback execute);
    bool execute(std::string& error) const;
    void clear();

private:
    struct Pass { std::string name; std::vector<GraphUse> uses; Callback callback; };
    std::vector<Pass> passes_;
};

} // namespace vr
