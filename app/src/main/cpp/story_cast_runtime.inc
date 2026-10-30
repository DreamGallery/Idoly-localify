// Redirect this page's own asset lookup, so the game supplies matching tags,
// portraits, descriptions and click targets. Never mutate shared MasterDB data.
using StoryCastChanged = void (*)(void*, Il2CppString*, void*);
StoryCastChanged origStoryCastChanged = nullptr;
const MethodInfoHead* storyCastChangedMethod = nullptr;
struct StoryCastPage {
    uint32_t weak;
    uint64_t generation;
    std::string requested, rendered;
};
std::unordered_map<void*, StoryCastPage> storyCastPages;

void hookStoryCastChanged(void* self, Il2CppString* asset, void* method) {
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    if (!self || !asset || !stringNew || !gcHandleNewWeak || !gcHandleGetTarget || !gcHandleFree) {
        origStoryCastChanged(self, asset, method);
        return;
    }
    const std::string requested = utf8(asset);
    auto it = storyCastPages.find(self);
    if (it != storyCastPages.end() && gcHandleGetTarget(it->second.weak) != self) {
        gcHandleFree(it->second.weak);
        storyCastPages.erase(it);
        it = storyCastPages.end();
    }
    if (it == storyCastPages.end()) {
        for (auto old = storyCastPages.begin(); old != storyCastPages.end();) {
            if (!gcHandleGetTarget(old->second.weak)) {
                gcHandleFree(old->second.weak);
                old = storyCastPages.erase(old);
            } else ++old;
        }
        if (storyCastPages.size() < 16) {
            const auto weak = gcHandleNewWeak(self, false);
            if (weak) it = storyCastPages.emplace(self, StoryCastPage{weak, 0, {}, {}}).first;
        }
    }
    auto replacement = activeStorySwapAssets.castAsset(requested);
    if (!replacement.empty() && !bindStorySwapScreen(self)) replacement.clear();
    const auto rendered = replacement.empty() ? requested : replacement;
    if (it != storyCastPages.end())
        it->second = {it->second.weak, storySwapPlaybackGeneration.load(), requested, rendered};
    if (!replacement.empty())
        LOGI("Story cast: %s -> %s", requested.c_str(), replacement.c_str());
    origStoryCastChanged(self, replacement.empty() ? asset : stringNew(replacement.c_str()), method);
}

void refreshStorySwapCast() {
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    if (!storyCastChangedMethod || !runtimeInvoke || !stringNew || !gcHandleGetTarget || !gcHandleFree) return;
    std::vector<std::pair<uint32_t, std::string>> pending;
    for (auto it = storyCastPages.begin(); it != storyCastPages.end();) {
        auto& page = it->second;
        void* target = gcHandleGetTarget(page.weak);
        if (!target || target != it->first) {
            gcHandleFree(page.weak);
            it = storyCastPages.erase(it);
            continue;
        }
        if (page.generation != storySwapPlaybackGeneration) {
            page.generation = storySwapPlaybackGeneration;
            const auto replacement = activeStorySwapAssets.castAsset(page.requested);
            const auto desired = replacement.empty() ? page.requested : replacement;
            if (desired != page.rendered && bindStorySwapScreen(target))
                pending.emplace_back(page.weak, page.requested);
        }
        ++it;
    }
    // Page initialization can precede resource delivery. Rebuild once on the
    // Unity canvas thread when that successful delivery publishes its generation.
    for (const auto& item : pending) {
        void* target = gcHandleGetTarget(item.first);
        if (!target) continue;
        void* args[] = {stringNew(item.second.c_str())};
        void* exception = nullptr;
        runtimeInvoke(storyCastChangedMethod, target, args, &exception);
        if (exception) LOGE("Story cast refresh failed; retaining game-owned page");
    }
}

void installStorySwapCastHook() {
    const auto* method = methodInfo("Assembly-CSharp.dll", "Solis.OutGame",
                                  "StoryCharactersPage", "OnStoryChanged", 1);
    if (!method || !method->methodPointer || !methodGetFlags || !methodGetParamCount ||
        !methodGetReturnType || !methodGetParam || !typeGetName || !il2cppFree) return;
    uint32_t implementationFlags = 0;
    if ((methodGetFlags(method, &implementationFlags) & 0x0010u) || methodGetParamCount(method) != 1) return;
    char* result = typeGetName(methodGetReturnType(method));
    char* parameter = typeGetName(methodGetParam(method, 0));
    const bool valid = result && parameter && std::strcmp(result, "System.Void") == 0 &&
                       std::strcmp(parameter, "System.String") == 0;
    if (result) il2cppFree(result);
    if (parameter) il2cppFree(parameter);
    if (!valid) return;
    void* stub = shadowhook_hook_func_addr(method->methodPointer,
        reinterpret_cast<void*>(hookStoryCastChanged), reinterpret_cast<void**>(&origStoryCastChanged));
    if (!stub || !origStoryCastChanged) {
        if (stub) shadowhook_unhook(stub);
        LOGE("Story cast hook unavailable");
        return;
    }
    storyCastChangedMethod = method;
    LOGI("StoryCharactersPage.OnStoryChanged hook installed");
}
