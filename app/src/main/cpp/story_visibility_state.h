#pragma once
#include <unordered_set>

namespace story_bridge {
// Engine identities are compared only; never dereferenced or retained as GC roots.
class PlaybackState {
public:
    bool start(void* engine) { if (engine) engines.insert(engine); return visible(); }
    bool finish(void* engine) { engines.erase(engine); return visible(); }
    bool reset() { engines.clear(); return false; }
    bool visible() const { return !engines.empty(); }
private:
    std::unordered_set<void*> engines;
};
}
