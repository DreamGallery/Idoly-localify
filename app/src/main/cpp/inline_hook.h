#pragma once

#include <type_traits>

namespace localify {

// ShadowHook publishes the trampoline before activating the replacement. Pass
// the proxy's actual storage, not a temporary copied after the hook returns.
// The caller owns this storage for the entire lifetime of the installed hook.
template <typename Function, typename Installer>
bool installInlineHook(void* target, void* replacement, Function& original, Installer install) {
    static_assert(std::is_pointer_v<Function> &&
                  std::is_function_v<std::remove_pointer_t<Function>>);
    static_assert(sizeof(Function) == sizeof(void*));
    if (!target || !replacement) return false;
    void* stub = install(target, replacement, reinterpret_cast<void**>(&original));
    return stub && original;
}

} // namespace localify
