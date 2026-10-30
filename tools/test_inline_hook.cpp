#include "inline_hook.h"
#include <cassert>

namespace {
using Function = int (*)(int);
Function original = nullptr;
int target(int value) { return value + 1; }
int replacement(int value) { assert(original); return original(value) + 10; }
}

int main() {
    int installs = 0;
    auto install = [&](void* address, void* proxy, void** output) -> void* {
        ++installs;
        assert(address == reinterpret_cast<void*>(target));
        // The real engine can enter the replacement before install returns.
        assert(output == reinterpret_cast<void**>(&original));
        *output = address;
        assert(reinterpret_cast<Function>(proxy)(3) == 14);
        return address;
    };
    assert(localify::installInlineHook(reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(replacement), original, install));
    assert(original(1) == 2 && installs == 1);
    assert(!localify::installInlineHook(nullptr, reinterpret_cast<void*>(replacement), original, install));
    assert(!localify::installInlineHook(reinterpret_cast<void*>(target), nullptr, original, install));
    assert(installs == 1);
    Function missing = nullptr;
    auto failure = [](void*, void*, void**) -> void* { return nullptr; };
    assert(!localify::installInlineHook(reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(replacement), missing, failure));
    assert(!missing);
    auto noTrampoline = [](void* address, void*, void**) { return address; };
    assert(!localify::installInlineHook(reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(replacement), missing, noTrampoline));
}
