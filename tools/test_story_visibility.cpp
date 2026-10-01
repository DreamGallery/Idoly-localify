#include <cassert>
#include "../app/src/main/cpp/story_visibility_state.h"

int main() {
    story_bridge::PlaybackState state;
    int normal = 0, love = 0, unloaded = 0;
    // Failed loading never reached OnPlay: its cleanup must not hide anything.
    assert(!state.finish(&unloaded));
    assert(!state.start(nullptr));
    // Resume/seek can repeat OnPlay for one engine; only one cleanup is needed.
    assert(state.start(&normal));
    assert(state.start(&normal));
    assert(!state.finish(&normal));
    // A second engine may start before the first one's cleanup finishes.
    assert(state.start(&normal));
    assert(state.start(&love));
    assert(state.finish(&normal));
    // End may synchronously call Clear. Duplicate cleanup must not affect love.
    assert(state.finish(&normal));
    assert(state.finish(&unloaded));
    assert(!state.finish(&love));
    // A nested End callback may launch another script on the same engine.
    assert(state.start(&normal));
    assert(!state.finish(&normal));
    assert(state.start(&normal));
    assert(state.visible()); // No deferred post-original removal may run here.
    // Old presenter destruction resolves its own engine, never clears all.
    assert(state.start(&love));
    assert(state.finish(&normal));
    assert(state.finish(&normal));
    assert(!state.finish(&love));
    assert(!state.finish(&normal));
    assert(state.start(&love));
    assert(!state.finish(&love));
}
