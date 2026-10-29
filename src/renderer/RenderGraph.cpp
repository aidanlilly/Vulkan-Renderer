#include "renderer/RenderGraph.hpp"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace vr {

void RenderGraph::addPass(std::string name, std::vector<GraphUse> uses, Callback execute) {
    passes_.push_back({std::move(name), std::move(uses), std::move(execute)});
}

bool RenderGraph::execute(std::string& error) const {
    const std::size_t count = passes_.size();
    std::vector<std::unordered_set<std::size_t>> outgoing(count);
    std::vector<unsigned> indegree(count, 0);
    std::unordered_map<std::string, std::size_t> lastWriter;
    std::unordered_map<std::string, std::vector<std::size_t>> readers;
    auto addEdge = [&](std::size_t from, std::size_t to) {
        if (from != to && outgoing[from].insert(to).second) ++indegree[to];
    };

    for (std::size_t passIndex = 0; passIndex < count; ++passIndex) {
        for (const auto& use : passes_[passIndex].uses) {
            if (use.resource.empty()) {
                error = "Render graph pass '" + passes_[passIndex].name + "' names an empty resource.";
                return false;
            }
            if (use.access == GraphAccess::Read) {
                if (const auto writer = lastWriter.find(use.resource); writer != lastWriter.end()) addEdge(writer->second, passIndex);
                readers[use.resource].push_back(passIndex);
            } else {
                if (const auto writer = lastWriter.find(use.resource); writer != lastWriter.end()) addEdge(writer->second, passIndex);
                if (auto foundReaders = readers.find(use.resource); foundReaders != readers.end()) {
                    for (const auto reader : foundReaders->second) addEdge(reader, passIndex);
                    foundReaders->second.clear();
                }
                lastWriter[use.resource] = passIndex;
            }
        }
    }

    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < count; ++i) if (indegree[i] == 0) ready.push(i);
    std::size_t executed = 0;
    while (!ready.empty()) {
        const auto index = ready.front();
        ready.pop();
        if (passes_[index].callback) passes_[index].callback();
        ++executed;
        for (const auto next : outgoing[index]) if (--indegree[next] == 0) ready.push(next);
    }
    if (executed != count) {
        error = "Render graph contains a dependency cycle.";
        return false;
    }
    return true;
}

void RenderGraph::clear() { passes_.clear(); }

} // namespace vr
