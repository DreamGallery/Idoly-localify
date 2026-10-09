// Runs when Unity submits a UI texture. The original texture object and sprite UVs
// are retained; restarting with translation disabled reloads untouched APK data.
struct RuntimeAtlas {
    std::string file;
    int width;
    int height;
};
std::unordered_map<std::string, RuntimeAtlas> runtimeAtlases;
std::unordered_map<void*, uint32_t> inspectedTextures;
std::string runtimeImageRoot;
using CanvasSetTextureFn = void (*)(void*, void*, void*);
CanvasSetTextureFn origCanvasSetTexture = nullptr;

void loadRuntimeImages(const std::string& root) {
    runtimeImageRoot = root;
    try {
        std::ifstream file(root + "/manifest.json");
        if (!file) return;
        auto manifest = nlohmann::json::parse(file);
        if (manifest.at("schema_version") != 1 || !manifest.at("textures").is_array() ||
            manifest.at("textures").size() > 64) return;
        decltype(runtimeAtlases) next;
        for (const auto& texture : manifest.at("textures")) {
            const auto name = texture.at("name").get<std::string>();
            const auto png = texture.at("file").get<std::string>();
            const int width = texture.at("width"), height = texture.at("height");
            if (name.empty() || name.size() > 256 || png.find('/') != std::string::npos ||
                png.find('\\') != std::string::npos || png.find("..") != std::string::npos ||
                png.size() > 64 || png.size() < 5 || png.substr(png.size()-4) != ".png" ||
                width < 1 || height < 1 || width > 4096 || height > 4096 || next.count(name))
                throw std::runtime_error("Invalid runtime image metadata");
            next.emplace(name, RuntimeAtlas{png, width, height});
        }
        runtimeAtlases.swap(next);
    } catch (const std::exception&) { LOGE("Runtime image manifest unavailable"); }
}

void patchRuntimeTexture(void* texture) {
    if (!texture || runtimeAtlases.empty() || !gcHandleNewWeak || !gcHandleGetTarget || !gcHandleFree) return;
    auto previous = inspectedTextures.find(texture);
    if (previous != inspectedTextures.end()) {
        if (gcHandleGetTarget(previous->second) == texture) return;
        gcHandleFree(previous->second);
        inspectedTextures.erase(previous);
    }
    // Avoid retaining dead Unity wrappers or unbounded negative cache entries.
    if (inspectedTextures.size() >= 2048) {
        for (const auto& item : inspectedTextures) gcHandleFree(item.second);
        inspectedTextures.clear();
    }
    const auto weak = gcHandleNewWeak(texture, false);
    if (!weak) return;
    inspectedTextures.emplace(texture, weak);
    static void* textureClass = findClass("UnityEngine.CoreModule.dll", "UnityEngine", "Texture2D");
    if (!textureClass || !objectGetClass || !classIsAssignableFrom ||
        !classIsAssignableFrom(textureClass, objectGetClass(texture))) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getWidth = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Texture", "get_width", 0);
    static const auto getHeight = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Texture", "get_height", 0);
    static const auto load = methodInfo("UnityEngine.ImageConversionModule.dll", "UnityEngine", "ImageConversion", "LoadImage", 3);
    if (!getName || !getWidth || !getHeight || !load || !runtimeInvoke || !objectUnbox || !arrayNew) return;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, texture, nullptr, &error));
    if (error || !name) return;
    auto found = runtimeAtlases.find(utf8(name));
    if (found == runtimeAtlases.end()) return;
    void* width = runtimeInvoke(getWidth, texture, nullptr, &error);
    if (error || !width) return;
    const int actualWidth = *static_cast<int*>(objectUnbox(width));
    void* height = runtimeInvoke(getHeight, texture, nullptr, &error);
    if (error || !height || actualWidth != found->second.width ||
        *static_cast<int*>(objectUnbox(height)) != found->second.height) return;
    std::ifstream input(runtimeImageRoot + "/" + found->second.file, std::ios::binary | std::ios::ate);
    if (!input) return;
    const auto size = input.tellg();
    if (size <= 8 || size > 32 * 1024 * 1024) return;
    input.seekg(0);
    void* byteClass = findClass("mscorlib.dll", "System", "Byte");
    if (!byteClass) return;
    auto* bytes = arrayNew(byteClass, static_cast<size_t>(size));
    if (!bytes || bytes->length != static_cast<size_t>(size) ||
        !input.read(reinterpret_cast<char*>(bytes->data), size)) return;
    // Discard the CPU pixel copy after upload. The source PNG remains on disk.
    bool nonReadable = true;
    void* args[] = {texture, bytes, &nonReadable};
    void* result = runtimeInvoke(load, nullptr, args, &error);
    if (!error && result && *static_cast<bool*>(objectUnbox(result)))
        LOGI("Runtime UI atlas replaced: %s", found->first.c_str());
    else LOGE("Runtime UI atlas load failed: %s", found->first.c_str());
}

void hookCanvasSetTexture(void* self, void* texture, void* method) {
    static thread_local bool updating = false;
    if (!updating) {
        updating = true;
        try { patchRuntimeTexture(texture); }
        catch (const std::exception&) { LOGE("Runtime image replacement unavailable; retaining original texture"); }
        updating = false;
    }
    origCanvasSetTexture(self, texture, method);
}
