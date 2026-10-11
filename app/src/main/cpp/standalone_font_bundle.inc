// Loaded on the Unity thread from a font-routing callback. The module delivers
// this bundle before nativeInstall; it is never downloaded or replaced in play.
void* loadStandaloneReplacementFont(bool report, bool rounded = false) {
    struct BundleState { uint32_t handle = 0; std::chrono::steady_clock::time_point retryAt{}; };
    static BundleState states[2];
    auto& bundleHandle = states[rounded ? 1 : 0].handle;
    auto& retryAt = states[rounded ? 1 : 0].retryAt;
    const auto path = rounded ? standaloneFontBundlePath.substr(0, standaloneFontBundlePath.rfind('/') + 1)
        + "love-rounded.bundle" : standaloneFontBundlePath;
    const char* key = rounded ? "localify-loveroundedsc" : "localify-defaultcjk";
    const char* fontName = rounded ? "Localify-LoveRoundedSC" : "Localify-DefaultCJK";
    const auto now = std::chrono::steady_clock::now();
    if (now < retryAt || standaloneFontBundlePath.empty() || !runtimeInvoke ||
        !stringNew || !classGetType || !typeGetObject || !gcHandleNew ||
        !gcHandleGetTarget || !objectGetClass || !classIsAssignableFrom) return nullptr;
    retryAt = now + std::chrono::seconds(10);
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return nullptr;
    const auto size = input.tellg();
    char header[8]{};
    input.seekg(0);
    input.read(header, sizeof(header));
    if (size < 8 || size > 64 * 1024 * 1024 || !input || std::memcmp(header, "UnityFS\0", 8) != 0) {
        if (report) LOGE("Standalone font bundle has invalid header or size");
        return nullptr;
    }
    static const auto loadFile = methodInfo("UnityEngine.AssetBundleModule.dll", "UnityEngine",
        "AssetBundle", "LoadFromFile", 1);
    static const auto loadMemory = methodInfo("UnityEngine.AssetBundleModule.dll", "UnityEngine",
        "AssetBundle", "LoadFromMemory", 1);
    static const auto loadAsset = []() {
        const auto* method = methodInfo("UnityEngine.AssetBundleModule.dll", "UnityEngine",
            "AssetBundle", "LoadAsset", 2);
        return method ? method : methodInfo("UnityEngine.AssetBundleModule.dll", "UnityEngine",
            "AssetBundle", "LoadAsset_Internal", 2);
    }();
    static const auto unload = methodInfo("UnityEngine.AssetBundleModule.dll", "UnityEngine",
        "AssetBundle", "Unload", 1);
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
        "Object", "get_name", 0);
    static void* fontClass = findClass("UnityEngine.TextRenderingModule.dll", "UnityEngine", "Font");
    if ((!loadFile && !loadMemory) || !loadAsset || !unload || !getName || !fontClass) {
        if (report) LOGE("Standalone font methods unavailable: file=%d memory=%d asset=%d unload=%d name=%d font=%d",
            loadFile != nullptr, loadMemory != nullptr, loadAsset != nullptr, unload != nullptr, getName != nullptr, fontClass != nullptr);
        return nullptr;
    }
    void* exception = nullptr;
    void* bundle = bundleHandle ? gcHandleGetTarget(bundleHandle) : nullptr;
    if (!bundle) {
        if (loadFile) {
            void* args[] = {stringNew(path.c_str())};
            bundle = runtimeInvoke(loadFile, nullptr, args, &exception);
        } else {
            // IL2CPP may strip the unused file overload; the game already uses
            // this memory overload for Octo bundles.
            void* byteClass = findClass("mscorlib.dll", "System", "Byte");
            if (!arrayNew || !byteClass) return nullptr;
            auto* bytes = arrayNew(byteClass, static_cast<size_t>(size));
            if (!bytes || bytes->length != static_cast<size_t>(size)) return nullptr;
            input.clear();
            input.seekg(0);
            if (!input.read(reinterpret_cast<char*>(bytes->data), static_cast<size_t>(size))) return nullptr;
            void* args[] = {bytes};
            bundle = runtimeInvoke(loadMemory, nullptr, args, &exception);
        }
        if (exception || !bundle) {
            if (report) LOGE("Unity rejected standalone font bundle");
            return nullptr;
        }
        bundleHandle = gcHandleNew(bundle, false);
    }
    // Asset identity is independent of the selected family and weight.
    void* args[] = {stringNew(key),
                    typeGetObject(classGetType(fontClass))};
    void* font = runtimeInvoke(loadAsset, bundle, args, &exception);
    bool valid = !exception && font && classIsAssignableFrom(fontClass, objectGetClass(font));
    if (valid) {
        auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, font, nullptr, &exception));
        valid = !exception && utf8(name) == fontName;
    }
    if (valid && setCachedFontRetention(font, true)) {
        auto& fontHandle = rounded ? loadedRoundedFontHandle : loadedReplacementFontHandle;
        fontHandle = gcHandleNew(font, false);
        if (fontHandle) {
            LOGI("Loaded standalone replacement font %s (%lld bytes)", fontName, static_cast<long long>(size));
            return font;
        }
    }
    // Do not leave an invalid bundle loaded: Unity would reject the next retry
    // as a duplicate. No objects from this failed load have been given to UI.
    bool destroy = true;
    void* unloadArgs[] = {&destroy};
    exception = nullptr;
    runtimeInvoke(unload, bundle, unloadArgs, &exception);
    if (bundleHandle && gcHandleFree) gcHandleFree(bundleHandle);
    bundleHandle = 0;
    if (report) LOGE("Standalone bundle did not provide a usable replacement Font");
    return nullptr;
}
