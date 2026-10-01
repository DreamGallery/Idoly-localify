#pragma once
#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include <mutex>
#include "story_visibility_state.h"

namespace story_bridge {
namespace detail {
using Lifecycle = void (*)(void*, void*);
using Transition = void (*)(void*, void*, void*);
inline Lifecycle playOriginal = nullptr, clearOriginal = nullptr, endOriginal = nullptr;
inline JavaVM* vm = nullptr;
inline jclass entry = nullptr;
inline jmethodID callback = nullptr;
inline bool (*isEngine)(void*) = nullptr;
inline void* (*presenterEngine)(void*) = nullptr;
inline std::recursive_mutex mutex;
inline PlaybackState state;
inline bool delivered = false, installed = false;

inline void notify(bool visible, const char* reason) {
    if (visible == delivered || !vm || !entry || !callback) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (status != JNI_OK) return;
    env->CallStaticVoidMethod(entry, callback, visible ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        __android_log_print(ANDROID_LOG_ERROR, "IdolyLocalify", "Story visibility callback failed");
    } else {
        delivered = visible;
        __android_log_print(ANDROID_LOG_INFO, "IdolyLocalify", "Story visibility=%d reason=%s", visible, reason);
    }
    if (attached) vm->DetachCurrentThread();
}
inline void update(void* self, bool start, const char* reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    notify(start ? state.start(self) : state.finish(self), reason);
}
inline void play(void* self, void* method) {
    update(self, true, "ADVEngine.OnPlay");
    playOriginal(self, method);
}
inline void clear(void* self, void* method) {
    update(self, false, "ADVEngine.OnClear");
    clearOriginal(self, method);
}
inline void end(void* self, void* method) {
    if (isEngine && isEngine(self)) update(self, false, "Engine.End");
    endOriginal(self, method);
}
inline void leave(void* self) {
    void* engine = presenterEngine ? presenterEngine(self) : nullptr;
    if (engine) update(engine, false, "story presenter exit/destroy");
}
// Distinct slots preserve each original even when concrete generic bases have
// distinct native bodies. Shared bodies are installed once by address.
inline void* exitOriginals[8] = {};
template<int N> void exitTransition(void* self, void* info, void* method) {
    leave(self);
    reinterpret_cast<Transition>(exitOriginals[N])(self, info, method);
}
template<int N> void exitDestroy(void* self, void* method) {
    leave(self);
    reinterpret_cast<Lifecycle>(exitOriginals[N])(self, method);
}
} // namespace detail

inline bool initialize(JNIEnv* env, jclass entry) {
    if (detail::entry) return true;
    if (!env || !entry || env->GetJavaVM(&detail::vm) != JNI_OK) return false;
    detail::entry = static_cast<jclass>(env->NewGlobalRef(entry));
    if (!detail::entry) return false;
    detail::callback = env->GetStaticMethodID(detail::entry, "onStoryVisibilityChanged", "(Z)V");
    if (!detail::callback) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteGlobalRef(detail::entry);
        detail::entry = nullptr;
        return false;
    }
    return true;
}

// Lookup verifies exact runtime return, instance/static flag and parameter
// types against the v31 metadata before returning a native method pointer.
template<typename Lookup>
bool install(Lookup lookup, bool (*isEngine)(void*), void* (*presenterEngine)(void*)) {
    using namespace detail;
    if (installed) return true;
    if (!callback || !isEngine || !presenterEngine) return false;
    detail::isEngine = isEngine;
    detail::presenterEngine = presenterEngine;
    void* playTarget = lookup("ADV.Runtime.dll", "Solis.ADV", "ADVEngine", "OnPlay", 0);
    void* clearTarget = lookup("ADV.Runtime.dll", "Solis.ADV", "ADVEngine", "OnClear", 0);
    void* endTarget = lookup("Uguiss.Runtime.dll", "Uguiss", "Engine", "End", 0);
    if (!playTarget || !clearTarget || !endTarget || playTarget == clearTarget ||
        playTarget == endTarget || clearTarget == endTarget) {
        __android_log_print(ANDROID_LOG_WARN, "IdolyLocalify", "Story lifecycle unavailable; automatic hiding disabled");
        return false;
    }
    auto hook = [](void* target, void* replacement, void** original) -> void* {
        void* stub = shadowhook_hook_func_addr(target, replacement, original);
        if (stub && !*original) { shadowhook_unhook(stub); return nullptr; }
        return stub;
    };
    // Install recovery first; no start callback can hide the icon if recovery
    // cannot be hooked. Originals are published by shadowhook before activation.
    void* clearStub = hook(clearTarget, reinterpret_cast<void*>(clear), reinterpret_cast<void**>(&clearOriginal));
    if (!clearStub) return false;
    void* endStub = hook(endTarget, reinterpret_cast<void*>(end), reinterpret_cast<void**>(&endOriginal));
    if (!endStub) { shadowhook_unhook(clearStub); return false; }
    void* playStub = hook(playTarget, reinterpret_cast<void*>(play), reinterpret_cast<void**>(&playOriginal));
    if (!playStub) { shadowhook_unhook(endStub); shadowhook_unhook(clearStub); return false; }
    void* transitionHooks[] = {
        reinterpret_cast<void*>(exitTransition<0>), reinterpret_cast<void*>(exitTransition<1>),
        reinterpret_cast<void*>(exitTransition<2>), reinterpret_cast<void*>(exitTransition<3>),
        reinterpret_cast<void*>(exitTransition<4>), reinterpret_cast<void*>(exitTransition<5>),
        reinterpret_cast<void*>(exitTransition<6>), reinterpret_cast<void*>(exitTransition<7>)};
    void* destroyHooks[] = {
        reinterpret_cast<void*>(exitDestroy<0>), reinterpret_cast<void*>(exitDestroy<1>),
        reinterpret_cast<void*>(exitDestroy<2>), reinterpret_cast<void*>(exitDestroy<3>),
        reinterpret_cast<void*>(exitDestroy<4>), reinterpret_cast<void*>(exitDestroy<5>),
        reinterpret_cast<void*>(exitDestroy<6>), reinterpret_cast<void*>(exitDestroy<7>)};
    void* hookedTargets[8] = {};
    int exitCount = 0;
    for (const char* klass : {"StoryPlayerScreenPresenter", "StoryPreviewPlayerScreenPresenter",
                              "LoveAdvPlayerScreenPresenter", "VerticalAdvPlayerScreenPresenter"}) {
        for (const char* name : {"OnWillOut", "OnDestroy"}) {
            const int count = name[2] == 'W' ? 1 : 0;
            void* target = lookup("Assembly-CSharp.dll", "Solis.OutGame", klass, name, count);
            if (!target || target == playTarget || target == clearTarget || target == endTarget) continue;
            bool duplicate = false;
            for (int i = 0; i < exitCount; ++i) if (hookedTargets[i] == target) duplicate = true;
            if (duplicate) continue;
            if (hook(target, count ? transitionHooks[exitCount] : destroyHooks[exitCount], &exitOriginals[exitCount]))
                hookedTargets[exitCount++] = target;
        }
    }
    installed = true;
    __android_log_print(ANDROID_LOG_INFO, "IdolyLocalify", "Story lifecycle hooks ready: OnPlay/OnClear/End exitHooks=%d", exitCount);
    return true;
}
} // namespace story_bridge
