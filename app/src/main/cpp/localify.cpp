#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <shadowhook.h>
#include <xdl.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <codecvt>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <chrono>
#include <sys/stat.h>
#include <locale>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "json.hpp"
#include "runtime/dictionary.h"
#include "master_blob_matcher.h"
#include "story_swap.h"
#include "gacha_pickup_layout.h"
#include "guild_medal_font.h"
#include "love_font_policy.h"
#include "story_punctuation.h"
#include "shop_exchange_description_matcher.h"
#include "story_bridge.h"
#include "font_profile_config.h"
#include "font_label_geometry.h"
#include "font_outline_geometry.h"
#include "story_outline_font.h"
#include "mixed_font_router.h"
#include "love_episode_label.h"
#include "panorama_font_labels.h"
#include "ui_font_policy.h"
#include "constrained_font_metrics.h"
#include "font_prewarm_coverage.h"
#include "tmp_renderer_submit.h"
#include "capture_session_state.h"

namespace {
constexpr const char* TAG = "IdolyLocalify";
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

struct Il2CppString {
    void* klass;
    void* monitor;
    int32_t length;
    char16_t chars[0];
};

struct Il2CppByteArray {
    void* klass;
    void* monitor;
    void* bounds;
    size_t length;
    uint8_t data[0];
};

struct MethodInfoHead {
    void* methodPointer;
};

using DomainGet = void* (*)();
using DomainGetAssemblies = void** (*)(void*, size_t*);
using AssemblyGetImage = void* (*)(void*);
using ImageGetName = const char* (*)(void*);
using ClassFromName = void* (*)(void*, const char*, const char*);
using ClassGetMethodFromName = const MethodInfoHead* (*)(void*, const char*, int);
using ClassGetFieldFromName = void* (*)(void*, const char*);
using FieldGetValue = void (*)(void*, void*, void*);
using FieldGetValueObject = void* (*)(void*, void*);
using FieldSetValue = void (*)(void*, void*, void*);
using ClassGetMethods = const MethodInfoHead* (*)(void*, void**);
using MethodGetName = const char* (*)(const MethodInfoHead*);
using MethodGetParamCount = uint32_t (*)(const MethodInfoHead*);
using MethodGetParam = const void* (*)(const MethodInfoHead*, uint32_t);
using MethodGetReturnType = const void* (*)(const MethodInfoHead*);
using MethodGetFlags = uint32_t (*)(const MethodInfoHead*, uint32_t*);
using TypeGetName = char* (*)(const void*);
using ClassIsAssignableFrom = bool (*)(void*, void*);
using Il2CppFree = void (*)(void*);
using ObjectGetClass = void* (*)(void*);
using ClassGetName = const char* (*)(void*);
using ClassGetType = const void* (*)(void*);
using TypeGetObject = void* (*)(const void*);
using StringNew = Il2CppString* (*)(const char*);
using RuntimeInvoke = void* (*)(const MethodInfoHead*, void*, void**, void**);
using GcHandleNew = uint32_t (*)(void*, bool);
using GcHandleGetTarget = void* (*)(uint32_t);
using GcHandleFree = void (*)(uint32_t);
using ObjectUnbox = void* (*)(void*);
using ArrayNew = Il2CppByteArray* (*)(void*, size_t);

void* il2cppHandle = nullptr;
DomainGet domainGet = nullptr;
DomainGetAssemblies domainGetAssemblies = nullptr;
AssemblyGetImage assemblyGetImage = nullptr;
ImageGetName imageGetName = nullptr;
ClassFromName classFromName = nullptr;
ClassGetMethodFromName classGetMethodFromName = nullptr;
ClassGetFieldFromName classGetFieldFromName = nullptr;
FieldGetValue fieldGetValue = nullptr;
FieldGetValueObject fieldGetValueObject = nullptr;
FieldSetValue fieldSetValue = nullptr;
ClassGetMethods classGetMethods = nullptr;
MethodGetName methodGetName = nullptr;
MethodGetParamCount methodGetParamCount = nullptr;
MethodGetParam methodGetParam = nullptr;
MethodGetReturnType methodGetReturnType = nullptr;
MethodGetFlags methodGetFlags = nullptr;
TypeGetName typeGetName = nullptr;
ClassIsAssignableFrom classIsAssignableFrom = nullptr;
Il2CppFree il2cppFree = nullptr;
ObjectGetClass objectGetClass = nullptr;
ClassGetName classGetName = nullptr;
ClassGetName classGetNamespace = nullptr;
ClassGetType classGetType = nullptr;
TypeGetObject typeGetObject = nullptr;
StringNew stringNew = nullptr;
RuntimeInvoke runtimeInvoke = nullptr;
GcHandleNew gcHandleNew = nullptr;
GcHandleGetTarget gcHandleGetTarget = nullptr;
GcHandleNew gcHandleNewWeak = nullptr;
GcHandleFree gcHandleFree = nullptr;
ObjectUnbox objectUnbox = nullptr;
ArrayNew arrayNew = nullptr;

localify::DictionaryRuntime translations;
using localify::FontMode;
struct ResolvedI18nValue {
    uint32_t weakHandle;
    std::string key;
    std::string source;
};
std::unordered_map<Il2CppString*, ResolvedI18nValue> resolvedI18nStrings;
thread_local Il2CppString* resolvingI18n = nullptr;
struct TrackedText {
    uint32_t weakHandle;
    std::string source;
    std::string rendered;
    std::string i18nKey;
    uint64_t generation;
};
std::unordered_map<void*, TrackedText> trackedTexts;
std::unordered_set<std::string> dumped;
std::mutex dumpMutex;
std::string dumpPath;
std::string resourceRoot;
std::string assetRoot;
std::string octoSettingsPath;
std::atomic<bool> octoCaptured{false};
std::mutex octoCaptureMutex;
uint32_t fallbackFontAssetHandle = 0;
uint32_t loadedFontHandle = 0;
uint32_t loadedReplacementFontHandle = 0;
uint32_t loadedRoundedFontHandle = 0;
std::string standaloneFontBundlePath;
struct ReplacementFontPair { uint32_t replacementHandle; uint32_t originalHandle; };
std::unordered_map<void*, ReplacementFontPair> labelReplacementAssets;
std::unordered_map<void*, ReplacementFontPair> pickerReplacementAssets;
std::unordered_map<void*, ReplacementFontPair> storyReplacementAssets;
struct ConstrainedTitleFont { uint32_t weakHandle, fontHandle; uint64_t signature; size_t length; std::vector<uint32_t> retiredHandles; };
std::unordered_map<void*, ConstrainedTitleFont> constrainedTitleFonts;
std::unordered_map<void*, void*> labelOriginalFonts;
struct OriginalLabelStyle { uint32_t weakHandle; uint32_t materialHandle; bool pendingRestoreMaterial = false; };
std::unordered_map<int32_t, OriginalLabelStyle> originalLabelStyles;
struct OriginalLegacyFont { uint32_t weakHandle; uint32_t fontHandle; };
std::unordered_map<void*, OriginalLegacyFont> originalLegacyFonts;
bool usesIndependentFontRouting() {
    return translations.options().fontMode == FontMode::PreserveNonCjk || translations.options().fontMode == FontMode::ReplaceUi;
}
bool replacesCjk() { return translations.options().fontMode == FontMode::ReplaceAll || usesIndependentFontRouting(); }
std::unordered_set<void*> updatedFontAssets;
std::recursive_mutex fontMutex;
struct OriginalTextFont {
    uint32_t fontHandle;
    uint32_t materialHandle;
};
std::unordered_map<int32_t, OriginalTextFont> originalTextFonts;
std::unordered_map<void*, uint32_t> retainedFontObjects;
std::unordered_set<int32_t> preserveOriginalFontObjects;
std::atomic<bool> hooksInstalled{false};
std::atomic<bool> installerCalled{false};

FontProfileConfig fontProfiles;
std::mutex profileMutex;
std::atomic<uint64_t> fontProfileGeneration{1};
float profileValue(const char* id, const char* field) {
    std::lock_guard<std::mutex> lock(profileMutex);
    return fontProfiles.value(id, field);
}
bool profileFlag(const char* id, const char* field) {
    std::lock_guard<std::mutex> lock(profileMutex);
    return fontProfiles.flag(id, field);
}
std::vector<float> profileVector(const char* id, const char* field) {
    std::lock_guard<std::mutex> lock(profileMutex);
    return fontProfiles.adjust(id).at(field).get<std::vector<float>>();
}
// Weak identity prevents an address reused by a new Unity label from inheriting
// the previous label's original anchor/size. Access is on the Unity main thread.
struct FontBaseline {
    uint32_t weak;
    std::unordered_map<std::string, float> values;
    uint64_t generation = 0;
    std::string fittedText;
    uint64_t fittedGeneration = 0;
    bool pendingRendererSubmit = false;
    bool glyphFontRouteKnown = false;
    bool glyphNumericSymbols = false;
    void* storyOutlineMaterial = nullptr;
    uint64_t storyOutlineGeneration = 0;
};
std::unordered_map<void*, FontBaseline> fontBaselines;
FontBaseline* fontBaseline(void* object) {
    if (!object || !gcHandleNewWeak || !gcHandleGetTarget || !gcHandleFree) return nullptr;
    static size_t lookups = 0;
    if (++lookups % 256 == 0 && fontBaselines.size() > 1024) {
        for (auto old = fontBaselines.begin(); old != fontBaselines.end();) {
            if (!gcHandleGetTarget(old->second.weak)) {
                gcHandleFree(old->second.weak);
                old = fontBaselines.erase(old);
            } else ++old;
        }
    }
    auto it = fontBaselines.find(object);
    if (it != fontBaselines.end() && gcHandleGetTarget(it->second.weak) != object) {
        gcHandleFree(it->second.weak); fontBaselines.erase(it); it = fontBaselines.end();
    }
    if (it == fontBaselines.end()) {
        const auto weak = gcHandleNewWeak(object, false);
        if (!weak) return nullptr;
        it = fontBaselines.emplace(object, FontBaseline{weak, {}, 0}).first;
    }
    return &it->second;
}
float originalFontValue(void* object, const char* key, float current) {
    auto* baseline = fontBaseline(object);
    return baseline ? baseline->values.try_emplace(key, current).first->second : current;
}
float knownFontValue(void* object, const char* key, float current) {
    auto* baseline = fontBaseline(object);
    if (!baseline) return current;
    const auto found = baseline->values.find(key);
    return found == baseline->values.end() ? current : found->second;
}
std::atomic<bool> textCaptureEnabled{false};
// A full/failed session remains stoppable/exportable, but no longer pays per-
// frame hierarchy or JSON costs. Session state itself stays under dumpMutex.
std::atomic<bool> captureAcceptingRecords{false};
bool captureWritesPaused = false; // guarded by dumpMutex
std::atomic<bool> legacyDumpSuppressed{false};
std::string textCapturePath;
std::unordered_set<std::string> capturedThisSession;
CaptureSessionState captureSession;
std::atomic<uint64_t> layoutCaptureGeneration{0};
std::unordered_map<void*, uint64_t> layoutCaptured;
std::string layoutCapturePath;

using InitFn = int (*)(const char*);
using TextFn = void (*)(void*, Il2CppString*, void*);
using EnableFn = void (*)(void*, void*);
using SetTextBoolFn = void (*)(void*, Il2CppString*, bool, void*);
using LegacyTextFn = void (*)(void*, Il2CppString*, void*);
using I18nFn = void (*)(void*, Il2CppString*, Il2CppString*, void*);
using OctoSetupFn = bool (*)(void*, bool, void*);
using ResourceLoadFn = void (*)(void*, Il2CppString*, void*, void*, void*);
using AssetLoadFn = void (*)(void*, Il2CppString*, void*, void*, void*);
using GetBlobFn = void (*)(void*, int32_t, void**, int32_t*, void*);
using CanvasWillRenderFn = void (*)(void*);
using SetArraySizesFn = int32_t (*)(void*, void*, void*);
using GetCharacterFn = void* (*)(uint32_t, void*, bool, int32_t, int32_t, bool*, void*);
GetCharacterFn origGetCharacter = nullptr;
thread_local void* currentStoryPunctuationFont = nullptr;
void* storyPunctuationFont(void* self);
struct StoryPunctuationScope {
    void* previous;
    explicit StoryPunctuationScope(void* self) : previous(currentStoryPunctuationFont) {
        currentStoryPunctuationFont = storyPunctuationFont(self);
    }
    ~StoryPunctuationScope() { currentStoryPunctuationFont = previous; }
};
using FitLayoutFn = void (*)(void*, int32_t, void*);

InitFn origInit = nullptr;
TextFn origTmpSetText = nullptr;
EnableFn origTmpOnEnable = nullptr;
EnableFn origTmpPreRender = nullptr;
FitLayoutFn origFitToChildLayout = nullptr;
SetArraySizesFn origTmpUiSetArraySizes = nullptr;
SetArraySizesFn origTmp3dSetArraySizes = nullptr;
TextFn origTmpSetTextOne = nullptr;
SetTextBoolFn origTmpSetTextTwo = nullptr;
LegacyTextFn origLegacySetText = nullptr;
I18nFn origI18nSetValue = nullptr;
OctoSetupFn origOctoSetup = nullptr;
ResourceLoadFn origResourceLoad = nullptr;
AssetLoadFn origAssetLoad = nullptr;
GetBlobFn origGetBlob = nullptr;
CanvasWillRenderFn origCanvasWillRender = nullptr;
MasterBlobMatcher masterBlobPatches;
std::atomic<uint32_t> seenMasterBlobs{0};
std::atomic<uint32_t> matchedMasterBlobs{0};

void updateFont(void* textObject, bool preserveOriginalFont = false, const bool* replacementOverride = nullptr, bool pickerMetrics = false,
                const mixed_font_router::UnicodeChar* titleBuffer = nullptr, size_t titleCapacity = 0, bool keepMetricVariant = false);
void updateLegacyFont(void* textObject, Il2CppString* rendered);
void routeTmpFontAfterSetter(void* textObject);
void collectConstrainedTitleFonts();
void synchronizeTmpRendererMaterial(void* self);
void restoreStoryNameColor(void* textObject);
void applyStorySubtitleLayout(void* textObject);
void applyPortraitNameLayout(void* textObject);
void refreshProfileMaterials();
void* findClass(const char* imageName, const char* nameSpace, const char* className);
void* readFontProperty(void* object, const char* assembly, const char* ns, const char* type, const char* name);
bool unityObjectAlive(void* object);
FontVerticalMetrics verticalMetrics(void* font);
// Registered only from the TextMeshProUGUI lifecycle hooks. All access is on
// Unity's rendering thread; weak handles never retain retired scene labels.
std::unordered_map<void*, uint32_t> registeredTmpUi;
std::vector<void*> canvasDirtyQueue;
size_t canvasDirtyCursor = 0;
uint64_t canvasTextGeneration = 1;
uint64_t canvasFontGeneration = 1;
uint64_t canvasCaptureGeneration = 0;

bool isTmpUi(void* self) {
    if (!self || !objectGetClass || !classIsAssignableFrom) return false;
    static void* uiClass = findClass("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI");
    return uiClass && classIsAssignableFrom(uiClass, objectGetClass(self));
}

void registerTmpUi(void* self) {
    if (!isTmpUi(self) || !gcHandleNewWeak || !gcHandleGetTarget || !gcHandleFree) return;
    auto it = registeredTmpUi.find(self);
    if (it != registeredTmpUi.end()) {
        if (gcHandleGetTarget(it->second) == self) return;
        gcHandleFree(it->second);
        registeredTmpUi.erase(it);
    }
    const uint32_t weak = gcHandleNewWeak(self, false);
    if (weak) registeredTmpUi.emplace(self, weak);
    static size_t registrations = 0;
    if (++registrations % 256 == 0 && registeredTmpUi.size() > 1024) {
        for (auto old = registeredTmpUi.begin(); old != registeredTmpUi.end();) {
            if (!gcHandleGetTarget(old->second)) {
                gcHandleFree(old->second);
                old = registeredTmpUi.erase(old);
            } else ++old;
        }
    }
}
bool materialFloat(void* material, const char* property, float& value);
void* materialTexture(void* material, const char* property);
void* findClass(const char* imageName, const char* nameSpace, const char* className);
const MethodInfoHead* methodInfo(const char* imageName, const char* nameSpace,
                                 const char* className, const char* methodName,
                                 int parameterCount);

std::string utf8(const Il2CppString* value) {
    if (!value || value->length <= 0 || value->length > 1024 * 1024) return {};
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
        return conv.to_bytes(value->chars, value->chars + value->length);
    } catch (const std::range_error&) {
        return {};
    }
}

std::string textObjectPath(void* object) {
    if (!object || !runtimeInvoke) return {};
    static const auto getTransform = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    if (!getTransform || !getParent || !getName) return {};
    void* error = nullptr;
    void* transform = runtimeInvoke(getTransform, object, nullptr, &error);
    std::string result;
    for (int depth = 0; transform && !error && depth < 16; ++depth) {
        auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, transform, nullptr, &error));
        if (error) break;
        auto part = utf8(name);
        if (part.size() > 256) part.resize(256);
        result = part + (result.empty() ? "" : "/" + result);
        transform = runtimeInvoke(getParent, transform, nullptr, &error);
    }
    return result;
}

void dump(const char* kind, const std::string& source, const std::string& key = {}, void* textObject = nullptr) {
    std::lock_guard<std::recursive_mutex> dictionaryLock(translations.frameMutex());
    if (((!translations.options().dumpEnabled || legacyDumpSuppressed.load()) && !captureAcceptingRecords.load()) || source.empty() || source.size() > 1024 * 1024 ||
        translations.catalog().translatedValues.find(source) != translations.catalog().translatedValues.end()) return;
    std::lock_guard<std::mutex> lock(dumpMutex);
    const std::string identity = std::string(kind) + "\n" + key + "\n" + source;
    if (captureWritesPaused) return;
    const bool writeDump = translations.options().dumpEnabled && !legacyDumpSuppressed.load() && dumped.count(identity) == 0;
    const bool writeCapture = captureAcceptingRecords.load() && capturedThisSession.count(identity) == 0;
    // Most repeated setters have already been captured. Avoid hierarchy walks
    // and JSON serialization for records neither destination needs again.
    if (!writeDump && !writeCapture) return;
    nlohmann::json item = {{"kind", kind}, {"source", source}};
    if (!key.empty()) item["key"] = key;
    if (writeCapture && textObject) item["object_path"] = textObjectPath(textObject);
    const auto encoded = item.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    if (writeDump) {
        dumped.insert(identity);
        std::ofstream output(dumpPath, std::ios::app);
        if (output) output << encoded;
    }
    if (writeCapture) {
        const bool written = captureSession.append(false, encoded, [&](const std::string& record) {
            std::ofstream capture(textCapturePath, std::ios::app);
            if (!capture) return false;
            capture << record;
            capture.flush();
            capture.close();
            return !capture.fail();
        });
        if (written) capturedThisSession.insert(identity);
        else if (captureSession.truncated || captureSession.writeFailed)
            captureAcceptingRecords.store(false);
    }
}

// Separate local diagnostic file: translated labels also need their object
// path/rect to fix alignment, but must never be imported as translation input.
void captureTextLayout(void* self) {
    if (!captureAcceptingRecords.load() || !self || !runtimeInvoke || !objectUnbox) return;
    const auto generation = layoutCaptureGeneration.load();
    static uint64_t lastGeneration = 0;
    if (lastGeneration != generation) { layoutCaptured.clear(); lastGeneration = generation; }
    if (layoutCaptured[self] == generation) return;
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    static const auto getAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment", 0);
    static const auto getRectTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
    static const auto getPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
    if (!getText || !getSize || !getAlignment || !getRectTransform || !getRect || !getPosition) return;
    void* error = nullptr;
    auto* value = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error));
    auto text = utf8(value);
    if (error || text.empty() || text.size() > 1048576) return;
    void* transform = runtimeInvoke(getRectTransform, self, nullptr, &error);
    void* size = runtimeInvoke(getSize, self, nullptr, &error);
    void* alignment = runtimeInvoke(getAlignment, self, nullptr, &error);
    void* rect = transform ? runtimeInvoke(getRect, transform, nullptr, &error) : nullptr;
    void* position = transform ? runtimeInvoke(getPosition, transform, nullptr, &error) : nullptr;
    if (error || !size || !alignment || !rect || !position) return;
    auto* r = static_cast<float*>(objectUnbox(rect));
    auto* xy = static_cast<float*>(objectUnbox(position));
    nlohmann::json item = {{"kind", "layout"}, {"text", text}, {"object_path", textObjectPath(self)},
        {"font_size", *static_cast<float*>(objectUnbox(size))},
        {"alignment", *static_cast<int*>(objectUnbox(alignment))},
        {"rect", {r[0], r[1], r[2], r[3]}}, {"anchored_position", {xy[0], xy[1]}}};
    // Capture final layout metrics with the user's diagnostic session. These
    // explain font-driven clipping and auto-sizing without a UI hierarchy scan.
    const auto property = [&](const char* name) {
        return readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", name);
    };
    if (void* value = property("get_enableAutoSizing")) item["auto_size"] = *static_cast<bool*>(objectUnbox(value));
    if (void* value = property("get_fontSizeMin")) item["font_size_min"] = *static_cast<float*>(objectUnbox(value));
    if (void* value = property("get_fontSizeMax")) item["font_size_max"] = *static_cast<float*>(objectUnbox(value));
    if (void* value = property("get_overflowMode")) item["overflow_mode"] = *static_cast<int32_t*>(objectUnbox(value));
    if (void* value = property("get_characterSpacing")) item["character_spacing"] = *static_cast<float*>(objectUnbox(value));
    if (void* value = property("get_wordSpacing")) item["word_spacing"] = *static_cast<float*>(objectUnbox(value));
    if (void* value = property("GetParsedText")) item["rendered_text"] = utf8(static_cast<Il2CppString*>(value));
    if (void* value = property("get_textBounds")) {
        const auto* v = static_cast<float*>(objectUnbox(value));
        item["text_bounds_center"] = {v[0], v[1], v[2]};
        item["text_bounds_extents"] = {v[3], v[4], v[5]};
    }
    const auto metrics = [&](void* font) {
        const auto m = verticalMetrics(font);
        return nlohmann::json{{"point_size", m.pointSize}, {"scale", m.scale},
            {"ascent", m.ascent}, {"descent", m.descent},
            {"line_height", fontLineHeight(item["font_size"].get<float>(), m)}};
    };
    if (void* font = property("get_font")) {
        item["font_metrics"] = metrics(font);
        const auto original = labelOriginalFonts.find(font);
        if (original != labelOriginalFonts.end()) item["original_font_metrics"] = metrics(original->second);
    }
    const auto encoded = item.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    std::lock_guard<std::mutex> lock(dumpMutex);
    if (captureAcceptingRecords.load() && generation == layoutCaptureGeneration.load()) {
        const bool written = captureSession.append(true, encoded, [&](const std::string& record) {
            std::ofstream out(layoutCapturePath, std::ios::app);
            if (!out) return false;
            out << record;
            out.flush();
            out.close();
            return !out.fail();
        });
        if (written) layoutCaptured[self] = generation;
        else if (captureSession.truncated || captureSession.writeFailed)
            captureAcceptingRecords.store(false);
    }
}

// Playback metadata belongs to one successful resource delivery, not merely
// to a saved alias. All accesses share translations.frameMutex() with the UI hooks.
StorySwapPlaybackTitles activeStorySwapTitles;
StorySwapPlaybackAssets activeStorySwapAssets;
std::atomic<uint64_t> storySwapPlaybackGeneration{1};
uint64_t canvasStorySwapGeneration = 1;
uint32_t storySwapScreenWeak = 0;
struct StorySwapLabel { uint32_t weak; uint64_t generation; std::string original, rendered; };
std::unordered_map<void*, StorySwapLabel> storySwapLabels;

uint64_t resetStorySwapPlayback() {
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    activeStorySwapTitles = {};
    activeStorySwapAssets = {};
    ++storySwapPlaybackGeneration;
    for (auto it = storySwapLabels.begin(); it != storySwapLabels.end();) {
        if (gcHandleGetTarget && gcHandleFree && !gcHandleGetTarget(it->second.weak)) {
            gcHandleFree(it->second.weak);
            it = storySwapLabels.erase(it);
        } else ++it;
    }
    if (storySwapScreenWeak && gcHandleFree) gcHandleFree(storySwapScreenWeak);
    storySwapScreenWeak = 0;
    return storySwapPlaybackGeneration.load();
}

bool bindStorySwapScreen(void* self) {
    if (!runtimeInvoke || !gcHandleNewWeak || !gcHandleGetTarget || !gcHandleFree) return false;
    static const auto getTransform = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    if (!getTransform || !getParent || !getName) return false;
    void* error = nullptr;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    for (int depth = 0; transform && !error && depth < 8; ++depth) {
        auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, transform, nullptr, &error));
        if (!error && utf8(name) == "StoryPlayerScreen(Clone)") {
            if (!storySwapScreenWeak) storySwapScreenWeak = gcHandleNewWeak(transform, false);
            return storySwapScreenWeak && gcHandleGetTarget(storySwapScreenWeak) == transform;
        }
        transform = runtimeInvoke(getParent, transform, nullptr, &error);
    }
    return false;
}

#include "story_cast_runtime.inc"

Il2CppString* applyStorySwapTitle(void* self, Il2CppString* text) {
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    if (!self || !text || !stringNew || !gcHandleGetTarget || !gcHandleFree || !gcHandleNewWeak) return text;
    auto found = storySwapLabels.find(self);
    std::string current = utf8(text);
    if (found != storySwapLabels.end()) {
        auto& tracked = found->second;
        if (gcHandleGetTarget(tracked.weak) == self && current == tracked.rendered) {
            if (tracked.generation == storySwapPlaybackGeneration && activeStorySwapTitles.ready() &&
                storySwapTitleRole(textObjectPath(self)) && bindStorySwapScreen(self)) return text;
            current = tracked.original;
            text = stringNew(current.c_str());
        }
        gcHandleFree(tracked.weak);
        storySwapLabels.erase(found);
    }
    if (!activeStorySwapTitles.ready()) return text;
    const auto replacement = storySwapTitleReplacement(activeStorySwapTitles, textObjectPath(self), current);
    if (replacement.empty() || replacement == current || !bindStorySwapScreen(self)) return text;
    const uint32_t weak = gcHandleNewWeak(self, false);
    if (!weak) return text;
    storySwapLabels.emplace(self, StorySwapLabel{weak, storySwapPlaybackGeneration, current, replacement});
    return stringNew(replacement.c_str());
}

void refreshStorySwapTitle(void* self) {
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    if ((!activeStorySwapTitles.ready() && storySwapLabels.empty()) || !runtimeInvoke || !origTmpSetText) return;
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    static const auto setText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_text", 1);
    if (!getText || !setText) return;
    void* error = nullptr;
    auto* current = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error));
    if (error || !current) return;
    auto* replacement = applyStorySwapTitle(self, current);
    if (replacement == current || !replacement) return;
    auto tracked = trackedTexts.find(self);
    if (tracked != trackedTexts.end() && tracked->second.rendered == utf8(current))
        tracked->second.rendered = utf8(replacement);
    origTmpSetText(self, replacement, const_cast<MethodInfoHead*>(setText));
    routeTmpFontAfterSetter(self);
}

Il2CppString* translateText(void* textObject, Il2CppString* original) {
    std::lock_guard<std::recursive_mutex> dictionaryLock(translations.frameMutex());
    if (!original) return original;
    const std::string source = utf8(original);
    if (source.empty()) return original;
    // The I18n key has already resolved this particular string, including
    // decisions to retain Japanese. Never run it through a context-free map
    // again. Weak handles prevent reused addresses from inheriting this tag.
    bool resolved = resolvingI18n == original;
    const auto tagged = resolvedI18nStrings.find(original);
    if (!resolved && tagged != resolvedI18nStrings.end() && gcHandleGetTarget)
        resolved = gcHandleGetTarget(tagged->second.weakHandle) == original;
    // Preserve these English labels verbatim. The panorama glyph route and
    // paired layout below apply its requested replacement font independently of text.
    if (source == "360°LIVE" || source == "360°" || source == "LIVE") return original;
    // This message-picker prompt is drawn by a compact masked button. Replacing
    // its font asset made the label disappear in the old replace-all build.
    // Keep that compatibility exception there; whole-label routing uses replacement.
    const bool messagePickerPrompt = translations.options().fontMode == FontMode::ReplaceAll &&
        (source == "选择消息" || source == "メッセージを選択");
    if (replacesCjk()) updateFont(textObject, messagePickerPrompt);
    if (replacesCjk() && textObject && runtimeInvoke && objectUnbox) {
        applyPortraitNameLayout(textObject);
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
        static const auto getRectTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
        static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
        static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
        if (getName && getSize && getRectTransform && getRect && getParent) {
            void* exception = nullptr;
            auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, textObject, nullptr, &exception));
            if (!exception && utf8(name) == "Message Text") {
                // The replacement font uses taller line metrics
                // than the game's FOT font. Keep portrait dialogue at the
                // original screen-space baseline interval.
                static const auto getLineSpacing = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_lineSpacing", 0);
                static const auto setLineSpacing = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_lineSpacing", 1);
                if (getLineSpacing && setLineSpacing) {
                    void* sizeBox = runtimeInvoke(getSize, textObject, nullptr, &exception);
                    const float size = sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
                    void* transform = runtimeInvoke(getRectTransform, textObject, nullptr, &exception);
                    void* rectBox = transform ? runtimeInvoke(getRect, transform, nullptr, &exception) : nullptr;
                    void* parent = transform ? runtimeInvoke(getParent, transform, nullptr, &exception) : nullptr;
                    auto* parentName = parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &exception)) : nullptr;
                    const auto* rect = rectBox ? static_cast<float*>(objectUnbox(rectBox)) : nullptr;
                    if (!exception && rect && utf8(parentName) == "Message Root" &&
                        std::fabs(size - 40.0f) < 1.0f &&
                        std::fabs(rect[2] - 804.0f) < 1.0f &&
                        std::fabs(rect[3] - 168.0f) < 1.0f) {
                        void* spacingBox = runtimeInvoke(getLineSpacing, textObject, nullptr, &exception);
                        const float spacing = spacingBox ? *static_cast<float*>(objectUnbox(spacingBox)) : 0.0f;
                        const float baseSpacing = knownFontValue(textObject, "portrait-line", spacing);
                        if (!exception && baseSpacing > 27.0f && baseSpacing < 30.0f) {
                            originalFontValue(textObject, "portrait-line", spacing);
                            float closer = baseSpacing + profileValue("portrait_love_event_dialogue", "line_spacing_delta_unity");
                            void* args[] = {&closer};
                            runtimeInvoke(setLineSpacing, textObject, args, &exception);
                            if (exception) LOGE("Cannot set portrait story line spacing");
                        }
                        static const auto getAnchor = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
                        static const auto setAnchor = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "set_anchoredPosition", 1);
                        if (!exception && getAnchor && setAnchor) {
                            void* anchorBox = runtimeInvoke(getAnchor, transform, nullptr, &exception);
                            const auto* anchor = anchorBox ? static_cast<float*>(objectUnbox(anchorBox)) : nullptr;
                            if (!exception && anchor) {
                                const float baseY = originalFontValue(textObject, "portrait-y", anchor[1]);
                                const float offset = profileValue("portrait_love_event_dialogue", "anchored_y_delta_unity");
                                // One canvas unit is about 1.4 pixels here.
                                // Lift the first line by the measured 13 pixels.
                                if (std::fabs(anchor[1] - (baseY + offset)) > 0.5f) {
                                    float lifted[] = {anchor[0], baseY + offset};
                                    void* args[] = {lifted};
                                    runtimeInvoke(setAnchor, transform, args, &exception);
                                    if (exception) LOGE("Cannot raise portrait story text");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (replacesCjk() && textObject && runtimeInvoke && objectUnbox) {
        // Home bond talks use a fixed 800 x 190 dialogue box. The replacement
        // replacement font appears larger than the game's FOT glyphs at its
        // original 48-point setting. Restrict the adjustment to this prefab.
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
        static const auto setSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSize", 1);
        static const auto getRectTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
        static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
        static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
        if (getName && getSize && setSize && getRectTransform && getRect && getParent) {
            void* exception = nullptr;
            auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, textObject, nullptr, &exception));
            if (!exception && utf8(name) == "Serif") {
                void* transform = runtimeInvoke(getRectTransform, textObject, nullptr, &exception);
                void* rectBox = transform ? runtimeInvoke(getRect, transform, nullptr, &exception) : nullptr;
                void* parent = transform ? runtimeInvoke(getParent, transform, nullptr, &exception) : nullptr;
                auto* parentName = parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &exception)) : nullptr;
                void* grandparent = parent ? runtimeInvoke(getParent, parent, nullptr, &exception) : nullptr;
                auto* grandparentName = grandparent ? static_cast<Il2CppString*>(runtimeInvoke(getName, grandparent, nullptr, &exception)) : nullptr;
                const auto* rect = rectBox ? static_cast<float*>(objectUnbox(rectBox)) : nullptr;
                if (!exception && rect && utf8(parentName) == "SerifWIndow" &&
                    utf8(grandparentName) == "Bottom" &&
                    std::fabs(rect[2] - 800.0f) < 1.0f &&
                    std::fabs(rect[3] - 190.0f) < 1.0f) {
                    void* sizeBox = runtimeInvoke(getSize, textObject, nullptr, &exception);
                    const float size = sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
                    const float baseSize = knownFontValue(textObject, "hometalk-size", size);
                    if (!exception && std::fabs(baseSize - 48.0f) < 1.0f) {
                        originalFontValue(textObject, "hometalk-size", size);
                        float smaller = baseSize * profileValue("home_talk_dialogue", "font_size_ratio");
                        void* args[] = {&smaller};
                        runtimeInvoke(setSize, textObject, args, &exception);
                        if (exception) LOGE("Cannot size bond talk dialogue");
                        else LOGI("Sized bond talk dialogue 48 -> 42");
                    }
                    // TMP keeps the first line at the same screen position for
                    // one through four lines. Lift every dialogue equally so
                    // four lines have balanced margins without shifting the
                    // first line when the line count changes.
                    static const auto getAnchor = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
                                                             "RectTransform", "get_anchoredPosition", 0);
                    static const auto setAnchor = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
                                                             "RectTransform", "set_anchoredPosition", 1);
                    static const auto getInstanceId = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
                                                                 "Object", "GetInstanceID", 0);
                    if (!exception && getAnchor && setAnchor && getInstanceId) {
                        void* idBox = runtimeInvoke(getInstanceId, textObject, nullptr, &exception);
                        void* anchorBox = runtimeInvoke(getAnchor, transform, nullptr, &exception);
                        const auto* anchor = anchorBox ? static_cast<float*>(objectUnbox(anchorBox)) : nullptr;
                        if (!exception && idBox && anchor) {
                            const int32_t id = *static_cast<int32_t*>(objectUnbox(idBox));
                            (void)id;
                            const float baseY = originalFontValue(textObject, "hometalk-y", anchor[1]);
                            const float desiredY = baseY + profileValue("home_talk_dialogue", "anchored_y_delta_unity");
                            if (std::fabs(anchor[1] - desiredY) > 0.5f) {
                                float position[] = {anchor[0], desiredY};
                                void* args[] = {position};
                                runtimeInvoke(setAnchor, transform, args, &exception);
                                if (exception) LOGE("Cannot position bond talk dialogue");
                                else LOGI("Positioned bond talk dialogue: offset=%.1f",
                                          desiredY - baseY);
                            }
                        }
                    }
                }
            }
        }
    }
    // Typography/layout rules above apply to resolved I18n labels too. Only
    // the context-free translation below is bypassed.
    if (resolved) {
        if (!replacesCjk() && translations.catalog().translatedValues.count(source))
            updateFont(textObject, messagePickerPrompt);
        return original;
    }
    std::function<std::string()> objectPath;
    if (textObject) objectPath = [&] { return textObjectPath(textObject); };
    const auto decision = translations.catalog().resolveText(source, objectPath);
    if (decision.matched && (decision.retainOriginal || stringNew)) {
        if (decision.playerTemplate && translations.options().fontEnabled && textObject) {
            static std::atomic<int> reported{0};
            if (reported.fetch_add(1) < 4)
                LOGI("Matched rendered player-name text for font replacement");
        }
        if (decision.updateFont) updateFont(textObject, messagePickerPrompt);
        return decision.retainOriginal ? original : stringNew(decision.text.c_str());
    }
    const auto trackedShop = trackedTexts.find(textObject);
    if (!resolved && trackedShop != trackedTexts.end() && source == trackedShop->second.rendered &&
        ShopExchangeDescriptionMatcher::mayMatch(trackedShop->second.source) && gcHandleGetTarget &&
        ShopExchangeDescriptionMatcher::preserveTrackedSource(
            gcHandleGetTarget(trackedShop->second.weakHandle) == textObject, false,
            trackedShop->second.source, trackedShop->second.rendered, source, source, textObjectPath(textObject))) {
        updateFont(textObject, messagePickerPrompt);
        return original;
    }
    dump("text", source, {}, textObject);
    return original;
}

void relayoutBondTalkName(void* textObject) {
    if (!textObject || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    static thread_local bool rebuilding = false;
    if (rebuilding) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    static const auto getRectTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto rebuild = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "LayoutRebuilder", "ForceRebuildLayoutImmediate", 1);
    if (!getName || !getSize || !getRectTransform || !getRect || !getParent || !rebuild) return;
    void* exception = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, textObject, nullptr, &exception));
    if (exception || utf8(name) != "Name") return;
    void* transform = runtimeInvoke(getRectTransform, textObject, nullptr, &exception);
    void* parent = transform ? runtimeInvoke(getParent, transform, nullptr, &exception) : nullptr;
    auto* parentName = parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &exception)) : nullptr;
    void* grandparent = parent ? runtimeInvoke(getParent, parent, nullptr, &exception) : nullptr;
    auto* grandparentName = grandparent ? static_cast<Il2CppString*>(runtimeInvoke(getName, grandparent, nullptr, &exception)) : nullptr;
    void* sizeBox = runtimeInvoke(getSize, textObject, nullptr, &exception);
    void* rectBox = transform ? runtimeInvoke(getRect, transform, nullptr, &exception) : nullptr;
    const auto* rect = rectBox ? static_cast<float*>(objectUnbox(rectBox)) : nullptr;
    if (exception || !sizeBox || !rect || utf8(parentName) != "NameHeader" ||
        utf8(grandparentName) != "Bottom" ||
        std::fabs(*static_cast<float*>(objectUnbox(sizeBox)) - 40.0f) > 1.0f ||
        std::fabs(rect[3] - 57.9f) > 1.0f) return;
    if (rect[2] >= profileValue("home_talk_name", "rebuild_parent_layout_below_width_unity")) return;
    const float before = rect[2];
    rebuilding = true;
    void* args[] = {parent};
    runtimeInvoke(rebuild, nullptr, args, &exception);
    if (exception) {
        rebuilding = false;
        LOGE("Cannot rebuild bond talk name layout");
        return;
    }
    rectBox = runtimeInvoke(getRect, transform, nullptr, &exception);
    rect = rectBox ? static_cast<float*>(objectUnbox(rectBox)) : nullptr;
    if (!exception && rect) LOGI("Rebuilt bond talk name width %.1f -> %.1f", before, rect[2]);
    rebuilding = false;
}

// UI labels can keep an already resolved I18n string across screen visits.
// Remember untranslated UI labels too: newly added translations must refresh
// existing pages. Weak handles do not keep retired Unity labels alive.
void rememberTranslatedText(void* self, Il2CppString* input, Il2CppString* output) {
    if (textCaptureEnabled.load()) layoutCaptured.erase(self);
    if (!self || !input || !output || !gcHandleNewWeak || !gcHandleGetTarget || !gcHandleFree)
        return;
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    const auto resolved = resolvedI18nStrings.find(input);
    const bool keyed = resolved != resolvedI18nStrings.end() &&
        gcHandleGetTarget(resolved->second.weakHandle) == input;
    const std::string source = keyed ? resolved->second.source : utf8(input);
    const std::string rendered = utf8(output);
    auto old = trackedTexts.find(self);
    if (old != trackedTexts.end() && !keyed && source == old->second.rendered &&
        rendered == old->second.rendered && ShopExchangeDescriptionMatcher::mayMatch(old->second.source) &&
        ShopExchangeDescriptionMatcher::preserveTrackedSource(
            gcHandleGetTarget(old->second.weakHandle) == self, keyed, old->second.source,
            old->second.rendered, source, rendered, textObjectPath(self))) {
        // Repeated rendered-value setters must not erase this scoped dynamic
        // description's original source. Changed dates/body/keys remain new inputs.
        return;
    }
    if (old != trackedTexts.end()) {
        gcHandleFree(old->second.weakHandle);
        trackedTexts.erase(old);
    }
    if (source.empty()) return;
    // Bound additional source-only tracking. Large non-UI text stays outside
    // the Canvas refresh path; existing localized tracking is unchanged.
    if (!keyed && source == rendered &&
        (!isTmpUi(self) || source.size() > 65536 || trackedTexts.size() >= 8192)) return;
    const uint32_t handle = gcHandleNewWeak(self, false);
    if (!handle) return;
    trackedTexts.emplace(self, TrackedText{handle, source, rendered,
        keyed ? resolved->second.key : std::string(), keyed ? 0 : translations.generation()});
    if (trackedTexts.size() > 4096) {
        for (auto it = trackedTexts.begin(); it != trackedTexts.end();) {
            if (!gcHandleGetTarget(it->second.weakHandle)) {
                gcHandleFree(it->second.weakHandle);
                it = trackedTexts.erase(it);
            } else ++it;
        }
    }
}

void refreshTranslatedText(void* self) {
    if (!self || !runtimeInvoke || !origTmpSetText || !stringNew) return;
    const uint64_t generation = translations.generation();
    if (generation == 1) return;
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    auto it = trackedTexts.find(self);
    if (it == trackedTexts.end() || it->second.generation == generation) return;
    if (!gcHandleGetTarget || gcHandleGetTarget(it->second.weakHandle) != self) {
        if (gcHandleFree) gcHandleFree(it->second.weakHandle);
        trackedTexts.erase(it);
        return;
    }
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    static const auto setText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_text", 1);
    if (!getText || !setText) return;
    void* exception = nullptr;
    auto* current = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &exception));
    if (exception || !current) return;
    if (utf8(current) != it->second.rendered) {
        // The game changed this label through another path. Do not overwrite it.
        if (gcHandleFree) gcHandleFree(it->second.weakHandle);
        trackedTexts.erase(it);
        return;
    }
    std::string desired = it->second.source;
    if (!it->second.i18nKey.empty()) {
        const auto* selected = translations.catalog().resolveI18n(
            it->second.i18nKey, it->second.source);
        if (selected) desired = *selected;
    }
    auto* replacement = stringNew(desired.c_str());
    if (!replacement) return;
    if (it->second.i18nKey.empty()) {
        // Translate the original source exactly once. A translated value may
        // also appear as an unrelated source key in the context-free map.
        replacement = translateText(self, replacement);
    } else {
        auto* previous = resolvingI18n;
        resolvingI18n = replacement;
        replacement = translateText(self, replacement);
        resolvingI18n = previous;
    }
    if (utf8(replacement) != it->second.rendered) {
        origTmpSetText(self, replacement, const_cast<MethodInfoHead*>(setText));
        routeTmpFontAfterSetter(self);
        restoreStoryNameColor(self);
        it->second.rendered = utf8(replacement);
        static std::atomic<int> reported{0};
        if (reported.fetch_add(1) < 20) LOGI("Refreshed translated TMP label after dictionary reload");
    }
    it->second.generation = generation;
}

void hookTmpSetText(void* self, Il2CppString* value, void* method) {
    auto* translated = applyStorySwapTitle(self, translateText(self, value));
    rememberTranslatedText(self, value, translated);
    origTmpSetText(self, translated, method);
    routeTmpFontAfterSetter(self);
    restoreStoryNameColor(self);
    relayoutBondTalkName(self);
}

void hookTmpSetTextOne(void* self, Il2CppString* value, void* method) {
    auto* translated = applyStorySwapTitle(self, translateText(self, value));
    rememberTranslatedText(self, value, translated);
    origTmpSetTextOne(self, translated, method);
    routeTmpFontAfterSetter(self);
    restoreStoryNameColor(self);
    relayoutBondTalkName(self);
}

void hookTmpSetTextTwo(void* self, Il2CppString* value, bool syncInput, void* method) {
    auto* translated = applyStorySwapTitle(self, translateText(self, value));
    rememberTranslatedText(self, value, translated);
    origTmpSetTextTwo(self, translated, syncInput, method);
    routeTmpFontAfterSetter(self);
    restoreStoryNameColor(self);
    relayoutBondTalkName(self);
}

// OnPreRenderCanvas is called only for dirty UI elements. Schedule a bounded
// batch before Unity dispatches willRenderCanvases, so static labels are added
// to this frame's graphic rebuild queue after a data/profile/capture change.
void hookCanvasWillRender(void* method) {
    collectConstrainedTitleFonts();
    const uint64_t textGeneration = translations.generation();
    const uint64_t fontGeneration = fontProfileGeneration.load(std::memory_order_acquire);
    const uint64_t captureGeneration = layoutCaptureGeneration.load(std::memory_order_acquire);
    const uint64_t storyGeneration = storySwapPlaybackGeneration.load(std::memory_order_acquire);
    const bool storyChanged = storyGeneration != canvasStorySwapGeneration;
    if (storyChanged) refreshStorySwapCast();
    const bool textChanged = textGeneration != canvasTextGeneration;
    const bool fontChanged = fontGeneration != canvasFontGeneration;
    const bool captureChanged = captureGeneration != canvasCaptureGeneration &&
        textCaptureEnabled.load(std::memory_order_acquire);
    if (storyChanged || textChanged || fontChanged || captureGeneration != canvasCaptureGeneration) {
        std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
        canvasDirtyQueue.clear();
        canvasDirtyCursor = 0;
        for (auto it = registeredTmpUi.begin(); it != registeredTmpUi.end();) {
            void* target = gcHandleGetTarget ? gcHandleGetTarget(it->second) : nullptr;
            if (!target || target != it->first) {
                if (gcHandleFree) gcHandleFree(it->second);
                it = registeredTmpUi.erase(it);
                continue;
            }
            const auto tracked = trackedTexts.find(target);
            const bool needsText = textChanged && tracked != trackedTexts.end() &&
                tracked->second.generation != textGeneration;
            const bool needsFont = fontChanged && fontBaselines.find(target) != fontBaselines.end();
            if (needsText || needsFont || captureChanged || storyChanged) canvasDirtyQueue.push_back(target);
            ++it;
        }
        canvasStorySwapGeneration = storyGeneration;
        canvasTextGeneration = textGeneration;
        canvasFontGeneration = fontGeneration;
        canvasCaptureGeneration = captureGeneration;
        if (fontChanged) refreshProfileMaterials();
        LOGI("Canvas refresh queued %zu labels: text=%d font=%d capture=%d",
             canvasDirtyQueue.size(), textChanged, fontChanged, captureChanged);
    }
    static const auto dirty = methodInfo("Unity.TextMeshPro.dll", "TMPro",
        "TextMeshProUGUI", "SetVerticesDirty", 0);
    if (dirty && runtimeInvoke && gcHandleGetTarget) {
        constexpr size_t kMaxPerCanvasPass = 128;
        size_t processed = 0;
        while (canvasDirtyCursor < canvasDirtyQueue.size() && processed < kMaxPerCanvasPass) {
            void* object = canvasDirtyQueue[canvasDirtyCursor++];
            ++processed;
            const auto it = registeredTmpUi.find(object);
            if (it == registeredTmpUi.end() || gcHandleGetTarget(it->second) != object ||
                !isTmpUi(object)) continue;
            void* exception = nullptr;
            runtimeInvoke(dirty, object, nullptr, &exception);
            if (exception) LOGE("Cannot mark localized TMP label dirty");
        }
        if (canvasDirtyCursor == canvasDirtyQueue.size()) {
            canvasDirtyQueue.clear();
            canvasDirtyCursor = 0;
        }
    }
    if (origCanvasWillRender) origCanvasWillRender(method);
}

// Captured labels are fitted before mesh generation, after game layouts.
// Only the preview buttons change bounds; badges retain original geometry.
void fitCapturedButtonLabel(void* self) {
    if (!self || !translations.options().fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    if (!getName || !getTransform || !getParent) return;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    const std::string label = utf8(name);
    if (error || (label != "Label" && label != "Text")) return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* parent = transform ? runtimeInvoke(getParent, transform, nullptr, &error) : nullptr;
    auto* parentName = parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &error)) : nullptr;
    const std::string host = utf8(parentName);
    const char* costumeId = label == "Label" &&
        (host == "HideButton" || host == "ReactionButton" || host == "DetermineButton")
        ? costumeButtonProfile(textObjectPath(self)) : nullptr;
    const char* id = costumeId ? costumeId :
        label == "Text" && host == "Noti" ? "shop_new_arrivals" :
        label == "Label" && host == "FloorLabel" ? "item_floor_badge" :
        label == "Label" && host == "WallLabel" ? "item_wall_badge" : nullptr;
    if (error || !id) return;
    nlohmann::json profile;
    { std::lock_guard<std::mutex> lock(profileMutex); profile = fontProfiles.definition(id); }
    std::string suffix;
    for (const auto& part : profile["path_suffix"]) suffix += "/" + part.get<std::string>();
    const std::string path = "/" + textObjectPath(self);
    if (path.size() < suffix.size() || path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) return;

    static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
    static const auto getPivot = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_pivot", 0);
    static const auto getMin = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchorMin", 0);
    static const auto getMax = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchorMax", 0);
    static const auto getPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
    static const auto getScale = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_localScale", 0);
    static const auto setPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    static const auto setBounds = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "SetSizeWithCurrentAnchors", 2);
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    static const auto setSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSize", 1);
    static const auto getAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment", 0);
    static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
    static const auto getAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_enableAutoSizing", 0);
    static const auto setAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_enableAutoSizing", 1);
    static const auto getMargin = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_margin", 0);
    static const auto setMargin = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_margin", 1);
    static const auto getPreferred = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_preferredWidth", 0);
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    if (!getRect || !getPivot || !getMin || !getMax || !getPosition || !getScale || !setPosition ||
        !setBounds || !getSize || !setSize || !getAlignment || !setAlignment || !getAuto || !setAuto ||
        !getMargin || !setMargin || !getPreferred || !getText) return;
    auto read = [&](const MethodInfoHead* method, void* object, float* destination, size_t count) {
        void* boxed = runtimeInvoke(method, object, nullptr, &error);
        void* raw = !error && boxed ? objectUnbox(boxed) : nullptr;
        if (!raw) return false;
        std::memcpy(destination, raw, count * sizeof(float));
        return std::all_of(destination, destination + count, [](float v) { return std::isfinite(v); });
    };
    float rect[4], parentRect[4], pivot[2], anchorsMin[2], anchorsMax[2], position[2], scale[3], margin[4], size;
    if (!read(getRect, transform, rect, 4) || !read(getRect, parent, parentRect, 4) ||
        !read(getPivot, transform, pivot, 2) || !read(getMin, transform, anchorsMin, 2) ||
        !read(getMax, transform, anchorsMax, 2) || !read(getPosition, transform, position, 2) ||
        !read(getScale, transform, scale, 3) || !read(getMargin, self, margin, 4) || !read(getSize, self, &size, 1) ||
        parentRect[2] < 1 || parentRect[3] < 1 || std::fabs(scale[0] - 1) > 0.001f) return;
    auto* baseline = fontBaseline(self);
    if (!baseline) return;
    const auto& guard = profile["guard"];
    const float originalSize = originalFontValue(self, "captured_size", size);
    const float originalWidth = originalFontValue(self, "captured_width", rect[2]);
    const float originalHeight = originalFontValue(self, "captured_height", rect[3]);
    if (originalSize < guard["original_font_size_unity"][0].get<float>() ||
        originalSize > guard["original_font_size_unity"][1].get<float>() ||
        std::fabs(originalWidth - guard["rect_size_unity"][0].get<float>()) > 1 ||
        std::fabs(originalHeight - guard["rect_size_unity"][1].get<float>()) > 1) return;
    // The shop badge participates in the game's animated layout. Preserve its
    // original text bounds/position instead of feeding its parent's changing
    // width back into the child. Only the preview buttons have fixed parents.
    const bool itemBadge = std::strcmp(id, "item_floor_badge") == 0 ||
        std::strcmp(id, "item_wall_badge") == 0;
    if (itemBadge) {
        auto* value = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error));
        const auto content = utf8(value);
        if (error || (content != "地面" && content != "墙面")) return;
    }
    const bool preserveBounds = itemBadge || std::strcmp(id, "shop_new_arrivals") == 0;
    const float originalParentWidth = originalFontValue(self, "captured_parent_width", parentRect[2]);
    const float width = preserveBounds ? originalWidth :
        parentRect[2] * profile["adjust"]["content_width_ratio"].get<float>();
    const float marginTotal = margin[0] + margin[2];
    const float ratio = profile["adjust"]["content_width_ratio"].get<float>();
    const float available = (itemBadge ? originalWidth * ratio :
        preserveBounds ? std::min(originalWidth, originalParentWidth * ratio) : width) - marginTotal;
    float centeredX = position[0];
    if (available < 1 || (!preserveBounds && !centeredLabelX(parentRect[0], parentRect[2], width,
            anchorsMin[0], anchorsMax[0], pivot[0], centeredX))) return;
    if (!preserveBounds && std::fabs(rect[2] - width) > 0.1f) {
        int axis = 0; float value = width; void* args[] = {&axis, &value};
        runtimeInvoke(setBounds, transform, args, &error);
    }
    if (!preserveBounds && !error && std::fabs(position[0] - centeredX) > 0.1f) {
        float value[] = {centeredX, position[1]}; void* args[] = {value};
        runtimeInvoke(setPosition, transform, args, &error);
    }
    if (!itemBadge && !error && std::fabs(margin[0] - margin[2]) > 0.01f) {
        float value[] = {marginTotal / 2, margin[1], marginTotal / 2, margin[3]}; void* args[] = {value};
        runtimeInvoke(setMargin, self, args, &error);
    }
    void* alignment = !error ? runtimeInvoke(getAlignment, self, nullptr, &error) : nullptr;
    if (!itemBadge && !error && alignment && *static_cast<int*>(objectUnbox(alignment)) != 4098) {
        int centered = 4098; void* args[] = {&centered}; runtimeInvoke(setAlignment, self, args, &error);
    }
    void* automatic = !error ? runtimeInvoke(getAuto, self, nullptr, &error) : nullptr;
    if (!error && automatic && *static_cast<bool*>(objectUnbox(automatic))) {
        bool disabled = false; void* args[] = {&disabled}; runtimeInvoke(setAuto, self, args, &error);
    }
    auto* text = !error ? static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error)) : nullptr;
    if (error || !text) return;
    const auto currentText = utf8(text);
    const auto generation = fontProfileGeneration.load();
    const float maximum = profile["adjust"]["font_size_max_unity"].get<float>();
    auto& values = baseline->values;
    // Re-measure at the configured maximum only when inputs change. Cached fit
    // is reapplied if the game restores fontSize; it never compounds each frame.
    if (baseline->fittedGeneration != generation || baseline->fittedText != currentText ||
        std::fabs(values["fit_available"] - available) > 0.1f || values["fit_maximum"] != maximum) {
        if (std::fabs(size - maximum) > 0.01f) {
            float value = maximum; void* args[] = {&value}; runtimeInvoke(setSize, self, args, &error);
            size = maximum;
        }
        float preferred;
        if (error || !read(getPreferred, self, &preferred, 1) || preferred <= 0) return;
        values["fit_size"] = std::min(maximum, maximum * available / preferred);
        values["fit_available"] = available;
        values["fit_maximum"] = maximum;
        baseline->fittedText = currentText;
        baseline->fittedGeneration = generation;
    }
    const float desired = values["fit_size"];
    if (std::isfinite(desired) && desired > 0 && std::fabs(size - desired) > 0.05f) {
        float value = desired; void* args[] = {&value}; runtimeInvoke(setSize, self, args, &error);
    }
    if (error) LOGE("Cannot fit captured label %s", id);
}

#include "panorama_label_runtime.inc"
#include "gallery_footer_layout.inc"
#include "love_mission_layout.inc"
#include "love_home_layout.inc"
#include "gacha_pickup_layout.inc"
#include "guild_top_layout.inc"
#include "activity_result_layout.inc"

void refreshLoveEpisodeLabel(void* self) {
    if (!self || !runtimeInvoke || !objectUnbox || !origTmpSetText || !stringNew) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    void* error = nullptr;
    auto* name = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error)) : nullptr;
    if (error || utf8(name) != "EpisodeText" || textObjectPath(self) != love_episode_label::path) return;
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    static const auto setText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_text", 1);
    auto* current = getText ? static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error)) : nullptr;
    if (error || !current || !setText) return;
    const auto desired = love_episode_label::format(love_episode_label::path, utf8(current));
    if (!desired) return;
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
    static const auto setBounds = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "SetSizeWithCurrentAnchors", 2);
    if (!getTransform || !getParent || !getRect || !setBounds) return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* parent = !error && transform ? runtimeInvoke(getParent, transform, nullptr, &error) : nullptr;
    void* rectBox = !error && parent ? runtimeInvoke(getRect, parent, nullptr, &error) : nullptr;
    if (error || !rectBox) return;
    const auto* rect = static_cast<float*>(objectUnbox(rectBox));
    if (!rect || !std::isfinite(rect[2]) || !std::isfinite(rect[3]) || rect[2] < 100 || rect[3] < 20) return;
    const float width = rect[2] * 0.96f, height = rect[3];
    // This Image contains the static Japanese words around the numeric child.
    // Disable only the Image: its child text and the separate bubble stay active.
    // This also works with an original atlas, without an image patch dependency.
    void* labelImage = getComponentByType(parent, "UnityEngine.UI.dll", "UnityEngine.UI", "Image");
    static const auto getEnabled = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour", "get_enabled", 0);
    static const auto setEnabled = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Behaviour", "set_enabled", 1);
    if (!labelImage || !getEnabled || !setEnabled) return;
    void* enabledBox = runtimeInvoke(getEnabled, labelImage, nullptr, &error);
    auto* enabled = !error && enabledBox ? static_cast<bool*>(objectUnbox(enabledBox)) : nullptr;
    if (!enabled) return;
    if (*enabled) {
        bool off = false; void* args[] = {&off};
        runtimeInvoke(setEnabled, labelImage, args, &error);
    }
    if (error) return;
    // Read every dirty rebuild: formatted setters may bypass the string setter.
    if (*desired != utf8(current)) origTmpSetText(self, stringNew(desired->c_str()), const_cast<MethodInfoHead*>(setText));
    auto setVector = [&](const char* property, float x, float y) {
        const std::string get = std::string("get_") + property, set = std::string("set_") + property;
        const auto getter = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", get.c_str(), 0);
        const auto setter = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", set.c_str(), 1);
        void* box = getter ? runtimeInvoke(getter, transform, nullptr, &error) : nullptr;
        const auto* previous = !error && box ? static_cast<float*>(objectUnbox(box)) : nullptr;
        if (!previous || !setter) return;
        if (std::fabs(previous[0] - x) < 0.01f && std::fabs(previous[1] - y) < 0.01f) return;
        float value[] = {x, y}; void* args[] = {value}; runtimeInvoke(setter, transform, args, &error);
    };
    setVector("anchorMin", 0.5f, 0.5f); setVector("anchorMax", 0.5f, 0.5f);
    setVector("pivot", 0.5f, 0.5f); setVector("anchoredPosition", 0, 0);
    rectBox = runtimeInvoke(getRect, transform, nullptr, &error);
    if (error || !rectBox) return;
    const auto* childRect = static_cast<float*>(objectUnbox(rectBox));
    const float dimensions[] = {width, height};
    for (int32_t axis = 0; axis < 2; ++axis) if (std::fabs(childRect[axis + 2] - dimensions[axis]) > 0.01f) {
        float value = dimensions[axis]; void* args[] = {&axis, &value}; runtimeInvoke(setBounds, transform, args, &error);
    }
    const auto setProperty = [&](const char* property, auto desiredValue) {
        const std::string get = std::string("get_") + property, set = std::string("set_") + property;
        const auto getter = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", get.c_str(), 0);
        const auto setter = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", set.c_str(), 1);
        void* box = getter ? runtimeInvoke(getter, self, nullptr, &error) : nullptr;
        if (!error && box && setter && *static_cast<decltype(desiredValue)*>(objectUnbox(box)) != desiredValue) {
            void* args[] = {&desiredValue}; runtimeInvoke(setter, self, args, &error);
        }
    };
    setProperty("alignment", int32_t(514));
    setProperty("enableWordWrapping", false);
    setProperty("fontSizeMin", 24.0f); setProperty("fontSizeMax", 32.0f);
    setProperty("enableAutoSizing", true);
    if (error) LOGE("Cannot layout complete Love episode label");
}

// Most objects need no ancestor walk. Only native design exceptions and
// friend identity fields can override the default/numeric policy by path.
std::string uiFontPolicyPath(void* self) {
    if (!self || !runtimeInvoke) return {};
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    void* error = nullptr;
    auto* name = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error)) : nullptr;
    return !error && ui_font_policy::needsPath(utf8(name)) ? textObjectPath(self) : std::string{};
}

void refreshDefaultUiFont(void* self) {
    if (!self || !translations.options().fontEnabled || translations.options().fontMode != FontMode::ReplaceUi || !runtimeInvoke) return;
    const auto path = uiFontPolicyPath(self);
    const auto found = fontBaselines.find(self);
    auto* baseline = found != fontBaselines.end() ? fontBaseline(self) : nullptr;
    const bool glyphKnown = baseline && baseline->glyphFontRouteKnown;
    bool getterKnown = false, getterNumeric = false;
    if (!glyphKnown && !ui_font_policy::keepsOriginalDesign(path) && !ui_font_policy::isFriendIdentity(path)) {
        static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
        void* error = nullptr;
        auto* text = getText ? static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error)) : nullptr;
        getterKnown = !error && text && text->length > 0;
        if (getterKnown) getterNumeric = ui_font_policy::isNumericSymbolText(static_cast<size_t>(text->length),
            [&](size_t i) { return text->chars[i]; });
    }
    const auto route = ui_font_policy::preRenderRoute(path, glyphKnown,
        glyphKnown && baseline->glyphNumericSymbols, getterKnown, getterNumeric);
    if (route == ui_font_policy::PreRenderRoute::Defer) return;
    const bool useReplacement = route == ui_font_policy::PreRenderRoute::Replacement;
    // Preserve an existing picker/forum metric variant when only refreshing
    // its material or rebinding after a prefab reset. Glyph routing remains
    // authoritative for selecting a new variant after the text changes.
    updateFont(self, false, &useReplacement, false, nullptr, 0, true);
}

// The shared header has ample room for three Chinese characters at 26pt.
// Scope by the captured label path and original bounds; its adjacent level
// number remains governed by the existing original-font policy.
void sizeHeaderManagerLabel(void* self) {
    if (!self || !translations.options().fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll","UnityEngine","Object","get_name"));
    if (utf8(name) != "ManagerLabel" || textObjectPath(self) !=
        "WindowSystem/WindowRoot/HeaderCanvas/ContentArea/HeaderRoot/ManagerLevel/ManagerLabel") return;
    const auto property = [&](const char* key) {
        return readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text",key);
    };
    auto* text = static_cast<Il2CppString*>(property("get_text"));
    if (utf8(text) != "经纪人") return;
    void* transform = property("get_rectTransform");
    void* bounds = readFontProperty(transform,"UnityEngine.CoreModule.dll","UnityEngine","RectTransform","get_rect");
    void* sizeBox = property("get_fontSize");
    const auto* rect = bounds ? static_cast<float*>(objectUnbox(bounds)) : nullptr;
    if (!rect || !sizeBox || std::fabs(rect[2]-118) > .5f || std::fabs(rect[3]-39) > .5f) return;
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    if (!std::isfinite(size) || std::fabs(size-22) > .1f) return;
    static const auto setSize = methodInfo("Unity.TextMeshPro.dll","TMPro","TMP_Text","set_fontSize",1);
    if (!setSize) return;
    float desired = 26; void* args[] = {&desired}; void* error = nullptr;
    runtimeInvoke(setSize,self,args,&error);
    if (error) LOGE("Cannot size header manager label");
}

// The opening title is authored at 140pt in an 840x478 box, with explicit
// line breaks and overflow enabled. Longer translations and taller replacement
// metrics must fit that same box instead of overlapping the description.
void fitStoryOpeningTitle(void* self) {
    if (!self || !translations.options().fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    if (utf8(name) != "Title" || textObjectPath(self) !=
        "WindowSystem/WindowRoot/StoryWindow(Clone)/StoryPlayerScreen(Clone)/StoryFullScreenLoading/Root/Layout/Title") return;
    const auto property = [&](const char* key) {
        return readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", key);
    };
    void* transform = property("get_rectTransform");
    void* bounds = transform ? readFontProperty(transform,
        "UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect") : nullptr;
    void* sizeBox = property("get_fontSize");
    void* autoBox = property("get_enableAutoSizing");
    void* minimumBox = property("get_fontSizeMin");
    void* maximumBox = property("get_fontSizeMax");
    void* overflowBox = property("get_overflowMode");
    if (!bounds || !sizeBox || !autoBox || !minimumBox || !maximumBox || !overflowBox) return;
    const auto* rect = static_cast<float*>(objectUnbox(bounds));
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    const float minimum = *static_cast<float*>(objectUnbox(minimumBox));
    const float maximum = *static_cast<float*>(objectUnbox(maximumBox));
    const bool autoSize = *static_cast<bool*>(objectUnbox(autoBox));
    if (!std::isfinite(rect[2]) || !std::isfinite(rect[3]) ||
        std::fabs(rect[2] - 840) > .5f || std::fabs(rect[3] - 478) > .5f ||
        !std::isfinite(size) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        std::fabs(minimum - 18) > .1f || *static_cast<int32_t*>(objectUnbox(overflowBox)) != 0) return;
    // Enable fitting only from the original fixed-size state. Leave TMP's
    // fitted result alone on subsequent renders, avoiding size oscillation.
    if (autoSize || std::fabs(size - 140) > .1f ||
        (std::fabs(maximum - 72) > .1f && std::fabs(maximum - 140) > .1f)) return;
    static const auto setMax = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSizeMax", 1);
    static const auto setAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_enableAutoSizing", 1);
    if (!setMax || !setAuto) return;
    float cap = 140; bool enabled = true; void* error = nullptr;
    void* maxArgs[] = {&cap}; void* autoArgs[] = {&enabled};
    runtimeInvoke(setMax, self, maxArgs, &error);
    if (!error) runtimeInvoke(setAuto, self, autoArgs, &error);
    if (error) LOGE("Cannot fit story opening title");
}

void hookTmpPreRender(void* self, void* method) {
    std::lock_guard<std::recursive_mutex> frameLock(translations.frameMutex());
    registerTmpUi(self);
    refreshTranslatedText(self);
    refreshStorySwapTitle(self);
    refreshLoveEpisodeLabel(self);
    // Resolve after set_text and again after prefab font resets, before mesh creation.
    refreshDefaultUiFont(self);
    sizeHeaderManagerLabel(self);
    fitStoryOpeningTitle(self);
    applyStorySubtitleLayout(self);
    fitPanoramaLabels(self);
    alignGallerySearchFooter(self);
    alignLoveMissionLabels(self);
    alignGachaPickupLabels(self);
    alignGuildTopTitles(self);
    alignGuildPointNumbers(self);
    alignActivityReceiveName(self);
    alignActivityResultAile(self);
    if (self && fontProfileGeneration.load() > 1 && runtimeInvoke) {
        std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
        auto* baseline = fontBaseline(self);
        const auto generation = fontProfileGeneration.load();
        if (baseline && baseline->generation != generation) {
            static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
            void* error = nullptr;
            auto* current = getText ? static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error)) : nullptr;
            if (!error && current) {
                auto* previous = resolvingI18n;
                resolvingI18n = current;
                translateText(self, current);
                resolvingI18n = previous;
                relayoutBondTalkName(self);
            }
            baseline->generation = generation;
            refreshProfileMaterials();
        }
    }
    if (self && translations.options().fontEnabled && replacesCjk() && runtimeInvoke && objectUnbox) {
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
        static const auto getRectTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
        static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
        if (getName && getSize && getRectTransform && getParent) {
            void* exception = nullptr;
            auto* label = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &exception));
            if (!exception && utf8(label) == "Name") {
                void* sizeBox = runtimeInvoke(getSize, self, nullptr, &exception);
                const float size = sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
                if (!exception && std::fabs(size - 40.0f) < 1.0f) {
                    void* transform = runtimeInvoke(getRectTransform, self, nullptr, &exception);
                    void* parent = transform ? runtimeInvoke(getParent, transform, nullptr, &exception) : nullptr;
                    void* grandparent = parent ? runtimeInvoke(getParent, parent, nullptr, &exception) : nullptr;
                    auto* parentName = parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &exception)) : nullptr;
                    auto* grandparentName = grandparent ? static_cast<Il2CppString*>(runtimeInvoke(getName, grandparent, nullptr, &exception)) : nullptr;
                    if (!exception && utf8(parentName) == "NameHeader" &&
                        utf8(grandparentName) == "Bottom" && profileFlag("home_talk_name", "rebind_font_before_render")) {
                        // The HomeTalk prefab restores its original Japanese font
                        // after the first set_text call. Rebind immediately before
                        // TMP builds the visible mesh, including on the first line.
                        updateFont(self);
                    }
                }
            }
            if (!exception && homeRankOrSupportLayoutCandidate(utf8(label), translations.options().fontMode == FontMode::ReplaceAll)) {
                static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
                static const auto getAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_enableAutoSizing", 0);
                static const auto setAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_enableAutoSizing", 1);
                static const auto setSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSize", 1);
                static const auto getPreferred = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_preferredWidth", 0);
                void* transform = runtimeInvoke(getRectTransform, self, nullptr, &exception);
                void* parent = transform ? runtimeInvoke(getParent, transform, nullptr, &exception) : nullptr;
                void* grandparent = parent ? runtimeInvoke(getParent, parent, nullptr, &exception) : nullptr;
                auto* parentName = parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &exception)) : nullptr;
                auto* grandparentName = grandparent ? static_cast<Il2CppString*>(runtimeInvoke(getName, grandparent, nullptr, &exception)) : nullptr;
                void* rectBox = getRect && transform ? runtimeInvoke(getRect, transform, nullptr, &exception) : nullptr;
                const auto* rect = rectBox ? static_cast<float*>(objectUnbox(rectBox)) : nullptr;
                const std::string host = utf8(grandparentName);
                if (!exception && rect && utf8(label) == "Rank" && utf8(parentName) == "Button" &&
                    (host == "HomeVenusRankButton" || host == "HomeCompanyButton") &&
                    std::fabs(rect[2] - 152.0f) < 1.0f &&
                    std::fabs(rect[3] - 48.0f) < 1.0f && getAuto && setAuto && setSize && getPreferred) {
                    void* autoBox = runtimeInvoke(getAuto, self, nullptr, &exception);
                    void* sizeBox = runtimeInvoke(getSize, self, nullptr, &exception);
                    const bool autoSize = autoBox && *static_cast<bool*>(objectUnbox(autoBox));
                    const float size = sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
                    const char* profile = host == "HomeVenusRankButton" ? "home_venus_rank" : "home_company_rank";
                    const float baseSize = profileValue(profile, "font_size_max_unity");
                    if (!exception && autoSize == profileFlag(profile, "disable_auto_size")) {
                        bool disable = !profileFlag(profile, "disable_auto_size");
                        void* autoArgs[] = {&disable};
                        runtimeInvoke(setAuto, self, autoArgs, &exception);
                    }
                    void* preferredBox = exception ? nullptr : runtimeInvoke(getPreferred, self, nullptr, &exception);
                    const float preferredWidth = preferredBox ? *static_cast<float*>(objectUnbox(preferredBox)) : 0.0f;
                    if (!exception && size > 0.0f && preferredWidth > 0.0f) {
                        const float fitSize = size * profileValue(profile, "fit_font_to_width_unity") / preferredWidth;
                        float desiredSize = std::min(baseSize, fitSize);
                        if (std::fabs(size - desiredSize) > 0.25f) {
                            void* sizeArgs[] = {&desiredSize};
                            runtimeInvoke(setSize, self, sizeArgs, &exception);
                        }
                    }
                    if (exception) LOGE("Cannot size home rank label");
                }
                if (!exception && rect && utf8(label) == "Label" &&
                    utf8(parentName) == "CardSupport" && host == "Card") {
                    static const auto setBounds = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "SetSizeWithCurrentAnchors", 2);
                    static const auto getPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
                    static const auto setPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "set_anchoredPosition", 1);
                    static const auto getSpacing = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_characterSpacing", 0);
                    static const auto setSpacing = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_characterSpacing", 1);
                    const auto bounds = profileVector("card_support_label", "rect_size_unity");
                    const float targetY = profileValue("card_support_label", "anchored_y_unity");
                    const float targetSize = profileValue("card_support_label", "font_size_unity");
                    const float targetSpacing = profileValue("card_support_label", "character_spacing_unity");
                    if (setBounds && std::fabs(rect[2] - bounds[0]) > 0.5f) {
                        int horizontal = 0;
                        float width = bounds[0];
                        void* widthArgs[] = {&horizontal, &width};
                        runtimeInvoke(setBounds, transform, widthArgs, &exception);
                    }
                    if (!exception && setBounds && std::fabs(rect[3] - bounds[1]) > 0.5f) {
                        int vertical = 1;
                        float height = bounds[1];
                        void* heightArgs[] = {&vertical, &height};
                        runtimeInvoke(setBounds, transform, heightArgs, &exception);
                    }
                    void* positionBox = !exception && getPosition ? runtimeInvoke(getPosition, transform, nullptr, &exception) : nullptr;
                    const auto* position = positionBox ? static_cast<float*>(objectUnbox(positionBox)) : nullptr;
                    if (!exception && position && setPosition && std::fabs(position[1] - targetY) > 0.5f) {
                        float desiredPosition[] = {position[0], targetY};
                        void* positionArgs[] = {desiredPosition};
                        runtimeInvoke(setPosition, transform, positionArgs, &exception);
                    }
                    void* sizeBox = exception ? nullptr : runtimeInvoke(getSize, self, nullptr, &exception);
                    const float size = sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
                    if (!exception && setSize && std::fabs(size - targetSize) > 0.5f) {
                        float desiredSize = targetSize;
                        void* sizeArgs[] = {&desiredSize};
                        runtimeInvoke(setSize, self, sizeArgs, &exception);
                    }
                    if (!exception && getSpacing && setSpacing) {
                        void* spacingBox = runtimeInvoke(getSpacing, self, nullptr, &exception);
                        const float spacing = spacingBox ? *static_cast<float*>(objectUnbox(spacingBox)) : 0.0f;
                        if (!exception && std::fabs(spacing - targetSpacing) > 0.1f) {
                            float desiredSpacing = targetSpacing;
                            void* spacingArgs[] = {&desiredSpacing};
                            runtimeInvoke(setSpacing, self, spacingArgs, &exception);
                        }
                    }
                    if (exception) LOGE("Cannot fit level assist label");
                }
            }
        }
    }
    fitCapturedButtonLabel(self);
    applyPortraitNameLayout(self);
    centerLoveHomeDialogue(self);
    StoryPunctuationScope punctuation(self);
    origTmpPreRender(self, method);
    synchronizeTmpRendererMaterial(self);
    captureTextLayout(self);
    synchronizeActivityNameSubmeshFrame(self);
}

void hookLegacySetText(void* self, Il2CppString* value, void* method) {
    auto* translated = translateText(nullptr, value);
    updateLegacyFont(self, translated);
    origLegacySetText(self, translated, method);
}

void hookI18nSetValue(void* self, Il2CppString* key, Il2CppString* value, void* method) {
    std::lock_guard<std::recursive_mutex> dictionaryLock(translations.frameMutex());
    const std::string keyText = utf8(key);
    const std::string source = utf8(value);
    auto applyResolved = [&](const std::string& text) {
        auto* result = stringNew(text.c_str());
        if (gcHandleNewWeak && gcHandleGetTarget && gcHandleFree) {
            // Sweep collected strings instead of retaining every UI label for
            // the life of the game. The handles do not keep strings alive.
            for (auto it = resolvedI18nStrings.begin(); it != resolvedI18nStrings.end();) {
                if (!gcHandleGetTarget(it->second.weakHandle)) {
                    gcHandleFree(it->second.weakHandle);
                    it = resolvedI18nStrings.erase(it);
                } else ++it;
            }
            const uint32_t handle = gcHandleNewWeak(result, false);
            if (handle) resolvedI18nStrings.emplace(result,
                ResolvedI18nValue{handle, keyText, source});
        }
        auto* previous = resolvingI18n;
        resolvingI18n = result;
        origI18nSetValue(self, key, result, method);
        resolvingI18n = previous;
    };
    const auto* selected = translations.catalog().resolveI18n(keyText, source);
    if (selected && stringNew) {
        applyResolved(*selected);
    } else {
        dump("i18n", source, keyText);
        origI18nSetValue(self, key, value, method);
    }
}

void captureOctoSettings(void* settings) {
    if (!translations.options().captureOcto || !settings || !runtimeInvoke || !objectGetClass ||
        !classGetMethodFromName || !objectUnbox || octoCaptured.load()) return;
    std::lock_guard<std::mutex> lock(octoCaptureMutex);
    if (octoCaptured.load()) return;
    void* klass = objectGetClass(settings);
    if (!klass) return;
    auto readString = [&](const char* property) -> std::string {
        auto* getter = classGetMethodFromName(klass, property, 0);
        if (!getter) return {};
        void* exception = nullptr;
        auto* value = static_cast<Il2CppString*>(runtimeInvoke(getter, settings, nullptr, &exception));
        return exception ? std::string() : utf8(value);
    };
    auto readInt = [&](const char* property) -> int {
        auto* getter = classGetMethodFromName(klass, property, 0);
        if (!getter) return 0;
        void* exception = nullptr;
        void* boxed = runtimeInvoke(getter, settings, nullptr, &exception);
        return exception || !boxed ? 0 : *static_cast<int*>(objectUnbox(boxed));
    };
    nlohmann::json values = {
        {"url", readString("get_Url")},
        {"app_id", readInt("get_AppId")},
        {"version", readInt("get_Version")},
        {"client_secret_key", readString("get_ClientSecretKey")},
        {"aes_key", readString("get_AesKey")},
        {"a", readString("get_A")},
    };
    if (values["app_id"] == 0 || values["url"].get<std::string>().empty()) {
        LOGE("Octo settings hook fired but required fields were unavailable");
        return;
    }
    const std::string temporary = octoSettingsPath + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) { LOGE("Cannot create Octo settings file"); return; }
    output << values.dump(2) << '\n';
    output.close();
    if (std::rename(temporary.c_str(), octoSettingsPath.c_str()) != 0) {
        LOGE("Cannot finalize Octo settings file");
        return;
    }
    octoCaptured.store(true);
    LOGI("Captured Octo settings in app-owned files");
}

bool hookOctoSetup(void* settings, bool reset, void* method) {
    captureOctoSettings(settings);
    return origOctoSetup(settings, reset, method);
}

bool tryResourceLoad(Il2CppString* resourceName, void* onComplete) {
    const std::string name = utf8(resourceName);
    const uint64_t playbackGeneration = name.compare(0, 4, "adv_") == 0
        ? resetStorySwapPlayback() : storySwapPlaybackGeneration.load();
    if (name.find("adv_") != std::string::npos) {
        LOGI("Octo adventure request: %.160s", name.c_str());
    }
    if (translations.options().resourceReplace && name.size() >= 4 &&
        name.compare(name.size() - 4, 4, ".txt") == 0 &&
        name.find('/') == std::string::npos &&
        name.find('\\') == std::string::npos && name != "." && name != ".." &&
        onComplete && objectGetClass && classGetMethodFromName && stringNew && runtimeInvoke) {
        const std::string swapKind = storySwapKind(name);
        const std::string swapState = swapKind == "card" ? "story-swap.json" :
            "story-swap-" + swapKind + ".json";
        const auto selection = swapKind.empty() ? StorySwapSelection{} : storySwapRead(
            resourceRoot.substr(0, resourceRoot.find_last_of('/')) + "/" + swapState, swapKind);
        const std::string alias = selection.resolve(name);
        auto readText = [](const std::string& path, std::string& contents) {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) return false;
            const auto size = input.tellg();
            if (size <= 0 || size > 8 * 1024 * 1024) return false;
            input.seekg(0, std::ios::beg);
            contents.assign(static_cast<size_t>(size), '\0');
            return bool(input.read(contents.data(), size)) &&
                contents.find('\0') == std::string::npos;
        };
        std::string contents;
        bool loaded = readText(resourceRoot + "/" + alias, contents);
        std::string entryContents;
        if (loaded && alias != name && swapKind != "card") {
            loaded = readText(resourceRoot + "/" + name, entryContents) &&
                storySwapPairCompatible(entryContents, contents, swapKind);
        }
        const bool loadedAlias = loaded && alias != name;
        if (loadedAlias && swapKind == "love" && contents.find("[branchgroup type=Condition") != std::string::npos) {
            LOGI("Story swap Condition script: %s; preserving original entry branch context", alias.c_str());
        }
        // A removed or damaged source leaves the original local entry usable.
        if (!loaded && alias != name) {
            LOGI("Story swap source unavailable or incompatible: %s; using entry %s", alias.c_str(), name.c_str());
            loaded = readText(resourceRoot + "/" + name, contents);
        }
        StorySwapPlaybackTitles playbackTitles;
        if (loadedAlias) {
            if (!entryContents.empty() || readText(resourceRoot + "/" + name, entryContents))
                playbackTitles = storySwapPlaybackTitles(selection, name, alias, entryContents, contents);
        }
        if (loaded) {
            auto* invoke = classGetMethodFromName(objectGetClass(onComplete), "Invoke", 2);
            if (invoke) {
                void* args[] = {stringNew(contents.c_str()), nullptr};
                void* exception = nullptr;
                runtimeInvoke(invoke, onComplete, args, &exception);
                if (!exception) {
                    if (loadedAlias) {
                        std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
                        // A nested/newer adventure request owns its own playback.
                        if (storySwapPlaybackGeneration == playbackGeneration) {
                            activeStorySwapTitles = std::move(playbackTitles);
                            activeStorySwapAssets = {name, alias};
                            ++storySwapPlaybackGeneration;
                        }
                        LOGI("Story swap: %s -> %s; title metadata=%d", name.c_str(), alias.c_str(), activeStorySwapTitles.ready());
                    }
                    LOGI("Replaced Octo resource: %s", name.c_str());
                    return true;
                }
                LOGE("Octo resource callback failed: %s", name.c_str());
            }
        }
    }
    return false;
}

void hookResourceLoad(void* self, Il2CppString* resourceName, void* onComplete,
                      void* onProgress, void* method) {
    if (!tryResourceLoad(resourceName, onComplete))
        origResourceLoad(self, resourceName, onComplete, onProgress, method);
}

template <typename T>
T symbol(const char* name) {
    return reinterpret_cast<T>(xdl_sym(il2cppHandle, name, nullptr));
}

void* findClass(const char* imageName, const char* nameSpace, const char* className) {
    if (!domainGet || !domainGetAssemblies || !assemblyGetImage || !imageGetName ||
        !classFromName) return nullptr;
    void* domain = domainGet();
    if (!domain) return nullptr;
    size_t count = 0;
    void** assemblies = domainGetAssemblies(domain, &count);
    if (!assemblies) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        void* image = assemblyGetImage(assemblies[i]);
        const char* currentName = image ? imageGetName(image) : nullptr;
        if (!currentName || std::string(currentName) != imageName) continue;
        return classFromName(image, nameSpace, className);
    }
    return nullptr;
}

const MethodInfoHead* methodInfo(const char* imageName, const char* nameSpace,
                                 const char* className, const char* methodName, int parameterCount) {
    void* klass = findClass(imageName, nameSpace, className);
    return klass && classGetMethodFromName
        ? classGetMethodFromName(klass, methodName, parameterCount) : nullptr;
}

void* methodPointer(const char* imageName, const char* nameSpace,
                    const char* className, const char* methodName, int parameterCount) {
    const MethodInfoHead* method = methodInfo(imageName, nameSpace, className,
                                              methodName, parameterCount);
    return method ? method->methodPointer : nullptr;
}

void hookGetBlob(void* self, int32_t column, void** blob, int32_t* length, void* method) {
    origGetBlob(self, column, blob, length, method);
    if (!blob || !length || !*blob || *length <= 0 || *length > 8 * 1024 * 1024) return;
    const uint32_t seen = seenMasterBlobs.fetch_add(1) + 1;
    if (seen <= 2 || seen % 100000 == 0)
        LOGI("Read MasterDB Blob; reads=%u bytes=%d", seen, *length);
    const auto* translated = masterBlobPatches.match(
        static_cast<const uint8_t*>(*blob), static_cast<size_t>(*length));
    if (!translated) return;
    *blob = const_cast<uint8_t*>(translated->data());
    *length = static_cast<int32_t>(translated->size());
    const uint32_t matches = matchedMasterBlobs.fetch_add(1) + 1;
    if (matches <= 4 || matches % 50000 == 0)
        LOGI("Replaced MasterDB Blob; matches=%u", matches);
}

void logMasterMethods() {
    if (!translations.options().probeMaster || !classGetMethods || !methodGetName || !methodGetParamCount ||
        !methodGetParam || !methodGetReturnType || !typeGetName) return;
    struct Target { const char* image; const char* nameSpace; const char* name; };
    constexpr Target targets[] = {
        {"quaunity-master-manager.Runtime.dll", "Qua.Master", "MasterDBConnection"},
        {"quaunity-master-manager.Runtime.dll", "Qua.Master", "MasterBase`2"},
        {"quaunity-master-manager.Runtime.dll", "Qua.Master.SQLite", "ResultSet"},
        {"quaunity-master-manager.Runtime.dll", "Qua.Master.SQLite", "PreparedStatement"},
    };
    auto typeName = [](const void* type) -> std::string {
        if (!type) return "?";
        char* raw = typeGetName(type);
        std::string name = raw ? raw : "?";
        if (raw && il2cppFree) il2cppFree(raw);
        return name;
    };
    for (const auto& target : targets) {
        void* klass = findClass(target.image, target.nameSpace, target.name);
        LOGI("Master probe class %s.%s: %s", target.nameSpace, target.name,
             klass ? "found" : "missing");
        if (!klass) continue;
        void* iterator = nullptr;
        for (int count = 0; count < 80; ++count) {
            const auto* method = classGetMethods(klass, &iterator);
            if (!method) break;
            std::string params;
            const uint32_t parameterCount = methodGetParamCount(method);
            for (uint32_t index = 0; index < parameterCount && index < 8; ++index) {
                if (index) params += ", ";
                params += typeName(methodGetParam(method, index));
            }
            LOGI("Master probe %s.%s(%s) -> %s pointer=%d", target.name,
                 methodGetName(method), params.c_str(),
                 typeName(methodGetReturnType(method)).c_str(),
                 method->methodPointer != nullptr);
        }
    }
}

// Prefab TMP labels can have m_text serialized directly, without calling a
// setter after the scene loads. Translate that initial value when enabled.
bool hasTrackedRenderedText(void* self, Il2CppString* current) {
    if (!self || !current || !gcHandleGetTarget) return false;
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    const auto tracked = trackedTexts.find(self);
    return tracked != trackedTexts.end() &&
        gcHandleGetTarget(tracked->second.weakHandle) == self &&
        utf8(current) == tracked->second.rendered;
}

void hookTmpOnEnable(void* self, void* method) {
    origTmpOnEnable(self, method);
    registerTmpUi(self);
    if (!self || !runtimeInvoke) return;
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text",
                                           "get_text", 0);
    static const auto setText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text",
                                           "set_text", 1);
    if (!getText || !setText) return;
    void* exception = nullptr;
    auto* current = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &exception));
    if (exception || !current) return;
    // Re-enabling a pooled/hidden label does not assign a new source. Preserve
    // its original source and I18n key while the same live object still holds
    // our rendered value; refresh from that source if the dictionary changed.
    // Real setters and directly changed prefab values keep normal tracking.
    if (hasTrackedRenderedText(self, current)) {
        // Unity can restore a prefab's font/layout in its own OnEnable. Keep
        // those side effects from translateText, without translating an already
        // rendered value again or replacing its recorded source.
        auto* previous = resolvingI18n;
        resolvingI18n = current;
        translateText(self, current);
        resolvingI18n = previous;
        refreshTranslatedText(self);
        return;
    }
    auto* translated = translateText(self, current);
    if (translated == current) {
        rememberTranslatedText(self, current, translated);
        return;
    }
    void* args[] = {translated};
    runtimeInvoke(setText, self, args, &exception);
    if (exception) LOGE("TMP prefab text replacement failed");
    else {
        rememberTranslatedText(self, current, translated);
        LOGI("Localized serialized TMP text");
    }
}

bool tryAssetLoad(Il2CppString* assetName, void* onComplete) {
    const std::string name = utf8(assetName);
    if (!translations.options().assetReplace || name.empty() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos || name == "." || name == ".." ||
        !onComplete || !arrayNew || !runtimeInvoke || !objectGetClass ||
        !classGetMethodFromName) return false;
    const std::string path = assetRoot + "/" + name;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 8 || size > 64 * 1024 * 1024) {
        LOGE("Local Octo bundle has invalid size: %s", name.c_str());
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::string contents(static_cast<size_t>(size), '\0');
    if (!input.read(contents.data(), size) || contents.compare(0, 8, "UnityFS\0", 8) != 0) {
        LOGE("Local Octo bundle is not UnityFS: %s", name.c_str());
        return false;
    }
    void* byteClass = findClass("mscorlib.dll", "System", "Byte");
    const auto* load = methodInfo("UnityEngine.AssetBundleModule.dll", "UnityEngine",
                                  "AssetBundle", "LoadFromMemory", 1);
    const auto* invoke = classGetMethodFromName(objectGetClass(onComplete), "Invoke", 2);
    if (!byteClass || !load || !invoke) {
        LOGE("Octo bundle override runtime methods unavailable: %s", name.c_str());
        return false;
    }
    auto* bytes = arrayNew(byteClass, static_cast<size_t>(size));
    if (!bytes || bytes->length != static_cast<size_t>(size)) return false;
    std::memcpy(bytes->data, contents.data(), static_cast<size_t>(size));
    void* loadArgs[] = {bytes};
    void* exception = nullptr;
    void* bundle = runtimeInvoke(load, nullptr, loadArgs, &exception);
    if (exception || !bundle) {
        LOGE("Unity rejected local Octo bundle: %s", name.c_str());
        return false;
    }
    void* callbackArgs[] = {bundle, nullptr};
    exception = nullptr;
    runtimeInvoke(invoke, onComplete, callbackArgs, &exception);
    if (exception) {
        LOGE("Local Octo bundle callback failed: %s", name.c_str());
        return false;
    }
    LOGI("Replaced Octo bundle: %s", name.c_str());
    return true;
}

void hookAssetLoad(void* self, Il2CppString* assetName, void* onComplete,
                   void* onProgress, void* method) {
    const std::string name = utf8(assetName);
    if (translations.options().dumpEnabled && (name.find("banner") != std::string::npos ||
                        name.find("pvp") != std::string::npos))
        LOGI("Octo banner request: %s", name.c_str());
    if (!tryAssetLoad(assetName, onComplete))
        origAssetLoad(self, assetName, onComplete, onProgress, method);
}

const MethodInfoHead* materialMethod(const char* name, uint32_t parameters) {
    void* klass = findClass("UnityEngine.CoreModule.dll", "UnityEngine", "Material");
    void* iterator = nullptr;
    while (klass && classGetMethods && methodGetName && methodGetParamCount &&
           methodGetParam && typeGetName) {
        const auto* candidate = classGetMethods(klass, &iterator);
        if (!candidate) break;
        const char* label = methodGetName(candidate);
        if (!label || std::string(label) != name ||
            methodGetParamCount(candidate) != parameters) continue;
        char* rawType = typeGetName(methodGetParam(candidate, 0));
        const bool stringParameter = rawType && std::string(rawType) == "System.String";
        if (rawType && il2cppFree) il2cppFree(rawType);
        if (stringParameter) return candidate;
    }
    return nullptr;
}

bool materialFloat(void* material, const char* property, float& value) {
    static const auto getFloat = materialMethod("GetFloat", 1);
    if (!material || !getFloat || !stringNew || !objectUnbox) return false;
    void* args[] = {stringNew(property)};
    void* exception = nullptr;
    void* boxed = runtimeInvoke(getFloat, material, args, &exception);
    if (exception || !boxed) return false;
    value = *static_cast<float*>(objectUnbox(boxed));
    return std::isfinite(value);
}

void* materialTexture(void* material, const char* property) {
    static const auto getTexture = materialMethod("GetTexture", 1);
    if (!material || !getTexture || !stringNew || !runtimeInvoke) return nullptr;
    void* args[] = {stringNew(property)};
    void* exception = nullptr;
    void* texture = runtimeInvoke(getTexture, material, args, &exception);
    return exception ? nullptr : texture;
}

bool setMaterialTexture(void* material, const char* property, void* texture) {
    static const auto setTexture = materialMethod("SetTexture", 2);
    if (!material || !texture || !setTexture || !stringNew) return false;
    void* args[] = {stringNew(property), texture};
    void* exception = nullptr;
    runtimeInvoke(setTexture, material, args, &exception);
    return exception == nullptr;
}

bool setMaterialFloat(void* material, const char* property, float value) {
    static const auto setFloat = materialMethod("SetFloat", 2);
    if (!material || !setFloat || !stringNew) return false;
    void* args[] = {stringNew(property), &value};
    void* exception = nullptr;
    runtimeInvoke(setFloat, material, args, &exception);
    return exception == nullptr;
}

bool setMaterialColor(void* material, const char* property, const float* rgba) {
    static const auto setColor = materialMethod("SetColor", 2);
    if (!material || !setColor || !stringNew) return false;
    void* args[] = {stringNew(property), const_cast<float*>(rgba)};
    void* exception = nullptr;
    runtimeInvoke(setColor, material, args, &exception);
    return exception == nullptr;
}

const MethodInfoHead* instantiateUnityObject() {
    void* klass = findClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
    void* iterator = nullptr;
    while (klass && classGetMethods && methodGetName && methodGetParamCount &&
           methodGetParam && typeGetName) {
        const auto* candidate = classGetMethods(klass, &iterator);
        if (!candidate) break;
        const char* label = methodGetName(candidate);
        if (!label || std::strcmp(label, "Instantiate") != 0 ||
            methodGetParamCount(candidate) != 1) continue;
        char* rawType = typeGetName(methodGetParam(candidate, 0));
        const bool objectParameter = rawType && std::strcmp(rawType, "UnityEngine.Object") == 0;
        if (rawType && il2cppFree) il2cppFree(rawType);
        if (objectParameter) return candidate;
    }
    return nullptr;
}

using StoryStyle = StoryFontRole;

bool setCachedFontRetention(void* object, bool retained);

bool unityObjectAlive(void* object) {
    if (!object || !runtimeInvoke) return false;
    // A GC handle can outlive the native Unity object. Testing only the managed
    // pointer reuses destroyed materials after a scene's unused-asset sweep.
    static void* klass = findClass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
    static void* nativeField = klass && classGetFieldFromName
        ? classGetFieldFromName(klass, "m_CachedPtr") : nullptr;
    if (nativeField && fieldGetValue) {
        void* native = nullptr;
        fieldGetValue(object, nativeField, &native);
        return native != nullptr;
    }
    static const auto implicit = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "op_Implicit", 1);
    void* error = nullptr;
    void* args[] = {object};
    auto* result = implicit ? runtimeInvoke(implicit, nullptr, args, &error) : nullptr;
    return !error && result && objectUnbox && *static_cast<bool*>(objectUnbox(result));
}

struct CachedStoryMaterial { uint32_t sourceHandle, materialHandle; };
std::unordered_map<void*, CachedStoryMaterial> storyMaterials[2];
std::unordered_map<void*, CachedStoryMaterial> storySourceMaterials[2];

void* savedStorySource(void* originalFont, StoryStyle style) {
    if (style == StoryStyle::None || !gcHandleGetTarget) return nullptr;
    const auto& cache = storySourceMaterials[static_cast<int>(style)];
    const auto found = cache.find(originalFont);
    if (found == cache.end()) return nullptr;
    void* material = gcHandleGetTarget(found->second.materialHandle);
    return unityObjectAlive(material) ? material : nullptr;
}

void* rememberStorySource(void* originalFont, void* material, StoryStyle style) {
    if (void* saved = savedStorySource(originalFont, style)) return saved;
    static const auto instantiate = instantiateUnityObject();
    if (style == StoryStyle::None || !instantiate || !gcHandleNew || !gcHandleFree ||
        !unityObjectAlive(material)) return nullptr;
    void* error = nullptr;
    void* args[] = {material};
    void* copy = runtimeInvoke(instantiate, nullptr, args, &error);
    if (error || !copy || !setCachedFontRetention(copy, true)) return nullptr;
    const uint32_t source = gcHandleNew(originalFont, false), owned = gcHandleNew(copy, false);
    if (!source || !owned) {
        if (source) gcHandleFree(source);
        if (owned) gcHandleFree(owned);
        setCachedFontRetention(copy, false);
        return nullptr;
    }
    auto& cache = storySourceMaterials[static_cast<int>(style)];
    const auto previous = cache.find(originalFont);
    if (previous != cache.end()) {
        gcHandleFree(previous->second.sourceHandle);
        gcHandleFree(previous->second.materialHandle);
    }
    cache[originalFont] = {source, owned};
    LOGI("Retained ordinary-story source style=%d", static_cast<int>(style));
    return copy;
}

StoryFontRole ordinaryStoryRole(void* self, void* originalMaterial) {
    if (!self || !runtimeInvoke || !objectUnbox) return StoryFontRole::None;
    const auto found = fontBaselines.find(self);
    auto* baseline = found != fontBaselines.end() ? fontBaseline(self) : nullptr;
    if (baseline) {
        const auto role = baseline->values.find("story_role");
        if (role != baseline->values.end()) return static_cast<StoryFontRole>(static_cast<int>(role->second));
    }
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    if (!getName || !getSize || !originalMaterial) return StoryFontRole::None;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    const auto label = utf8(name);
    if (error || (label != "Message Label" && label != "Name Label")) return StoryFontRole::None;
    auto* sizeBox = runtimeInvoke(getSize, self, nullptr, &error);
    float width = 0;
    if (error || !sizeBox || !materialFloat(originalMaterial, "_OutlineWidth", width)) return StoryFontRole::None;
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    const auto role = storyFontRole(label, textObjectPath(self), size, width);
    if (role != StoryFontRole::None) {
        originalFontValue(self, "story_role", static_cast<float>(role));
        if (role == StoryFontRole::Subtitle) {
            originalFontValue(self, "story_subtitle_size", size);
            originalFontValue(self, "story_subtitle_outline", width);
        }
    }
    return role;
}

void applyStorySubtitleLayout(void* self) {
    if (!self || !translations.options().fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox ||
        fontBaselines.find(self) == fontBaselines.end()) return;
    auto* baseline = fontBaseline(self);
    // Registered only after the original subtitle size/material guards pass.
    // Never infer the role from a user-adjusted size or a generic label name.
    if (!baseline || !baseline->values.count("story_subtitle_size")) return;
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    static const auto setSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSize", 1);
    static const auto getSpacing = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_characterSpacing", 0);
    static const auto setSpacing = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_characterSpacing", 1);
    static const auto getAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_enableAutoSizing", 0);
    static const auto setAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_enableAutoSizing", 1);
    if (!getSize || !setSize || !getSpacing || !setSpacing || !getAuto || !setAuto) return;
    void* error = nullptr;
    void* sizeBox = runtimeInvoke(getSize, self, nullptr, &error);
    if (error || !sizeBox) return;
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    void* spacingBox = runtimeInvoke(getSpacing, self, nullptr, &error);
    if (error || !spacingBox) return;
    const float spacing = *static_cast<float*>(objectUnbox(spacingBox));
    void* autoBox = runtimeInvoke(getAuto, self, nullptr, &error);
    if (error || !autoBox) return;
    const bool autoSize = *static_cast<bool*>(objectUnbox(autoBox));
    float desiredSize, desiredSpacing;
    {
        std::lock_guard<std::mutex> lock(profileMutex);
        desiredSize = fontProfiles.value("ordinary_story_subtitle", "font_size_unity");
        desiredSpacing = fontProfiles.value("ordinary_story_subtitle", "character_spacing_unity");
    }
    if (autoSize) {
        bool disabled = false;
        void* args[] = {&disabled};
        runtimeInvoke(setAuto, self, args, &error);
    }
    if (!error && std::fabs(size - desiredSize) > 0.01f) {
        void* args[] = {&desiredSize};
        runtimeInvoke(setSize, self, args, &error);
    }
    if (!error && std::fabs(spacing - desiredSpacing) > 0.01f) {
        void* args[] = {&desiredSpacing};
        runtimeInvoke(setSpacing, self, args, &error);
    }
    const float generation = static_cast<float>(fontProfileGeneration.load());
    if (!error && baseline->values["story_subtitle_layout_generation"] != generation) {
        baseline->values["story_subtitle_layout_generation"] = generation;
        LOGI("Story subtitle layout: size=%.2f->%.2f spacing=%.2f->%.2f auto=%d",
             size, desiredSize, spacing, desiredSpacing, autoSize);
    }
}

bool applyStoryMaterial(void* material, StoryStyle style) {
    const char* id = style == StoryStyle::SpeakerName ? "ordinary_story_name" : "ordinary_story_subtitle";
    if (!setMaterialFloat(material, "_OutlineWidth", profileValue(id, "outline_width")) ||
        !setMaterialFloat(material, "_FaceDilate", profileValue(id, "face_dilate"))) return false;
    if (style == StoryStyle::SpeakerName) {
        const auto face = profileVector(id, "face_color_rgba");
        const auto outline = profileVector(id, "outline_color_rgba");
        return setMaterialColor(material, "_FaceColor", face.data()) &&
               setMaterialColor(material, "_OutlineColor", outline.data());
    }
    return true;
}

void refreshStoryMeshPadding(void* self, void* material, StoryFontRole role) {
    if (role == StoryFontRole::None) return;
    auto* baseline = fontBaseline(self);
    const auto generation = fontProfileGeneration.load();
    if (!baseline || (baseline->storyOutlineGeneration == generation &&
                      baseline->storyOutlineMaterial == material)) return;
    const auto update = objectGetClass && classGetMethodFromName
        ? classGetMethodFromName(objectGetClass(self), "UpdateMeshPadding", 0) : nullptr;
    if (!update || !applyStoryMaterial(material, role)) return;
    void* error = nullptr;
    runtimeInvoke(update, self, nullptr, &error);
    if (!error) {
        baseline->storyOutlineGeneration = generation;
        baseline->storyOutlineMaterial = material;
        float gradient = 0, ratio = 0, width = 0, face = 0;
        materialFloat(material, "_GradientScale", gradient);
        materialFloat(material, "_ScaleRatioA", ratio);
        materialFloat(material, "_OutlineWidth", width);
        materialFloat(material, "_FaceDilate", face);
        LOGI("Story outline refreshed: role=%d gradient=%.1f ratio=%.4f W=%.3f D=%.3f",
             static_cast<int>(role), gradient, ratio, width, face);
    }
}
struct RouletteOutlineMaterial {
    uint32_t sourceHandle, copyHandle;
    float baselineFace, width;
};
std::unordered_map<void*, RouletteOutlineMaterial> rouletteOutlineMaterials;
bool applyRouletteOutline(void* material, const RouletteOutlineMaterial& baseline) {
    float face = 0;
    return outwardOutlineFace(baseline.baselineFace, baseline.width,
        profileFlag("daily_roulette_description", "outline_outward"), face) &&
        setMaterialFloat(material, "_FaceDilate", face);
}
void* rouletteOutlineMaterial(void* self, void* shared) {
    if (!self || !shared || !runtimeInvoke || !objectUnbox || !replacesCjk()) return shared;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    void* error = nullptr;
    auto* name = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName,self,nullptr,&error)) : nullptr;
    if (error || utf8(name) != "DescriptionLabel") return shared;
    nlohmann::json profile;
    { std::lock_guard<std::mutex> lock(profileMutex); profile = fontProfiles.definition("daily_roulette_description"); }
    std::string expected = "WindowSystem/WindowRoot";
    for (const auto& part : profile["path_suffix"]) expected += "/" + part.get<std::string>();
    if (textObjectPath(self) != expected) return shared;
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
    void* sizeBox = getSize ? runtimeInvoke(getSize,self,nullptr,&error) : nullptr;
    const float size = !error && sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0;
    void* transform = getTransform ? runtimeInvoke(getTransform,self,nullptr,&error) : nullptr;
    void* rectBox = !error && transform && getRect ? runtimeInvoke(getRect,transform,nullptr,&error) : nullptr;
    const auto* rect = !error && rectBox ? static_cast<float*>(objectUnbox(rectBox)) : nullptr;
    const auto& guard = profile["guard"];
    if (!rect || size < guard["original_font_size_unity"][0].get<float>() ||
        size > guard["original_font_size_unity"][1].get<float>() ||
        std::fabs(rect[2]-guard["rect_size_unity"][0].get<float>()) > 1 ||
        std::fabs(rect[3]-guard["rect_size_unity"][1].get<float>()) > 1) return shared;
    // Register this exact label for the existing profile-change rebuild queue.
    auto* baseline = fontBaseline(self);
    const auto refreshPadding = [&]() {
        if (!baseline) return;
        const float generation = static_cast<float>(fontProfileGeneration.load());
        auto& applied = baseline->values["roulette_outline_generation"];
        if (applied == generation) return;
        static const auto updatePadding = methodInfo("Unity.TextMeshPro.dll","TMPro","TMP_Text","UpdateMeshPadding",0);
        void* paddingError = nullptr;
        if (updatePadding) runtimeInvoke(updatePadding,self,nullptr,&paddingError);
        if (!paddingError) applied = generation;
    };
    // A repeated setter can supply our isolated copy. Reuse the stored source
    // baseline, so re-enabling and live previews never accumulate dilation.
    for (const auto& item : rouletteOutlineMaterials) {
        void* copy = gcHandleGetTarget ? gcHandleGetTarget(item.second.copyHandle) : nullptr;
        if (copy == shared) { applyRouletteOutline(copy,item.second); refreshPadding(); return copy; }
    }
    const auto found = rouletteOutlineMaterials.find(shared);
    if (found != rouletteOutlineMaterials.end() && gcHandleGetTarget) {
        void* copy = gcHandleGetTarget(found->second.copyHandle);
        if (copy && applyRouletteOutline(copy,found->second)) return copy;
    }
    float face = 0, width = 0, target = 0;
    if (!gcHandleNew || !gcHandleFree || !gcHandleGetTarget ||
        !materialFloat(shared,"_FaceDilate",face) || !materialFloat(shared,"_OutlineWidth",width) ||
        !outwardOutlineFace(face,width,profileFlag("daily_roulette_description","outline_outward"),target)) return shared;
    static const auto instantiate = instantiateUnityObject();
    void* args[] = {shared};
    void* copy = instantiate ? runtimeInvoke(instantiate,nullptr,args,&error) : nullptr;
    if (error || !copy || !setMaterialFloat(copy,"_FaceDilate",target)) return shared;
    const uint32_t sourceHandle = gcHandleNew(shared,false), copyHandle = gcHandleNew(copy,false);
    if (!sourceHandle || !copyHandle) {
        if (sourceHandle) gcHandleFree(sourceHandle);
        if (copyHandle) gcHandleFree(copyHandle);
        return shared;
    }
    rouletteOutlineMaterials.emplace(shared,RouletteOutlineMaterial{sourceHandle,copyHandle,face,width});
    static int reports = 0;
    if (reports++ < 8) LOGI("Daily roulette outward outline: size=%.1f rect_width=%.1f rect_height=%.1f outline=%.4f face_before=%.4f face_after=%.4f",size,rect[2],rect[3],width,face,target);
    return copy;
}
void refreshProfileMaterials() {
    static uint64_t applied = 0;
    const auto generation = fontProfileGeneration.load();
    if (applied == generation || !gcHandleGetTarget) return;
    for (int style = 0; style < 2; ++style)
        for (const auto& item : storyMaterials[style])
            if (auto* material = gcHandleGetTarget(item.second.materialHandle))
                if (unityObjectAlive(material)) applyStoryMaterial(material, static_cast<StoryStyle>(style));
    for (const auto& item : rouletteOutlineMaterials)
        if (auto* material = gcHandleGetTarget(item.second.copyHandle)) applyRouletteOutline(material,item.second);
    applied = generation;
}
void* storyMaterial(void* shared, StoryStyle style, void* stableSource = nullptr) {
    static const auto instantiate = instantiateUnityObject();
    // One entry per original font/style, not per temporary TMP fallback. TMP
    // can recreate its own fallback each time a story screen is opened.
    void* key = stableSource ? stableSource : shared;
    auto& cache = storyMaterials[static_cast<int>(style)];
    const auto found = cache.find(key);
    if (found != cache.end() && gcHandleGetTarget) {
        void* cached = gcHandleGetTarget(found->second.materialHandle);
        const bool alive = unityObjectAlive(cached);
        if (alive && materialTexture(cached, "_MainTex") == materialTexture(shared, "_MainTex") &&
            applyStoryMaterial(cached, style)) return cached;
        if (alive) setCachedFontRetention(cached, false);
        if (gcHandleFree) {
            gcHandleFree(found->second.sourceHandle);
            gcHandleFree(found->second.materialHandle);
        }
        cache.erase(found);
        LOGI("Discarded stale ordinary-story material style=%d", static_cast<int>(style));
    }
    if (!instantiate || !gcHandleNew || !gcHandleFree || !runtimeInvoke || !unityObjectAlive(shared)) return nullptr;
    void* args[] = {shared};
    void* exception = nullptr;
    void* copy = runtimeInvoke(instantiate, nullptr, args, &exception);
    if (exception || !copy || !applyStoryMaterial(copy, style) || !setCachedFontRetention(copy, true)) return nullptr;
    const uint32_t source = gcHandleNew(key, false), owned = gcHandleNew(copy, false);
    if (!source || !owned) {
        if (source) gcHandleFree(source);
        if (owned) gcHandleFree(owned);
        setCachedFontRetention(copy, false);
        return nullptr;
    }
    cache[key] = {source, owned};
    LOGI("Created isolated story material style=%d", static_cast<int>(style));
    return copy;
}

void restoreStoryNameColor(void* textObject) {
    if (!translations.options().fontEnabled || !replacesCjk() || !textObject ||
        !runtimeInvoke || !objectUnbox) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    static const auto getMaterial = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSharedMaterial", 0);
    static const auto setColor = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_color", 1);
    if (!getName || !getSize || !getMaterial || !setColor) return;
    void* exception = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, textObject, nullptr, &exception));
    if (exception || utf8(name) != "Name Label") return;
    void* sizeBox = runtimeInvoke(getSize, textObject, nullptr, &exception);
    const float size = sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
    if (exception || size < 30.0f || size > 34.0f) return;
    void* material = runtimeInvoke(getMaterial, textObject, nullptr, &exception);
    float outlineWidth = 0.0f;
    if (exception || !materialFloat(material, "_OutlineWidth", outlineWidth) ||
        std::fabs(outlineWidth - profileValue("ordinary_story_name", "outline_width")) > 0.01f) return;
    auto white = profileVector("ordinary_story_name", "vertex_color_rgba");
    void* args[] = {white.data()};
    runtimeInvoke(setColor, textObject, args, &exception);
    if (exception) LOGE("Cannot keep ordinary-story name edge white");
}

bool setCachedFontRetention(void* object, bool retained) {
    // Native GC handles keep the managed wrapper alive, but Unity's unused
    // asset sweep does not see this C++ cache. Pin only our process-lifetime
    // font roots; Unity follows their material, atlas and source-font links.
    static const auto getFlags = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_hideFlags", 0);
    static const auto setFlags = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "set_hideFlags", 1);
    if (!object || !getFlags || !setFlags || !runtimeInvoke || !objectUnbox) return false;
    void* error = nullptr;
    void* box = runtimeInvoke(getFlags, object, nullptr, &error);
    if (error || !box) return false;
    const int32_t previous = *static_cast<int32_t*>(objectUnbox(box));
    constexpr int32_t dontUnloadUnusedAsset = 32;
    int32_t flags = retained ? previous | dontUnloadUnusedAsset : previous & ~dontUnloadUnusedAsset;
    if (flags == previous) return true;
    void* args[] = {&flags};
    runtimeInvoke(setFlags, object, args, &error);
    return !error;
}

#include "standalone_font_bundle.inc"

void* loadedCjkFont(bool independentReplacement = false, bool rounded = false) {
    std::lock_guard<std::recursive_mutex> lock(fontMutex);
    auto& handle = rounded ? loadedRoundedFontHandle : independentReplacement ? loadedReplacementFontHandle : loadedFontHandle;
    if (handle && gcHandleGetTarget) {
        void* cached = gcHandleGetTarget(handle);
        if (cached) return cached;
    }
    // Throttle retries while the module bundle or a legacy scene font is not
    // ready. Each source has its own schedule so modes cannot starve it.
    using FontScanClock = std::chrono::steady_clock;
    struct FontScanState {
        FontScanClock::time_point nextScan{};
        FontScanClock::time_point nextDiagnostic{};
    };
    static FontScanState scans[3];
    auto& scan = scans[rounded ? 2 : independentReplacement ? 1 : 0];
    const auto now = FontScanClock::now();
    if (now < scan.nextScan) return nullptr;
    scan.nextScan = now + std::chrono::seconds(2);
    const bool report = now >= scan.nextDiagnostic;
    if (report) scan.nextDiagnostic = now + std::chrono::seconds(60);
    // Current modes use only the module's selected font. A temporary bundle
    // failure must not cache an older embedded typeface under the same asset
    // key and prevent every subsequent module retry.
    if (independentReplacement) return loadStandaloneReplacementFont(report, rounded);
    const char* wanted = "FOT-UDKakugo_LargePro-DB";
    void* klass = findClass("UnityEngine.TextRenderingModule.dll", "UnityEngine", "Font");
    const auto findAll = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
                                    "Resources", "FindObjectsOfTypeAll", 1);
    const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
                                     "Object", "get_name", 0);
    if (!klass || !classGetType || !typeGetObject || !findAll || !getName) {
        if (report) LOGE("Cannot enumerate Unity fonts: class=%p find=%p name=%p", klass, findAll, getName);
        return nullptr;
    }
    void* typeObject = typeGetObject(classGetType(klass));
    void* args[] = {typeObject};
    void* exception = nullptr;
    auto* array = static_cast<char*>(runtimeInvoke(findAll, nullptr, args, &exception));
    if (!array || exception) {
        if (report) LOGE("FindObjectsOfTypeAll(Font) failed");
        return nullptr;
    }
    const size_t count = *reinterpret_cast<size_t*>(array + 24);
    if (report) LOGI("Loaded Unity Font count=%zu; searching for %s", count, wanted);
    if (count > 1024) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        void* font = *reinterpret_cast<void**>(array + 32 + i * sizeof(void*));
        auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, font, nullptr, &exception));
        if (exception) { exception = nullptr; continue; }
        const std::string label = utf8(name);
        if (report) LOGI("Loaded Unity Font: %s", label.c_str());
        if (label == wanted) {
            if (gcHandleNew) handle = gcHandleNew(font, false);
            if (handle || report) LOGI("Found Unity source font: %s", wanted);
            return font;
        }
    }
    if (report) LOGI("Unity source font unavailable: %s; retrying while text is active", wanted);
    return nullptr;
}

void updateLegacyFont(void* textObject, Il2CppString* rendered) {
    if (!translations.options().fontEnabled || !usesIndependentFontRouting() || !textObject || !rendered ||
        !runtimeInvoke || !gcHandleGetTarget || !gcHandleNew || !gcHandleNewWeak || !gcHandleFree) return;
    std::lock_guard<std::recursive_mutex> lock(fontMutex);
    static const auto getFont = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "Text", "get_font", 0);
    static const auto setFont = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "Text", "set_font", 1);
    if (!getFont || !setFont) return;
    const auto release = [](const OriginalLegacyFont& saved) {
        gcHandleFree(saved.weakHandle);
        gcHandleFree(saved.fontHandle);
    };
    auto saved = originalLegacyFonts.find(textObject);
    if (saved != originalLegacyFonts.end() && gcHandleGetTarget(saved->second.weakHandle) != textObject) {
        release(saved->second);
        originalLegacyFonts.erase(saved);
        saved = originalLegacyFonts.end();
    }
    const bool useReplacement = translations.options().fontMode == FontMode::ReplaceUi
        ? ui_font_policy::usesReplacement(uiFontPolicyPath(textObject),
            rendered->length >= 0 && ui_font_policy::isNumericSymbolText(static_cast<size_t>(rendered->length),
                [&](size_t i) { return rendered->chars[i]; }))
        : (rendered->length > 0 &&
           mixed_font_router::hasCjk(rendered->chars, static_cast<size_t>(rendered->length)));
    // Classification uses the translated setter argument, never the previous text.
    if (!useReplacement && saved == originalLegacyFonts.end()) return;
    void* exception = nullptr;
    void* current = runtimeInvoke(getFont, textObject, nullptr, &exception);
    if (exception || !current) return;
    void* replacement = useReplacement ? loadedCjkFont(true) : gcHandleGetTarget(saved->second.fontHandle);
    if (!replacement) return;
    if (useReplacement && current != replacement) {
        if (saved == originalLegacyFonts.end()) {
            static uint32_t insertions = 0;
            if ((++insertions % 32) == 0) {
                for (auto it = originalLegacyFonts.begin(); it != originalLegacyFonts.end();) {
                    if (!gcHandleGetTarget(it->second.weakHandle)) {
                        release(it->second);
                        it = originalLegacyFonts.erase(it);
                    } else ++it;
                }
            }
            const uint32_t weak = gcHandleNewWeak(textObject, false);
            if (!weak) return;
            const uint32_t font = gcHandleNew(current, false);
            if (!font) { gcHandleFree(weak); return; }
            originalLegacyFonts.emplace(textObject, OriginalLegacyFont{weak, font});
        } else if (gcHandleGetTarget(saved->second.fontHandle) != current) {
            // A prefab changed its original font since the previous assignment.
            const uint32_t font = gcHandleNew(current, false);
            if (!font) return;
            gcHandleFree(saved->second.fontHandle);
            saved->second.fontHandle = font;
        }
    }
    if (current != replacement) {
        void* args[] = {replacement};
        runtimeInvoke(setFont, textObject, args, &exception);
        if (exception) return;
    }
    if (!useReplacement) {
        release(saved->second);
        originalLegacyFonts.erase(saved);
    }
}

// This pill expands horizontally with the speaker name. Automatic vertical
// fitting to the 40-unit row makes CJK names jump from 40 to 27.6 points when
// a later setter reuses the same label. Keep the selected size before layout.
void applyPortraitNameLayout(void* self) {
    if (!self || !translations.options().fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self, "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    if (utf8(name) != "Text") return;
    static const std::string suffix = "/Love UI/Message Area/Message Container/Message Root/Name Container/Layout Group/Name Label/Text";
    const auto path = textObjectPath(self);
    if (path.size() < suffix.size() || path.compare(path.size() - suffix.size(), suffix.size(), suffix)) return;
    const auto property = [&](const char* key) { return readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", key); };
    void* transform = property("get_rectTransform");
    void* rectBox = readFontProperty(transform, "UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect");
    void* maxBox = property("get_fontSizeMax");
    void* sizeBox = property("get_fontSize");
    void* autoBox = property("get_enableAutoSizing");
    if (!rectBox || !maxBox || !sizeBox || !autoBox) return;
    const auto* rect = static_cast<float*>(objectUnbox(rectBox));
    const float maximum = *static_cast<float*>(objectUnbox(maxBox));
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    if (!std::isfinite(size) || !std::isfinite(maximum) || maximum < 39 || maximum > 64.5f ||
        !std::isfinite(rect[2]) || rect[2] <= 0 || rect[2] > 960 ||
        !std::isfinite(rect[3]) || std::fabs(rect[3] - 40) > 1) return;
    static const auto setAuto = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_enableAutoSizing", 1);
    static const auto setSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSize", 1);
    static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
    if (!setAuto || !setSize || !setAlignment) return;
    void* error = nullptr;
    const bool wasAuto = *static_cast<bool*>(objectUnbox(autoBox));
    if (wasAuto) { bool value = false; void* args[] = {&value}; runtimeInvoke(setAuto, self, args, &error); }
    float desired = profileValue("portrait_love_event_name", "font_size_base_unity") * profileValue("portrait_love_event_name", "font_size_ratio");
    if (!error && std::fabs(size - desired) > .1f) { void* args[] = {&desired}; runtimeInvoke(setSize, self, args, &error); }
    void* align = property("get_alignment");
    if (!error && align && *static_cast<int32_t*>(objectUnbox(align)) == 4097) {
        int32_t centered = 4098; void* args[] = {&centered}; runtimeInvoke(setAlignment, self, args, &error);
    }
    static int reports = 0;
    if ((wasAuto || std::fabs(size - desired) > .1f) && reports++ < 16)
        LOGI("Portrait name stable layout: previous_size=%.2f target_size=%.2f previous_auto=%d rect_w=%.2f exception=%d", size, desired, wasAuto, rect[2], error != nullptr);
}

void* createConstrainedFontClone(void* normalReplacement, const std::string& glyphText = "选择消息") {
    if (!normalReplacement || !runtimeInvoke || !classGetMethods || !methodGetName ||
        !methodGetParamCount || !methodGetParam || !typeGetName || !objectGetClass || !stringNew || !objectUnbox)
        return nullptr;
    const MethodInfoHead* addCharacters = nullptr;
    const MethodInfoHead* hasCharacters = nullptr;
    void* iterator = nullptr;
    while (const auto* candidate = classGetMethods(objectGetClass(normalReplacement), &iterator)) {
        const char* name = methodGetName(candidate);
        if (!name) continue;
        const bool add = !std::strcmp(name, "TryAddCharacters") && methodGetParamCount(candidate) == 3;
        const bool has = !std::strcmp(name, "HasCharacters") && methodGetParamCount(candidate) == 1;
        if (!add && !has) continue;
        char* rawType = typeGetName(methodGetParam(candidate, 0));
        const bool stringOverload = rawType && std::string(rawType) == "System.String";
        if (rawType && il2cppFree) il2cppFree(rawType);
        if (stringOverload) {
            if (add) addCharacters = candidate;
            if (has) hasCharacters = candidate;
        }
    }
    if (!addCharacters || !hasCharacters) return nullptr;
    auto* phrase = stringNew(glyphText.c_str());
    Il2CppString* missing = nullptr;
    bool includeFeatures = false, attempted = false, addResult = false;
    void* error = nullptr;
    const auto checkCoverage = [&]() -> FontCoverageCheck {
        void* args[] = {phrase};
        error = nullptr;
        void* result = runtimeInvoke(hasCharacters, normalReplacement, args, &error);
        return {result && !error && *static_cast<bool*>(objectUnbox(result)), error || !result};
    };
    const bool covered = ensureFontCoverage(checkCoverage, [&]() {
        attempted = true;
        void* args[] = {phrase, &missing, &includeFeatures};
        error = nullptr;
        void* result = runtimeInvoke(addCharacters, normalReplacement, args, &error);
        addResult = result && !error && *static_cast<bool*>(objectUnbox(result));
        return !error && result != nullptr;
    });
    if (!covered) {
        LOGE("Cannot prewarm constrained replacement glyphs: exception=%d attempted=%d result=%d missing_length=%d",
             error != nullptr, attempted, addResult, missing ? missing->length : -1);
        return nullptr;
    }
    if (!attempted || !addResult) {
        static int recovered = 0;
        if (recovered++ < 8) LOGI("Constrained replacement coverage verified despite no new glyph addition: attempted=%d", attempted);
    }
    static const auto instantiate = instantiateUnityObject();
    static const auto setPopulation = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "set_atlasPopulationMode", 1);
    if (!instantiate || !setPopulation) return nullptr;
    void* cloneArgs[] = {normalReplacement};
    void* clone = runtimeInvoke(instantiate, nullptr, cloneArgs, &error);
    if (error || !clone) return nullptr;
    // Instantiate copies the root's flags. Per-label clones are released when
    // their label disappears, so they must remain eligible for Unity's sweep.
    if (!setCachedFontRetention(clone, false)) return nullptr;
    int32_t staticPopulation = 0;
    void* populationArgs[] = {&staticPopulation};
    runtimeInvoke(setPopulation, clone, populationArgs, &error);
    if (error) return nullptr;
    // No separate texture or glyph allocation: this immutable picker clone
    // retains the normal replacement character/glyph tables, atlas and material.
    LOGI("Created static constrained clone sharing prewarmed normal replacement atlas");
    return clone;
}

bool normalizePickerFaceInfo(void* replacement, void* original) {
    static const auto getFace = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "get_faceInfo", 0);
    static const auto setFace = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "set_faceInfo", 1);
    if (!getFace || !setFace || !runtimeInvoke || !objectGetClass || !objectUnbox ||
        !classGetFieldFromName || !fieldGetValue || !fieldSetValue) return false;
    void* error = nullptr;
    void* target = runtimeInvoke(getFace, replacement, nullptr, &error);
    if (error || !target) return false;
    void* source = runtimeInvoke(getFace, original, nullptr, &error);
    if (error || !source) return false;
    void* klass = objectGetClass(target);
    void* pointField = classGetFieldFromName(klass, "m_PointSize");
    int32_t sourcePoint = 0, targetPoint = 0;
    if (!pointField) return false;
    fieldGetValue(source, pointField, &sourcePoint);
    fieldGetValue(target, pointField, &targetPoint);
    if (sourcePoint <= 0 || targetPoint <= 0) return false;
    void* scaleField = classGetFieldFromName(klass, "m_Scale");
    float sourceScale = 0, targetScale = 0;
    if (!scaleField) return false;
    fieldGetValue(source, scaleField, &sourceScale); fieldGetValue(target, scaleField, &targetScale);
    if (sourceScale <= 0 || targetScale <= 0) return false;
    const float ratio = static_cast<float>(targetPoint) / static_cast<float>(sourcePoint) * sourceScale / targetScale;
    const char* fields[] = {"m_LineHeight", "m_AscentLine", "m_CapLine", "m_MeanLine", "m_Baseline", "m_DescentLine"};
    for (const char* name : fields) {
        void* field = classGetFieldFromName(klass, name);
        if (!field) return false;
        float metric = 0.0f;
        fieldGetValue(source, field, &metric);
        metric *= ratio;
        fieldSetValue(target, field, &metric);
    }
    // Only the independently created asset receives original vertical line
    // metrics. Glyphs, atlas, point size, scale and widths stay intact.
    void* args[] = {objectUnbox(target)};
    runtimeInvoke(setFace, replacement, args, &error);
    if (error) return false;
    LOGI("Normalized picker replacement line metrics: original_point=%d target_point=%d ratio=%.4f", sourcePoint, targetPoint, ratio);
    return true;
}

void* readFontProperty(void* object, const char* assembly, const char* ns, const char* type, const char* name) {
    if (!object) return nullptr;
    const auto method = methodInfo(assembly, ns, type, name, 0);
    void* error = nullptr;
    void* value = method && runtimeInvoke ? runtimeInvoke(method, object, nullptr, &error) : nullptr;
    return error ? nullptr : value;
}
FontVerticalMetrics verticalMetrics(void* font) {
    FontVerticalMetrics result{};
    if (!font) return result;
    void* face = readFontProperty(font, "Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "get_faceInfo");
    void* klass = face && objectGetClass ? objectGetClass(face) : nullptr;
    if (!klass || !classGetFieldFromName || !fieldGetValue) return result;
    const auto read = [&](const char* name, void* output) {
        void* field = classGetFieldFromName(klass, name);
        if (field) fieldGetValue(face, field, output);
    };
    int32_t point = 0;
    read("m_PointSize", &point); result.pointSize = static_cast<float>(point);
    read("m_Scale", &result.scale); read("m_AscentLine", &result.ascent); read("m_DescentLine", &result.descent);
    return result;
}

void* createStoryFont(void* font, void* original, void* originalMaterial) {
    const auto source = verticalMetrics(original);
    float gradient = 0;
    if (!materialFloat(originalMaterial, "_GradientScale", gradient)) return nullptr;
    int32_t point = 90, padding = storyAtlasPadding(source.pointSize, source.scale, gradient);
    if (!padding || !fieldGetValueObject || !objectUnbox || !classGetFieldFromName) return nullptr;
    static const auto create = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "CreateFontAsset", 8);
    static void* modeClass = findClass("UnityEngine.TextCoreFontEngineModule.dll", "UnityEngine.TextCore.LowLevel", "GlyphRenderMode");
    static void* modeField = modeClass ? classGetFieldFromName(modeClass, "SDFAA") : nullptr;
    void* modeBox = modeField ? fieldGetValueObject(modeField, nullptr) : nullptr;
    if (!create || !modeBox) return nullptr;
    int32_t renderMode = *static_cast<int32_t*>(objectUnbox(modeBox));
    int32_t width = 1024, height = 1024, dynamicPopulation = 1;
    bool multiAtlas = true;
    void* args[] = {font, &point, &padding, &renderMode, &width, &height, &dynamicPopulation, &multiAtlas};
    void* error = nullptr;
    void* result = runtimeInvoke(create, nullptr, args, &error);
    if (error || !result) return nullptr;
    const auto target = verticalMetrics(result);
    // The factory's normal face scale is 1. Never silently use a different
    // range if a future TextCore version changes this contract.
    if (target.pointSize != point || std::fabs(target.scale - 1) > .001f) return nullptr;
    LOGI("Created ordinary-story SDF: original_point=%.1f original_scale=%.3f original_gradient=%.1f point=%d padding=%d render_mode=%d",
         source.pointSize, source.scale, gradient, point, padding, renderMode);
    return result;
}
void synchronizeTmpRendererMaterial(void* self) {
    if (!usesIndependentFontRouting() || !translations.options().fontEnabled || !isTmpUi(self)) return;
    std::lock_guard<std::recursive_mutex> lock(fontMutex);
    // Baseline lifetime is independent of the original material snapshot, which
    // is released immediately after a successful Latin restoration.
    auto found = fontBaselines.find(self);
    FontBaseline* baseline = found != fontBaselines.end() ? fontBaseline(self) : nullptr;
    const bool pending = baseline && baseline->pendingRendererSubmit;
    void* font = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_font");
    const bool replacement = font && labelOriginalFonts.count(font);
    if (!replacement && !pending) return;
    void* shared = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_fontSharedMaterial");
    void* expected = shared ? materialTexture(shared,"_MainTex") : nullptr;
    if (!expected) return;
    void* renderer = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TextMeshProUGUI","get_canvasRenderer");
    static const auto getMaterial = methodInfo("UnityEngine.UIModule.dll","UnityEngine","CanvasRenderer","GetMaterial",1);
    static const auto updateMaterial = methodInfo("Unity.TextMeshPro.dll","TMPro","TextMeshProUGUI","UpdateMaterial",0);
    if (!renderer || !getMaterial || !updateMaterial) return;
    const auto actualTexture = [&]() -> void* {
        int32_t index=0; void* args[]={&index}; void* error=nullptr;
        void* material=runtimeInvoke(getMaterial,renderer,args,&error);
        return !error && material ? materialTexture(material,"_MainTex") : nullptr;
    };
    void* before=actualTexture();
    if (!shouldSubmitTmpRenderer(replacement,pending,expected!=nullptr,before==expected)) return;
    void* error=nullptr;
    runtimeInvoke(updateMaterial,self,nullptr,&error);
    void* after=actualTexture();
    if (!baseline) baseline=fontBaseline(self);
    if (baseline) baseline->pendingRendererSubmit=pendingTmpRendererAfterSubmit(!error,after==expected);

}
void releaseConstrainedTitleFont(const ConstrainedTitleFont& saved) {
    if (!gcHandleGetTarget || !gcHandleFree) return;
    if (void* font = gcHandleGetTarget(saved.fontHandle)) labelOriginalFonts.erase(font);
    gcHandleFree(saved.weakHandle); gcHandleFree(saved.fontHandle);
    for (const auto handle : saved.retiredHandles) {
        if (void* font = gcHandleGetTarget(handle)) labelOriginalFonts.erase(font);
        gcHandleFree(handle);
    }
}
void commitConstrainedTitleFont(void* self, void* assigned) {
    auto saved = constrainedTitleFonts.find(self);
    if (saved == constrainedTitleFonts.end()) return;
    if (gcHandleGetTarget(saved->second.fontHandle) != assigned) {
        releaseConstrainedTitleFont(saved->second); constrainedTitleFonts.erase(saved); return;
    }
    for (const auto handle : saved->second.retiredHandles) {
        if (void* font = gcHandleGetTarget(handle)) labelOriginalFonts.erase(font);
        gcHandleFree(handle);
    }
    saved->second.retiredHandles.clear();
}
void collectConstrainedTitleFonts() {
    if (!gcHandleGetTarget) return;
    for (auto it = constrainedTitleFonts.begin(); it != constrainedTitleFonts.end();) {
        void* self = gcHandleGetTarget(it->second.weakHandle);
        if (!self || self != it->first) {
            releaseConstrainedTitleFont(it->second);
            it = constrainedTitleFonts.erase(it);
        } else ++it;
    }
}
void* constrainedTitleFont(void* self, void* original, void* normal,
                    const mixed_font_router::UnicodeChar* chars, size_t capacity) {
    if (!objectUnbox || !gcHandleGetTarget || !gcHandleNewWeak || !gcHandleNew || !gcHandleFree) return normal;
    auto existing = constrainedTitleFonts.find(self);
    if (existing != constrainedTitleFonts.end() && gcHandleGetTarget(existing->second.weakHandle) != self) {
        releaseConstrainedTitleFont(existing->second); constrainedTitleFonts.erase(existing); existing = constrainedTitleFonts.end();
    }
    void* sizeBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize");
    void* overflowBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_overflowMode");
    void* transform = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform");
    void* rectBox = transform ? readFontProperty(transform, "UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect") : nullptr;
    if (!sizeBox || !overflowBox || !rectBox) return normal;
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    const int32_t overflow = *static_cast<int32_t*>(objectUnbox(overflowBox));
    auto* rect = static_cast<float*>(objectUnbox(rectBox));
    const auto replacementMetrics = verticalMetrics(normal), originalMetrics = verticalMetrics(original);
    const std::string path = textObjectPath(self);
    const auto kind = constrainedTitle(path);
    float guardSize = size;
    if (kind == ConstrainedTitle::ClippedLabel) {
        void* autoBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_enableAutoSizing");
        if (!autoBox || *static_cast<bool*>(objectUnbox(autoBox))) return normal;
    }
    if (constrainedTitleUsesAutoSize(kind)) {
        // TMP may already have reduced the label to fit the replacement's
        // taller ascent/descent. Validate at its original maximum, retaining
        // automatic width fitting for long text after metrics are restored.
        void* autoBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_enableAutoSizing");
        void* maxBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSizeMax");
        if (!autoBox || !maxBox || !*static_cast<bool*>(objectUnbox(autoBox))) return normal;
        guardSize = *static_cast<float*>(objectUnbox(maxBox));
        if (!std::isfinite(size) || size <= 0 || size > guardSize + .5f) return normal;
    }
    if (!constrainedTitleMetricsGuard(path, rect[2], rect[3], guardSize, overflow, replacementMetrics, originalMetrics)) {
        return normal;
    }
    uint64_t signature = 1469598103934665603ULL;
    size_t length = 0;
    std::u16string glyphs;
    for (size_t i = 0; i < capacity && chars[i].unicode; ++i) {
        const uint32_t c = static_cast<uint32_t>(chars[i].unicode);
        signature = (signature ^ c) * 1099511628211ULL; ++length;
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return normal;
        if (c <= 0xffff) glyphs.push_back(static_cast<char16_t>(c));
        else { glyphs.push_back(static_cast<char16_t>(0xd800 + ((c - 0x10000) >> 10)));
               glyphs.push_back(static_cast<char16_t>(0xdc00 + ((c - 0x10000) & 0x3ff))); }
    }
    if (existing != constrainedTitleFonts.end() && existing->second.signature == signature && existing->second.length == length) {
        if (void* cached = gcHandleGetTarget(existing->second.fontHandle)) {
            const auto mapped = labelOriginalFonts.find(cached);
            if (mapped != labelOriginalFonts.end() && mapped->second == original) {
                return cached;
            }
        }
    }
    // Static clones also need the glyph TMP inserts when a long title is elided.
    glyphs.push_back(u'\u2026');
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    void* clone = createConstrainedFontClone(normal, converter.to_bytes(glyphs));
    if (!clone || !normalizePickerFaceInfo(clone, original)) {
        LOGE("Cannot normalize constrained label font metrics");
        return normal;
    }
    const uint32_t weak = gcHandleNewWeak(self, false);
    if (!weak) return normal;
    const uint32_t handle = gcHandleNew(clone, false);
    if (!handle) { gcHandleFree(weak); return normal; }
    std::vector<uint32_t> retired;
    if (existing != constrainedTitleFonts.end()) {
        void* currentFont = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font");
        retired = std::move(existing->second.retiredHandles);
        retired.push_back(existing->second.fontHandle);
        gcHandleFree(existing->second.weakHandle);
        constrainedTitleFonts.erase(existing);
        // Retain the font still assigned to this label until both font and
        // material setters commit. Failed earlier candidates need no retention.
        for (auto it = retired.begin(); it != retired.end();) {
            void* old = gcHandleGetTarget(*it);
            if (old != currentFont) {
                labelOriginalFonts.erase(old); gcHandleFree(*it); it = retired.erase(it);
            } else ++it;
        }
    }
    constrainedTitleFonts.emplace(self, ConstrainedTitleFont{weak, handle, signature, length, std::move(retired)});
    labelOriginalFonts[clone] = original;
    return clone;
}

void updateFont(void* textObject, bool preserveOriginalFont, const bool* replacementOverride, bool pickerMetrics,
                const mixed_font_router::UnicodeChar* titleBuffer, size_t titleCapacity, bool keepMetricVariant) {
    // Formatted Solis labels can keep get_text empty while their Unicode buffer
    // contains CJK. Only the glyph-lookup buffer is authoritative in this mode;
    // getter-based setter/pre-render calls must not undo its font/material.
    if (usesIndependentFontRouting() && !replacementOverride) return;
    if (!translations.options().fontEnabled || !textObject || !runtimeInvoke) return;
    std::lock_guard<std::recursive_mutex> lock(fontMutex);
    static const auto getInstanceId = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
                                                  "Object", "GetInstanceID", 0);
    void* idException = nullptr;
    void* idBox = getInstanceId ? runtimeInvoke(getInstanceId, textObject, nullptr, &idException) : nullptr;
    const int32_t instanceId = !idException && idBox && objectUnbox
        ? *static_cast<int32_t*>(objectUnbox(idBox)) : 0;
    if (preserveOriginalFont && instanceId) preserveOriginalFontObjects.insert(instanceId);
    const bool preserveLegacyFont = !usesIndependentFontRouting() &&
        (preserveOriginalFont || (instanceId && preserveOriginalFontObjects.count(instanceId)));
    const FontMode effectiveMode = preserveLegacyFont ? FontMode::Fallback : translations.options().fontMode;
    static const auto getFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font", 0);
    static const auto setFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_font", 1);
    static const auto getSharedMaterial = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSharedMaterial", 0);
    static const auto setSharedMaterial = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSharedMaterial", 1);
    static const auto createFallback = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "CreateFontAsset", 1);
    static const auto getFallbacks = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "get_fallbackFontAssetTable", 0);
    static void* assetClass = findClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Asset");
    static void* materialField = assetClass && classGetFieldFromName
        ? classGetFieldFromName(assetClass, "material") : nullptr;
    static const auto getStyledMaterial = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_MaterialManager", "GetFallbackMaterial", 2);
    if (!getFont || !setFont || !createFallback || !getFallbacks || !objectGetClass) return;
    void* exception = nullptr;
    void* asset = runtimeInvoke(getFont, textObject, nullptr, &exception);
    if (exception || !asset) return;
    void* labelReplacement = nullptr;
    StoryFontRole storyRole = StoryFontRole::None;
    void* storySource = nullptr;
    if (usesIndependentFontRouting()) {
        // Route complete labels through an independent replacement asset. The caller
        // selects the current policy; snapshots retain original font/material
        // for numeric counts and design exceptions.
        const auto original = labelOriginalFonts.find(asset);
        const bool usingReplacement = original != labelOriginalFonts.end();
        void* originalAsset = usingReplacement ? original->second : asset;
        static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
        auto* text = !replacementOverride && getText
            ? static_cast<Il2CppString*>(runtimeInvoke(getText, textObject, nullptr, &exception)) : nullptr;
        const bool useReplacement = replacementOverride ? *replacementOverride :
            (!exception && text && text->length > 0 &&
             mixed_font_router::hasCjk(text->chars, static_cast<size_t>(text->length)));
        if (exception || !instanceId || !gcHandleGetTarget || !gcHandleFree) return;
        const auto releaseSnapshot = [](const OriginalLabelStyle& snapshot) {
            gcHandleFree(snapshot.weakHandle);
            gcHandleFree(snapshot.materialHandle);
        };
        auto savedText = originalLabelStyles.find(instanceId);
        if (savedText != originalLabelStyles.end() &&
            gcHandleGetTarget(savedText->second.weakHandle) != textObject) {
            releaseSnapshot(savedText->second);
            originalLabelStyles.erase(savedText);
        }
        // Untouched native labels require neither material retention nor a weak
        // handle. A prefab may already have restored its original font itself.
        if (!useReplacement && !usingReplacement) {
            auto saved = originalLabelStyles.find(instanceId);
            if (saved != originalLabelStyles.end()) {
                // Retry only our own interrupted restore. If the game itself
                // restored its font and changed material, preserve that state.
                if (saved->second.pendingRestoreMaterial) {
                    void* material = gcHandleGetTarget(saved->second.materialHandle);
                    if (!material || !setSharedMaterial) return;
                    void* materialArgs[] = {material};
                    runtimeInvoke(setSharedMaterial, textObject, materialArgs, &exception);
                    if (exception) return;
                }
                commitConstrainedTitleFont(textObject, originalAsset);
                releaseSnapshot(saved->second);
                originalLabelStyles.erase(saved);
            }
            return;
        }
        void* sourceMaterial = getSharedMaterial
            ? runtimeInvoke(getSharedMaterial, textObject, nullptr, &exception) : nullptr;
        if (exception || !sourceMaterial || !setSharedMaterial || !gcHandleNew) return;
        void* roleMaterial = sourceMaterial;
        const auto originalStyle = originalLabelStyles.find(instanceId);
        if (usingReplacement && originalStyle != originalLabelStyles.end())
            roleMaterial = gcHandleGetTarget(originalStyle->second.materialHandle);
        if (useReplacement) {
            storyRole = ordinaryStoryRole(textObject, roleMaterial);
            if (storyRole == StoryFontRole::None && !unityObjectAlive(roleMaterial)) {
                // A pooled prefab can inherit a replaced font and a destroyed
                // material before this particular label has a baseline.
                for (const auto role : {StoryFontRole::Subtitle, StoryFontRole::SpeakerName}) {
                    void* saved = savedStorySource(originalAsset, role);
                    if (saved && ordinaryStoryRole(textObject, saved) == role) {
                        storyRole = role;
                        roleMaterial = saved;
                        break;
                    }
                }
            }
            if (storyRole != StoryFontRole::None) {
                storySource = rememberStorySource(originalAsset, roleMaterial, storyRole);
                if (!unityObjectAlive(roleMaterial) && storySource) roleMaterial = storySource;
            }
        }
        if (!useReplacement) {
            if (usingReplacement) {
                const auto saved = originalLabelStyles.find(instanceId);
                if (saved == originalLabelStyles.end() || !gcHandleGetTarget) return;
                void* material = gcHandleGetTarget(saved->second.materialHandle);
                if (!material) return;
                void* fontArgs[] = {originalAsset};
                runtimeInvoke(setFont, textObject, fontArgs, &exception);
                if (exception) return;
                saved->second.pendingRestoreMaterial = true;
                void* materialArgs[] = {material};
                runtimeInvoke(setSharedMaterial, textObject, materialArgs, &exception);
                if (exception) return;
                commitConstrainedTitleFont(textObject, originalAsset);
                releaseSnapshot(saved->second);
                originalLabelStyles.erase(saved);
            }
            if (!useReplacement) return;
            asset = originalAsset;
        } else {
            // Story fonts need an independent, wider SDF atlas. Do not retain
            // a generic font that was assigned before the label was parented.
            const bool ordinaryStory = storyRole != StoryFontRole::None;
            void* replacementFont = keepMetricVariant && usingReplacement && !ordinaryStory ? asset : nullptr;
            auto& targets = ordinaryStory ? storyReplacementAssets : (pickerMetrics ? pickerReplacementAssets : labelReplacementAssets);
            const auto cached = targets.find(originalAsset);
            if (!replacementFont && cached != targets.end() && gcHandleGetTarget)
                replacementFont = gcHandleGetTarget(cached->second.replacementHandle);
            if (!replacementFont) {
                void* normal = nullptr;
                if (pickerMetrics) {
                    const auto normalCached = labelReplacementAssets.find(originalAsset);
                    if (normalCached != labelReplacementAssets.end() && gcHandleGetTarget)
                        normal = gcHandleGetTarget(normalCached->second.replacementHandle);
                }
                if (!normal) {
                    auto* originalName = static_cast<Il2CppString*>(readFontProperty(originalAsset,
                        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
                    const bool rounded = love_font_policy::usesRoundedReplacement(utf8(originalName));
                    void* font = loadedCjkFont(true, rounded);
                    if (!font) return;
                    void* createArgs[] = {font};
                    normal = ordinaryStory ? createStoryFont(font, originalAsset, roleMaterial)
                        : runtimeInvoke(createFallback, nullptr, createArgs, &exception);
                    if (!normal && ordinaryStory) LOGE("Cannot create ordinary-story SDF font");
                    if (exception || !normal) return;
                    // The game's Love prefabs share this face across dialogue,
                    // choices and menus. Preserve their original vertical metrics
                    // before layout or automatic size fitting sees the new face.
                    if (rounded && !normalizePickerFaceInfo(normal, originalAsset)) return;
                    if (!setCachedFontRetention(normal, true)) {
                        LOGE("Cannot retain cached replacement font resources");
                        return;
                    }
                    if (pickerMetrics) {
                        const uint32_t normalHandle = gcHandleNew(normal, false);
                        if (!normalHandle) return;
                        const uint32_t originalHandle = gcHandleNew(originalAsset, false);
                        if (!originalHandle) { gcHandleFree(normalHandle); return; }
                        labelReplacementAssets[originalAsset] = {normalHandle, originalHandle};
                        labelOriginalFonts[normal] = originalAsset;
                    }
                }
                replacementFont = pickerMetrics ? createConstrainedFontClone(normal) : normal;
                if (!replacementFont) return;
                if (pickerMetrics && !normalizePickerFaceInfo(replacementFont, originalAsset)) {
                    LOGE("Cannot normalize independent picker font metrics");
                    return;
                }
                // Unlike forum-title clones, this picker variant also belongs
                // to the process-lifetime cache and needs its own native root.
                if (pickerMetrics && !setCachedFontRetention(replacementFont, true)) return;
                const uint32_t replacementHandle = gcHandleNew(replacementFont, false);
                if (!replacementHandle) return;
                const uint32_t originalHandle = gcHandleNew(originalAsset, false);
                if (!originalHandle) { gcHandleFree(replacementHandle); return; }
                // Pair lifetime matches the small per-original-font target cache.
                // No original glyph fallback is attached to the full replacement asset.
                targets[originalAsset] = {replacementHandle, originalHandle};
                labelOriginalFonts[replacementFont] = originalAsset;
                LOGI("Created full replacement label font: original=%p replacement=%p", originalAsset, replacementFont);
            }
            if (titleBuffer) replacementFont = constrainedTitleFont(textObject, originalAsset, replacementFont, titleBuffer, titleCapacity);
            if (!usingReplacement) {
                if (!gcHandleNewWeak) return;
                auto previous = originalLabelStyles.find(instanceId);
                if (previous == originalLabelStyles.end()) {
                    // Sweep destroyed labels periodically; instance IDs normally
                    // grow, so waiting for ID reuse would retain dead materials.
                    static uint32_t snapshotInsertions = 0;
                    if ((++snapshotInsertions % 32) == 0) {
                        for (auto it = originalLabelStyles.begin(); it != originalLabelStyles.end();) {
                            if (!gcHandleGetTarget(it->second.weakHandle)) {
                                releaseSnapshot(it->second);
                                it = originalLabelStyles.erase(it);
                            } else ++it;
                        }
                    }
                    const uint32_t weak = gcHandleNewWeak(textObject, false);
                    if (!weak) return;
                    const uint32_t material = gcHandleNew(sourceMaterial, false);
                    if (!material) { gcHandleFree(weak); return; }
                    originalLabelStyles.emplace(instanceId, OriginalLabelStyle{weak, material});
                } else if (gcHandleGetTarget(previous->second.materialHandle) != sourceMaterial) {
                    const uint32_t material = gcHandleNew(sourceMaterial, false);
                    if (!material) return;
                    gcHandleFree(previous->second.materialHandle);
                    previous->second.materialHandle = material;
                }
            }
            // Continue through the existing atlas/style rebinding code below.
            // Use a local target; the shared replace-all fallback stays separate.
            labelReplacement = replacementFont;
        }
    }
    void* cachedFallback = fallbackFontAssetHandle && gcHandleGetTarget
        ? gcHandleGetTarget(fallbackFontAssetHandle) : nullptr;
    if (effectiveMode == FontMode::Fallback && asset == cachedFallback && instanceId) {
        const auto saved = originalTextFonts.find(instanceId);
        if (saved != originalTextFonts.end() && gcHandleGetTarget && setSharedMaterial) {
            void* originalFont = gcHandleGetTarget(saved->second.fontHandle);
            void* originalMaterial = gcHandleGetTarget(saved->second.materialHandle);
            if (originalFont && originalMaterial) {
                void* fontArgs[] = {originalFont};
                runtimeInvoke(setFont, textObject, fontArgs, &exception);
                if (!exception) {
                    void* materialArgs[] = {originalMaterial};
                    runtimeInvoke(setSharedMaterial, textObject, materialArgs, &exception);
                }
                if (!exception) {
                    asset = originalFont;
                    LOGI("Restored original TMP font and material for message picker");
                } else {
                    LOGE("Cannot restore original TMP font and material for message picker");
                    return;
                }
            }
        } else if (preserveOriginalFont) {
            LOGE("Original TMP font snapshot unavailable for message picker");
        }
    }
    if (effectiveMode == FontMode::Fallback &&
        (asset == cachedFallback || updatedFontAssets.find(asset) != updatedFontAssets.end()))
        return;
    void* fallback = labelReplacement ? labelReplacement : cachedFallback;
    if (!fallback) {
        void* font = loadedCjkFont(usesIndependentFontRouting());
        if (!font) return;
        void* args[] = {font};
        fallback = runtimeInvoke(createFallback, nullptr, args, &exception);
        if (exception || !fallback) {
            LOGE("TMP_FontAsset.CreateFontAsset failed: asset=%p exception=%p", fallback, exception);
            return;
        }
        if (!setCachedFontRetention(fallback, true)) {
            LOGE("Cannot retain cached fallback font resources");
            return;
        }
        if (gcHandleNew) fallbackFontAssetHandle = gcHandleNew(fallback, false);
        LOGI("Created dynamic CJK TMP fallback asset");
    }
    if (effectiveMode != FontMode::Fallback) {
        // The game's subtitle and speaker-label materials carry different outlines,
        // colors, and masking settings. TMP's material manager copies those settings
        // while binding the atlas texture of the replacement font.
        if (!getSharedMaterial || !setSharedMaterial || !materialField || !fieldGetValue ||
            !getStyledMaterial) {
            static bool reported = false;
            if (!reported) {
                LOGE("TMP material access unavailable: get=%p set=%p field=%p read=%p styled=%p",
                     getSharedMaterial, setSharedMaterial, materialField,
                     fieldGetValue, getStyledMaterial);
                reported = true;
            }
            return;
        }
        void* sourceMaterial = runtimeInvoke(getSharedMaterial, textObject, nullptr, &exception);
        if (!exception && storyRole != StoryFontRole::None &&
            !unityObjectAlive(sourceMaterial) && unityObjectAlive(storySource)) {
            sourceMaterial = storySource;
            LOGI("Recovered ordinary-story source material after scene release: role=%d", static_cast<int>(storyRole));
        }
        if (exception || !sourceMaterial) {
            LOGE("TMP source material unavailable");
            return;
        }
        if (!usesIndependentFontRouting() && instanceId && asset != fallback && gcHandleNew &&
            originalTextFonts.find(instanceId) == originalTextFonts.end()) {
            static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine",
                                                   "Object", "get_name", 0);
            void* nameException = nullptr;
            auto* name = getName
                ? static_cast<Il2CppString*>(runtimeInvoke(getName, textObject, nullptr, &nameException))
                : nullptr;
            if (!nameException && utf8(name) == "Text") {
                auto retain = [](void* object) -> uint32_t {
                    const auto found = retainedFontObjects.find(object);
                    if (found != retainedFontObjects.end()) return found->second;
                    const uint32_t handle = gcHandleNew(object, false);
                    if (handle) retainedFontObjects.emplace(object, handle);
                    return handle;
                };
                const uint32_t fontHandle = retain(asset);
                const uint32_t materialHandle = retain(sourceMaterial);
                if (fontHandle && materialHandle)
                    originalTextFonts.emplace(instanceId, OriginalTextFont{fontHandle, materialHandle});
            }
        }
        void* targetMaterial = nullptr;
        fieldGetValue(fallback, materialField, &targetMaterial);
        if (!targetMaterial) {
            LOGE("TMP replacement material unavailable");
            return;
        }
        // Scene changes can restore a prefab's original material after its font
        // asset was replaced. A font-only guard would leave that material bound
        // to the old SDF atlas, making subsequent text invisible.
        void* currentAtlas = materialTexture(sourceMaterial, "_MainTex");
        void* replacementAtlas = materialTexture(targetMaterial, "_MainTex");
        if (asset == fallback && currentAtlas && currentAtlas == replacementAtlas) {
            refreshStoryMeshPadding(textObject, sourceMaterial, storyRole);
            void* localMaterial = rouletteOutlineMaterial(textObject,sourceMaterial);
            if (localMaterial != sourceMaterial) {
                void* localArgs[] = {localMaterial};
                runtimeInvoke(setSharedMaterial,textObject,localArgs,&exception);
                if (exception) return;
            }
            commitConstrainedTitleFont(textObject, fallback);
            return;
        }
        if (asset == fallback) {
            static int reportedRebinds = 0;
            if (reportedRebinds++ < 16)
                LOGI("Rebinding TMP material after scene reset");
        }
        void* materialArgs[] = {sourceMaterial, targetMaterial};
        void* styledMaterial = runtimeInvoke(getStyledMaterial, nullptr, materialArgs, &exception);
        if (exception || !unityObjectAlive(styledMaterial)) {
            // GetFallbackMaterial can return null when a scene restores an old
            // material onto a TMP object whose font asset is already replaced.
            // Clone that exact material and bind the new atlas instead of
            // leaving the text invisible or losing its per-object styling.
            static const auto instantiate = instantiateUnityObject();
            exception = nullptr;
            void* cloneArgs[] = {sourceMaterial};
            styledMaterial = instantiate
                ? runtimeInvoke(instantiate, nullptr, cloneArgs, &exception) : nullptr;
            float newScale = 0.0f;
            const bool cloned = !exception && styledMaterial;
            const bool textureBound = cloned && replacementAtlas &&
                setMaterialTexture(styledMaterial, "_MainTex", replacementAtlas);
            const bool scaleRead = textureBound && materialFloat(targetMaterial, "_GradientScale", newScale);
            bool scaleBound = scaleRead && setMaterialFloat(styledMaterial, "_GradientScale", newScale);
            // Match TMP's fallback material factory: atlas dimensions and
            // weight metrics belong to the replacement font as well.
            for (const char* property : {"_TextureWidth", "_TextureHeight", "_WeightNormal", "_WeightBold"}) {
                float value = 0;
                scaleBound = scaleBound && materialFloat(targetMaterial, property, value) &&
                    setMaterialFloat(styledMaterial, property, value);
            }
            if (!scaleBound) {
                static int reports = 0;
                if (reports++ < 24) {
                    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
                    const auto name = [&](void* object) {
                        void* error = nullptr;
                        auto* value = object && getName
                            ? static_cast<Il2CppString*>(runtimeInvoke(getName, object, nullptr, &error)) : nullptr;
                        return error ? std::string("<unavailable>") : utf8(value);
                    };
                    LOGE("Cannot rebind TMP material: path=%s original=%s replacement=%s source_material=%s target_material=%s clone=%d atlas=%d bind=%d scale_read=%d scale_bind=%d",
                         textObjectPath(textObject).c_str(), name(asset).c_str(), name(fallback).c_str(),
                         name(sourceMaterial).c_str(), name(targetMaterial).c_str(), cloned,
                         replacementAtlas != nullptr, textureBound, scaleRead, scaleBound);
                }
                return;
            }
            static int reportedManualRebinds = 0;
            if (reportedManualRebinds++ < 16)
                LOGI("Rebound TMP material to replacement atlas manually");
        }
        float sourceScale, targetScale, outlineWidth;
        if (storyRole == StoryFontRole::None && materialFloat(sourceMaterial, "_GradientScale", sourceScale) &&
            materialFloat(targetMaterial, "_GradientScale", targetScale) &&
            materialFloat(sourceMaterial, "_OutlineWidth", outlineWidth) &&
            sourceScale > 0.0f && targetScale > 0.0f && outlineWidth > 0.0f) {
            // The shader multiplies outline width by GradientScale. The TMP
            // fallback material keeps the new atlas scale but copies the old
            // width, so compensate for the change in SDF distance range.
            const float adjusted = std::clamp(outlineWidth * sourceScale / targetScale,
                                              0.0f, 1.0f);
            if (!setMaterialFloat(styledMaterial, "_OutlineWidth", adjusted)) {
                LOGE("Cannot normalize TMP outline width");
            } else if (std::fabs(adjusted - outlineWidth) > 0.001f) {
                static int reported = 0;
                if (reported++ < 12) {
                    LOGI("Normalized TMP outline %.3f -> %.3f (SDF scale %.1f -> %.1f)",
                         outlineWidth, adjusted, sourceScale, targetScale);
                }
            }
            float faceDilate = 0.0f;
            if (outlineWidth >= 0.45f &&
                materialFloat(sourceMaterial, "_FaceDilate", faceDilate) &&
                faceDilate >= 0.45f) {
                // The speaker-label preset dilates its face by 0.5. With the
                // new dynamic SDF atlas that fills the tiny label's glyph quad.
                // Keep the pale outline, but remove the incompatible dilation.
                if (!setMaterialFloat(styledMaterial, "_FaceDilate", 0.0f))
                    LOGE("Cannot normalize TMP speaker-label face dilation");
            }
        }
        void* assignedMaterial = styledMaterial;
        if (replacesCjk()) {
            if (storyRole == StoryFontRole::None) storyRole = ordinaryStoryRole(textObject, sourceMaterial);
            if (storyRole != StoryFontRole::None) {
                void* isolated = storyMaterial(styledMaterial, storyRole, storySource);
                if (isolated) assignedMaterial = isolated;
                else LOGE("Cannot isolate story material");
            }
        }
        assignedMaterial = rouletteOutlineMaterial(textObject,assignedMaterial);
        void* args[] = {fallback};
        runtimeInvoke(setFont, textObject, args, &exception);
        if (exception) { LOGE("TMP_Text.set_font failed"); return; }
        void* styleArgs[] = {assignedMaterial};
        runtimeInvoke(setSharedMaterial, textObject, styleArgs, &exception);
        if (exception) { LOGE("TMP_Text.set_fontSharedMaterial failed"); return; }
        if (storyRole != StoryFontRole::None) {
            if (auto* baseline = fontBaseline(textObject)) baseline->storyOutlineGeneration = 0;
            refreshStoryMeshPadding(textObject, assignedMaterial, storyRole);
        }
        commitConstrainedTitleFont(textObject, fallback);
        static int reportedFontUpdates = 0;
        if (reportedFontUpdates++ < 24)
            LOGI("Replaced TMP font while preserving material style");
        return;
    }
    void* list = runtimeInvoke(getFallbacks, asset, nullptr, &exception);
    if (exception || !list) {
        LOGE("TMP fallback list unavailable: list=%p exception=%p", list, exception);
        return;
    }
    const auto add = classGetMethodFromName(objectGetClass(list), "Add", 1);
    if (!add) { LOGE("TMP fallback List.Add unavailable"); return; }
    void* args[] = {fallback};
    runtimeInvoke(add, list, args, &exception);
    if (exception) { LOGE("TMP fallback List.Add failed"); return; }
    updatedFontAssets.insert(asset);
    LOGI("Attached CJK fallback to TMP font asset");
}

void* storyPunctuationFont(void* self) {
    if (!translations.options().fontEnabled || !usesIndependentFontRouting() || !self) return nullptr;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    const auto label = utf8(name);
    if (label != "Message Label" && label != "Message Text" && label != "Serif") return nullptr;
    const char* profile = story_punctuation::profile(textObjectPath(self));
    if (!profile || !profileFlag(profile, "original_punctuation")) return nullptr;
    void* font = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font");
    const auto original = labelOriginalFonts.find(font);
    return original != labelOriginalFonts.end() && unityObjectAlive(original->second) ? original->second : nullptr;
}

void* hookGetCharacter(uint32_t unicode, void* font, bool fallbacks, int32_t style,
                       int32_t weight, bool* alternative, void* method) {
    // Let TMP build its normal per-character font/material references. No rich
    // text is inserted, so the game's typewriter offsets and logs stay unchanged.
    if (currentStoryPunctuationFont && font != currentStoryPunctuationFont &&
        story_punctuation::isPunctuation(unicode) && labelOriginalFonts.count(font)) {
        bool nativeAlternative = false;
        void* character = origGetCharacter(unicode, currentStoryPunctuationFont, false,
            style, weight, &nativeAlternative, method);
        if (character) {
            if (alternative) *alternative = nativeAlternative;
            return character;
        }
    }
    return origGetCharacter(unicode, font, fallbacks, style, weight, alternative, method);
}

void routeTmpFontAfterSetter(void* textObject) {
    if (!translations.options().fontEnabled || !usesIndependentFontRouting() || !textObject) return;
    updateFont(textObject);
    applyStorySubtitleLayout(textObject);
}

void routeTmpFontBeforeGlyphLookup(void* self, void* array) {
    if (!translations.options().fontEnabled || !usesIndependentFontRouting() || !self || !array) return;
    // This 64-bit IL2CPP array carries final UnicodeChar text, including TMP's
    // formatted SetText/SetCharArray paths whose string getter may lag behind.
    auto* data = static_cast<char*>(array);
    const size_t count = *reinterpret_cast<size_t*>(data + 24);
    if (count > 65536) return;
    const auto* chars = reinterpret_cast<const mixed_font_router::UnicodeChar*>(data + 32);
    const bool numericSymbols = ui_font_policy::isNumericSymbolBuffer(chars, count);
    if (translations.options().fontMode == FontMode::ReplaceUi && isTmpUi(self)) {
        if (auto* baseline = fontBaseline(self)) {
            baseline->glyphFontRouteKnown = true;
            baseline->glyphNumericSymbols = numericSymbols;
        }
    }
    const bool useReplacement = translations.options().fontMode == FontMode::ReplaceUi
        ? ui_font_policy::usesReplacement(uiFontPolicyPath(self), numericSymbols)
        : (mixed_font_router::hasCjkBuffer(chars, count) ||
           (count >= 5 && (chars[0].unicode == '3' || chars[0].unicode == 'L') &&
            panorama_font_labels::shouldUseReplacement(textObjectPath(self), chars, count)) ||
           (mixed_font_router::isKnownShowcaseBgmBuffer(chars, count) &&
            mixed_font_router::isShowcaseBgmHeader(textObjectPath(self))));
    const bool messagePicker = count >= 5 && chars[0].unicode == 0x9009 &&
        chars[1].unicode == 0x62e9 && chars[2].unicode == 0x6d88 &&
        chars[3].unicode == 0x606f && chars[4].unicode == 0;
    // Switch before SetArraySizes chooses glyphs, materials and UV references.
    // The original method below rebuilds them coherently from the chosen font.
    bool constrainedTitle = false;
    if (!messagePicker) {
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        void* error = nullptr;
        auto* name = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error)) : nullptr;
        const auto objectName = error ? std::string() : utf8(name);
        constrainedTitle = constrainedTitleCandidateName(objectName) &&
            ::constrainedTitle(textObjectPath(self)) != ConstrainedTitle::None;
    }
    const bool trackSubmit = usesIndependentFontRouting() && isTmpUi(self);
    void* beforeFont = trackSubmit ? readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_font") : nullptr;
    void* beforeMaterial = trackSubmit ? readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_fontSharedMaterial") : nullptr;
    updateFont(self, false, &useReplacement, messagePicker, constrainedTitle ? chars : nullptr, constrainedTitle ? count : 0);
    applyStorySubtitleLayout(self);
    applyPortraitNameLayout(self);
    if (trackSubmit) {
        void* afterFont = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_font");
        void* afterMaterial = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_fontSharedMaterial");
        if (beforeFont != afterFont || beforeMaterial != afterMaterial) {
            if (auto* baseline = fontBaseline(self)) baseline->pendingRendererSubmit = true;
        }
    }
}
// SetText rebuilds the processing array even when the stale string getter
// already equals desired. Formatted setters bypass set_text in these prefabs.
void* rebuildTmpGlyphBuffer(void* self, void* array, const std::string& desired, bool* applied = nullptr) {
    static thread_local bool updating = false;
    if (updating || desired.empty() || !self || !array || !runtimeInvoke || !fieldGetValue ||
        !classGetFieldFromName || !stringNew) return array;
    static void* klass = findClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Text");
    static void* processingField = klass ? classGetFieldFromName(klass, "m_TextProcessingArray") : nullptr;
    static const auto setText = []() -> const MethodInfoHead* {
        if (!klass || !classGetMethods || !methodGetName || !methodGetParamCount ||
            !methodGetParam || !typeGetName || !il2cppFree) return nullptr;
        const auto parameterIs = [](const MethodInfoHead* method, int index, const char* expected) {
            char* name = typeGetName(methodGetParam(method, index));
            const bool matches = name && std::strcmp(name, expected) == 0;
            if (name) il2cppFree(name);
            return matches;
        };
        void* iterator = nullptr;
        while (const auto* method = classGetMethods(klass, &iterator)) {
            const uint32_t count = methodGetParamCount(method);
            if (std::strcmp(methodGetName(method), "SetText") != 0 || (count != 1 && count != 2)) continue;
            if (parameterIs(method, 0, "System.String") &&
                (count == 1 || parameterIs(method, 1, "System.Boolean"))) return method;
        }
        return nullptr;
    }();
    if (!processingField || !setText) {
        static int reports = 0;
        if (reports++ < 4) LOGE("Formatted UI buffer API unavailable: field=%d setter=%d", processingField != nullptr, setText != nullptr);
        return array;
    }
    auto* replacement = stringNew(desired.c_str());
    if (!replacement) return array;
    updating = true;
    bool sync = true;
    void* args[] = {replacement, &sync};
    void* error = nullptr;
    runtimeInvoke(setText, self, args, &error);
    void* updated = nullptr;
    if (!error) fieldGetValue(self, processingField, &updated);
    updating = false;
    const auto* result = static_cast<const Il2CppByteArray*>(updated);
    const auto* output = result ? reinterpret_cast<const mixed_font_router::UnicodeChar*>(result->data) : nullptr;
    bool complete = result && result->length > static_cast<size_t>(replacement->length) &&
        result->length <= 65536;
    for (int32_t i = 0; complete && i <= replacement->length; ++i)
        complete = output[i].unicode == (i == replacement->length ? 0 : replacement->chars[i]);
    if (!complete) { LOGE("Cannot rebuild formatted UI glyph buffer"); return array; }
    if (applied) *applied = true;
    return updated;
}

void* prepareLoveEpisodeGlyphs(void* self, void* array) {
    if (!self || !array) return array;
    const auto* input = static_cast<const Il2CppByteArray*>(array);
    if (input->length > 65536) return array;
    const auto* chars = reinterpret_cast<const mixed_font_router::UnicodeChar*>(input->data);
    const auto number = love_episode_label::numberBuffer(input->length,
        [&](size_t i) { return chars[i].unicode; });
    if (!number || textObjectPath(self) != love_episode_label::path) return array;
    const auto desired = love_episode_label::format(love_episode_label::path, *number);
    return desired ? rebuildTmpGlyphBuffer(self, array, *desired) : array;
}

void* prepareFormattedUiGlyphs(void* self, void* array) {
    if (!self || !array || !runtimeInvoke) return array;
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    const auto* input = static_cast<const Il2CppByteArray*>(array);
    if (input->length == 0 || input->length > 65536) return array;
    const auto* chars = reinterpret_cast<const mixed_font_router::UnicodeChar*>(input->data);
    std::u16string decoded;
    size_t i = 0;
    for (; i < input->length && i < 256 && chars[i].unicode; ++i) {
        const auto c = static_cast<uint32_t>(chars[i].unicode);
        // These reviewed short UI labels contain BMP characters only.
        if (c > 0xffff || (c >= 0xd800 && c <= 0xdfff)) return array;
        decoded.push_back(static_cast<char16_t>(c));
    }
    if (i >= input->length || i >= 256 || decoded.empty()) return array;
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    const auto text = converter.to_bytes(decoded);
    const bool medal = !guild_medal_font::decorate(text, "probe").empty();
    const bool activity = translations.catalog().homeActivityNames.mayMatch(text);
    const auto durationPattern = translations.catalog().i18n.find("activity.characterSelect.duration");
    const bool duration = durationPattern != translations.catalog().i18n.end() &&
        translations.catalog().homeActivityNames.mayMatchDuration(text, durationPattern->second);
    if (!medal && !activity && !duration) return array;
    const auto path = textObjectPath(self);
    std::string desired;
    if (medal && guild_medal_font::matches(path)) desired = guildMedalRenderedText(self, text);
    else if (activity) desired = translations.catalog().homeActivityNames.translate(text, path);
    else if (duration) desired = translations.catalog().homeActivityNames.translateDuration(text, path, durationPattern->second);
    if (desired.empty() || desired == text) return array;
    bool applied = false;
    void* updated = rebuildTmpGlyphBuffer(self, array, desired, &applied);
    if (applied) {
        static int reports = 0;
        if (reports++ < 16) LOGI("Formatted UI glyph buffer repaired: kind=%s", medal ? "guild-medal" : "activity-name");
    }
    return updated;
}

int32_t hookTmpUiSetArraySizes(void* self, void* array, void* method) {
    array = prepareLoveEpisodeGlyphs(self, array);
    array = prepareFormattedUiGlyphs(self, array);
    routeTmpFontBeforeGlyphLookup(self, array);
    StoryPunctuationScope punctuation(self);
    const int32_t result = origTmpUiSetArraySizes(self, array, method);
    return result;
}
int32_t hookTmp3dSetArraySizes(void* self, void* array, void* method) {
    routeTmpFontBeforeGlyphLookup(self, array);
    StoryPunctuationScope punctuation(self);
    const int32_t result = origTmp3dSetArraySizes(self, array, method);
    return result;
}

template <typename T>
bool installMethod(const char* label, void* target, void* replacement, T& original) {
    if (!target) {
        LOGI("%s method unavailable", label);
        return false;
    }
    void* trampoline = nullptr;
    void* stub = shadowhook_hook_func_addr(target, replacement, &trampoline);
    if (!stub || !trampoline) {
        LOGE("%s hook failed: %s", label, shadowhook_to_errmsg(shadowhook_get_errno()));
        return false;
    }
    original = reinterpret_cast<T>(trampoline);
    LOGI("%s hook installed", label);
    return true;
}

#include "runtime_ui_images.inc"

void installTextHooks() {
    if (hooksInstalled.exchange(true)) return;
    int installed = 0;
    if (!runtimeAtlases.empty()) {
        installed += installMethod("CanvasRenderer.SetTexture",
            methodPointer("UnityEngine.UIModule.dll", "UnityEngine", "CanvasRenderer", "SetTexture", 1),
            reinterpret_cast<void*>(hookCanvasSetTexture), origCanvasSetTexture);
    }
    installStorySwapCastHook();
    story_bridge::install([](const char* assembly, const char* ns,
                             const char* klass, const char* name, int count) -> void* {
        const auto* info = methodInfo(assembly, ns, klass, name, count);
        if (!info || !methodGetFlags || !methodGetParamCount || !methodGetReturnType ||
            !typeGetName || !il2cppFree) return nullptr;
        uint32_t implementationFlags = 0;
        if ((methodGetFlags(info, &implementationFlags) & 0x0010u) ||
            methodGetParamCount(info) != static_cast<uint32_t>(count)) return nullptr;
        char* returnName = typeGetName(methodGetReturnType(info));
        const bool isVoid = returnName && std::strcmp(returnName, "System.Void") == 0;
        if (returnName) il2cppFree(returnName);
        if (!isVoid || count > 1) return nullptr;
        if (count == 1) {
            if (!methodGetParam) return nullptr;
            char* parameterName = typeGetName(methodGetParam(info, 0));
            const bool transitionInfo = parameterName &&
                std::strcmp(parameterName, "Qua.UI.TransitionInfo") == 0;
            if (parameterName) il2cppFree(parameterName);
            using ClassFromType = void* (*)(const void*);
            using IsValueType = bool (*)(void*);
            static const auto fromType = symbol<ClassFromType>("il2cpp_class_from_type");
            static const auto isValueType = symbol<IsValueType>("il2cpp_class_is_valuetype");
            if (!transitionInfo || !fromType || !isValueType) return nullptr;
            void* parameterClass = fromType(methodGetParam(info, 0));
            if (!parameterClass || isValueType(parameterClass)) return nullptr;
        }
        return info->methodPointer;
    }, [](void* self) {
        static void* engineClass = findClass("ADV.Runtime.dll", "Solis.ADV", "ADVEngine");
        return self && engineClass && objectGetClass && classIsAssignableFrom &&
            classIsAssignableFrom(engineClass, objectGetClass(self));
    }, [](void* self) -> void* {
        if (!self || !objectGetClass || !classGetName || !classGetNamespace ||
            !classGetFieldFromName || !fieldGetValue || !classIsAssignableFrom ||
            !runtimeInvoke || !classGetMethodFromName) return nullptr;
        void* klass = objectGetClass(self);
        if (!klass || std::strcmp(classGetNamespace(klass), "Solis.OutGame") != 0) return nullptr;
        const char* name = classGetName(klass);
        if (std::strcmp(name, "StoryPlayerScreenPresenter") != 0 &&
            std::strcmp(name, "StoryPreviewPlayerScreenPresenter") != 0 &&
            std::strcmp(name, "LoveAdvPlayerScreenPresenter") != 0 &&
            std::strcmp(name, "VerticalAdvPlayerScreenPresenter") != 0) return nullptr;
        // All four player presenters declare _engine in the v31 metadata.
        // Resolve by field metadata, never a hard-coded native object offset.
        void* field = classGetFieldFromName(klass, "_engine");
        using FieldGetType = const void* (*)(void*);
        using ClassFromType = void* (*)(const void*);
        using IsValueType = bool (*)(void*);
        static const auto fieldType = symbol<FieldGetType>("il2cpp_field_get_type");
        static const auto fromType = symbol<ClassFromType>("il2cpp_class_from_type");
        static const auto isValueType = symbol<IsValueType>("il2cpp_class_is_valuetype");
        if (!field || !fieldType || !fromType || !isValueType) return nullptr;
        void* fieldClass = fromType(fieldType(field));
        if (!fieldClass || isValueType(fieldClass)) return nullptr;
        void* presenter = nullptr;
        fieldGetValue(self, field, &presenter);
        if (!presenter) return nullptr;
        // _engine is the ADV presenter. Its inherited get_Engine returns the
        // common Solis.ADV.ADVEngine (including Love/vertical presenters).
        const auto* getter = classGetMethodFromName(objectGetClass(presenter), "get_Engine", 0);
        if (!getter || !methodGetReturnType || !typeGetName || !il2cppFree) return nullptr;
        char* resultType = typeGetName(methodGetReturnType(getter));
        const bool validResult = resultType && std::strcmp(resultType, "Solis.ADV.ADVEngine") == 0;
        if (resultType) il2cppFree(resultType);
        if (!validResult) return nullptr;
        void* exception = nullptr;
        void* engine = runtimeInvoke(getter, presenter, nullptr, &exception);
        static void* engineClass = findClass("ADV.Runtime.dll", "Solis.ADV", "ADVEngine");
        return !exception && engine && engineClass &&
            classIsAssignableFrom(engineClass, objectGetClass(engine)) ? engine : nullptr;
    });
    installed += installMethod("TMP_Text.set_text",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_text", 1),
        reinterpret_cast<void*>(hookTmpSetText), origTmpSetText);
    installed += installMethod("TMP_FontAssetUtilities.GetCharacterFromFontAsset",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAssetUtilities", "GetCharacterFromFontAsset", 6),
        reinterpret_cast<void*>(hookGetCharacter), origGetCharacter);
    installed += installMethod("TextMeshProUGUI.OnEnable",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI", "OnEnable", 0),
        reinterpret_cast<void*>(hookTmpOnEnable), origTmpOnEnable);
    installed += installMethod("TextMeshProUGUI.OnPreRenderCanvas",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI", "OnPreRenderCanvas", 0),
        reinterpret_cast<void*>(hookTmpPreRender), origTmpPreRender);
    installed += installMethod("Qua.UI.FitToChild.SetLayout",
        methodPointer("quaunity-ui.Runtime.dll", "Qua.UI", "FitToChild", "SetLayout", 1),
        reinterpret_cast<void*>(hookActivityFitToChildLayout), origFitToChildLayout);
    const auto* canvasWillRender = methodInfo("UnityEngine.UIModule.dll", "UnityEngine",
        "Canvas", "SendWillRenderCanvases", 0);
    bool validCanvasWillRender = false;
    if (canvasWillRender && canvasWillRender->methodPointer && methodGetFlags &&
        methodGetParamCount && methodGetReturnType && typeGetName && il2cppFree) {
        uint32_t implementationFlags = 0;
        const uint32_t flags = methodGetFlags(canvasWillRender, &implementationFlags);
        char* resultType = typeGetName(methodGetReturnType(canvasWillRender));
        validCanvasWillRender = (flags & 0x0010u) && methodGetParamCount(canvasWillRender) == 0 &&
            resultType && std::strcmp(resultType, "System.Void") == 0;
        if (resultType) il2cppFree(resultType);
    }
    installed += installMethod("UnityEngine.Canvas.SendWillRenderCanvases",
        validCanvasWillRender ? canvasWillRender->methodPointer : nullptr,
        reinterpret_cast<void*>(hookCanvasWillRender), origCanvasWillRender);
    installed += installMethod("TMP_Text.SetText(string)",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "SetText", 1),
        reinterpret_cast<void*>(hookTmpSetTextOne), origTmpSetTextOne);
    installed += installMethod("TMP_Text.SetText(string,bool)",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "SetText", 2),
        reinterpret_cast<void*>(hookTmpSetTextTwo), origTmpSetTextTwo);
    installed += installMethod("TextMeshProUGUI.SetArraySizes",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI", "SetArraySizes", 1),
        reinterpret_cast<void*>(hookTmpUiSetArraySizes), origTmpUiSetArraySizes);
    installed += installMethod("TextMeshPro.SetArraySizes",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TextMeshPro", "SetArraySizes", 1),
        reinterpret_cast<void*>(hookTmp3dSetArraySizes), origTmp3dSetArraySizes);
    installed += installMethod("UnityEngine.UI.Text.set_text",
        methodPointer("UnityEngine.UI.dll", "UnityEngine.UI", "Text", "set_text", 1),
        reinterpret_cast<void*>(hookLegacySetText), origLegacySetText);
    installed += installMethod("Qua.UI.I18n.SetValue",
        methodPointer("quaunity-ui.Runtime.dll", "Qua.UI", "I18n", "SetValue", 2),
        reinterpret_cast<void*>(hookI18nSetValue), origI18nSetValue);
    if (masterBlobPatches.size()) {
        installMethod("Qua.Master.SQLite.ResultSet.GetBlob",
            methodPointer("quaunity-master-manager.Runtime.dll", "Qua.Master.SQLite",
                          "ResultSet", "GetBlob", 3),
            reinterpret_cast<void*>(hookGetBlob), origGetBlob);
    }
    if (translations.options().captureOcto) {
        installMethod("Octo.OctoManager.Setup",
            methodPointer("Octo.dll", "Octo", "OctoManager", "Setup", 2),
            reinterpret_cast<void*>(hookOctoSetup), origOctoSetup);
    }
    if (translations.options().resourceReplace) {
        void* klass = findClass("Octo.dll", "Octo.Loader", "OctoResourceLoader");
        void* iterator = nullptr;
        while (klass && classGetMethods && methodGetName && methodGetParamCount &&
               methodGetParam && typeGetName) {
            const auto* candidate = classGetMethods(klass, &iterator);
            if (!candidate) break;
            const char* label = methodGetName(candidate);
            if (!label || std::string(label) != "LoadFromCacheOrDownload" ||
                methodGetParamCount(candidate) != 3 || !candidate->methodPointer) continue;
            char* rawType = typeGetName(methodGetParam(candidate, 1));
            std::string callbackType = rawType ? rawType : "";
            if (rawType && il2cppFree) il2cppFree(rawType);
            LOGI("Octo resource callback type: %s", callbackType.c_str());
            if (callbackType.find("System.String") == std::string::npos ||
                callbackType.find("Octo.LoadError") == std::string::npos) continue;
            if (installMethod("OctoResourceLoader.LoadFromCacheOrDownload",
                              candidate->methodPointer,
                              reinterpret_cast<void*>(hookResourceLoad),
                              origResourceLoad)) break;
        }
        LOGI("Octo text resource hook %s", origResourceLoad ? "ready" : "unavailable");
    }
    if (translations.options().assetReplace && classGetMethods && methodGetName && methodGetParamCount &&
        methodGetParam && typeGetName) {
        void* klass = findClass("Octo.dll", "Octo.Loader", "OctoAssetBundleLoader");
        void* iterator = nullptr;
        while (klass) {
            const auto* candidate = classGetMethods(klass, &iterator);
            if (!candidate) break;
            const char* label = methodGetName(candidate);
            if (!label || std::string(label) != "LoadFromCacheOrDownload" ||
                methodGetParamCount(candidate) != 3 || !candidate->methodPointer) continue;
            char* rawType = typeGetName(methodGetParam(candidate, 1));
            std::string callbackType = rawType ? rawType : "";
            if (rawType && il2cppFree) il2cppFree(rawType);
            if (translations.options().dumpEnabled) LOGI("Octo asset callback type: %s", callbackType.c_str());
            if (callbackType.find("UnityEngine.AssetBundle") == std::string::npos ||
                callbackType.find("Octo.LoadError") == std::string::npos) continue;
            if (installMethod("OctoAssetBundleLoader.LoadFromCacheOrDownload",
                              candidate->methodPointer,
                              reinterpret_cast<void*>(hookAssetLoad),
                              origAssetLoad)) break;
        }
        LOGI("Octo asset bundle hook %s", origAssetLoad ? "ready" : "unavailable");
    }
    LOGI("Installed %d text hooks", installed);
}

int hookInit(const char* domainName) {
    int result = origInit(domainName);
    logMasterMethods();
    installTextHooks();
    return result;
}

std::string javaString(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

bool loadDictionary(const std::string& path, bool reload = false,
                    bool* restartRequired = nullptr, bool deferApply = false) {
    if (restartRequired) *restartRequired = false;
    try {
        const auto status = translations.load(path, reload, deferApply, restartRequired != nullptr);
        if (status == localify::DictionaryUpdate::RestartRequired) {
            *restartRequired = true;
            LOGI("Validated dictionary requires a game restart; live state retained");
        } else {
            std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
            const auto& catalog = translations.catalog();
            LOGI("Loaded %zu text, %zu legal, %zu i18n, %zu source variants, %zu unique translated values and %zu player-name templates; dump=%d",
                 catalog.text.size(), catalog.legal.size(), catalog.i18n.size(), catalog.variantCount,
                 catalog.translatedValues.size(), catalog.userTemplates.size(), translations.options().dumpEnabled);
        }
        return true;
    } catch (const std::exception& error) {
        LOGE("Cannot parse dictionary: %s", error.what());
        return false;
    }
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeValidateMasterBlobs(
    JNIEnv* env, jclass, jstring path) {
    if (!path) return JNI_FALSE;
    try {
        MasterBlobMatcher candidate;
        candidate.load(javaString(env, path));
        return JNI_TRUE;
    } catch (const std::exception&) {
        LOGE("Downloaded MasterDB patch failed validation");
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeReloadDictionary(
    JNIEnv* env, jclass, jstring dictionaryPath) {
    if (!hooksInstalled.load() || !dictionaryPath) return JNI_FALSE;
    return loadDictionary(javaString(env, dictionaryPath), true) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeUpdateDictionary(
    JNIEnv* env, jclass, jstring dictionaryPath, jboolean deferApply) {
    // Matches DictionaryUpdateStatus. No live mutation on INVALID/RESTART_REQUIRED.
    if (!hooksInstalled.load() && deferApply != JNI_TRUE) return -1;
    if (!dictionaryPath) return 0;
    bool restartRequired = false;
    if (!loadDictionary(javaString(env, dictionaryPath), true, &restartRequired,
                        deferApply == JNI_TRUE)) return 0;
    return restartRequired ? 2 : 1;
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeGetFontProfiles(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(profileMutex);
    return env->NewStringUTF(fontProfiles.dump().c_str());
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeApplyFontProfiles(JNIEnv* env, jclass, jstring encoded) {
    if (!encoded) return JNI_FALSE;
    FontProfileConfig candidate;
    if (!candidate.assign(javaString(env, encoded))) return JNI_FALSE;
    std::lock_guard<std::recursive_mutex> frameLock(translations.frameMutex());
    std::lock_guard<std::mutex> lock(profileMutex);
    fontProfiles = std::move(candidate);
    fontProfileGeneration.fetch_add(1);
    return JNI_TRUE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeResetFontProfiles(JNIEnv*, jclass) {
    std::lock_guard<std::recursive_mutex> frameLock(translations.frameMutex());
    std::lock_guard<std::mutex> lock(profileMutex);
    fontProfiles = FontProfileConfig();
    fontProfileGeneration.fetch_add(1);
    return JNI_TRUE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeSetTextCapture(JNIEnv*, jclass, jboolean enabled) {
    std::lock_guard<std::mutex> lock(dumpMutex);
    if (!enabled) {
        captureAcceptingRecords.store(false);
        textCaptureEnabled.store(false);
        captureSession.stop();
        return JNI_TRUE;
    }
    if (textCaptureEnabled.load()) return JNI_TRUE;
    if (dumpPath.empty()) return JNI_FALSE;
    const std::string directory = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/capture";
    mkdir(directory.c_str(), 0700);
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string path = directory + "/text-" + std::to_string(stamp) + ".jsonl";
    // Never append to another session if the system clock repeats a timestamp.
    const std::string layoutPath = path.substr(0, path.size() - 6) + ".layout.jsonl";
    if (std::ifstream(path) || std::ifstream(layoutPath)) return JNI_FALSE;
    std::ofstream output(path);
    if (!output) return JNI_FALSE;
    output.close();
    std::ofstream layout(layoutPath);
    const bool layoutOpened = static_cast<bool>(layout);
    layout.close();
    if (output.fail() || !layoutOpened || layout.fail()) {
        std::remove(path.c_str());
        std::remove(layoutPath.c_str());
        return JNI_FALSE;
    }
    chmod(path.c_str(), 0600);
    chmod(layoutPath.c_str(), 0600);
    textCapturePath = path;
    layoutCapturePath = layoutPath;
    layoutCaptureGeneration.fetch_add(1);
    capturedThisSession.clear();
    captureSession.begin(path.substr(path.find_last_of('/') + 1));
    textCaptureEnabled.store(true);
    captureAcceptingRecords.store(true);
    return JNI_TRUE;
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativePauseCaptureWrites(JNIEnv*, jclass, jboolean paused) {
    std::lock_guard<std::mutex> lock(dumpMutex);
    captureWritesPaused = paused;
    if (paused) {
        // A legacy dump-enabled dictionary must not recreate files immediately
        // after cleanup. Explicit capture sessions remain available.
        legacyDumpSuppressed.store(true);
        capturedThisSession.clear();
        dumped.clear();
    }
}
extern "C" JNIEXPORT jstring JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeGetTextCapturePath(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(dumpMutex);
    return env->NewStringUTF(textCapturePath.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeGetTextCaptureStatus(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lock(dumpMutex);
    if (captureSession.textFile.empty()) return env->NewStringUTF("");
    const nlohmann::json status = {{"schema_version", 1},
        {"text_file", captureSession.textFile},
        {"stopped", !captureSession.active}, {"complete", captureSession.complete()},
        {"truncated", captureSession.truncated}, {"write_failed", captureSession.writeFailed},
        {"dropped_records", captureSession.droppedRecords}, {"bytes_written", captureSession.bytes},
        {"text_records", captureSession.textRecords}, {"layout_records", captureSession.layoutRecords}};
    return env->NewStringUTF(status.dump().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeInstall(
    JNIEnv* env, jclass clazz, jstring libraryPath, jstring dictionaryPath, jstring outputPath,
    jboolean runtimeImagesReady) {
    if (installerCalled.exchange(true)) return JNI_TRUE;
    story_bridge::initialize(env, clazz);
    dumpPath = javaString(env, outputPath);
    octoSettingsPath = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/octo-settings.json";
    resourceRoot = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/resource";
    assetRoot = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/asset";
    if (runtimeImagesReady == JNI_TRUE)
        loadRuntimeImages(dumpPath.substr(0, dumpPath.find_last_of('/')) + "/images");
    standaloneFontBundlePath = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/resource-han-rounded.bundle";
    const std::string masterBlobPath = dumpPath.substr(0, dumpPath.find_last_of('/')) +
        "/master-blobs.bin";
    const std::string library = javaString(env, libraryPath);
    if (!loadDictionary(javaString(env, dictionaryPath))) {
        installerCalled = false;
        return JNI_FALSE;
    }
    if (std::ifstream(masterBlobPath, std::ios::binary)) {
        try {
            const size_t patches = masterBlobPatches.load(masterBlobPath);
            const size_t suppressed = translations.suppressMasterSources(masterBlobPatches.suppressedSources());
            LOGI("Loaded %zu MasterDB Blob patches; suppressed %zu global Text sources",
                 patches, suppressed);
        } catch (const std::exception& error) {
            LOGE("MasterDB Blob patches unavailable: %s", error.what());
        }
    }
    // UnityPlayer.loadNative has already loaded libil2cpp.
    // xdl_open takes XDL_* flags; RTLD_NOW aliases XDL_ALWAYS_FORCE_LOAD.
    il2cppHandle = xdl_open(library.c_str(), XDL_DEFAULT);
    if (!il2cppHandle) {
        LOGE("Cannot open %s", library.c_str());
        installerCalled = false;
        return JNI_FALSE;
    }
    domainGet = symbol<DomainGet>("il2cpp_domain_get");
    domainGetAssemblies = symbol<DomainGetAssemblies>("il2cpp_domain_get_assemblies");
    assemblyGetImage = symbol<AssemblyGetImage>("il2cpp_assembly_get_image");
    imageGetName = symbol<ImageGetName>("il2cpp_image_get_name");
    classFromName = symbol<ClassFromName>("il2cpp_class_from_name");
    classGetMethodFromName = symbol<ClassGetMethodFromName>("il2cpp_class_get_method_from_name");
    classGetFieldFromName = symbol<ClassGetFieldFromName>("il2cpp_class_get_field_from_name");
    fieldGetValue = symbol<FieldGetValue>("il2cpp_field_get_value");
    fieldGetValueObject = symbol<FieldGetValueObject>("il2cpp_field_get_value_object");
    fieldSetValue = symbol<FieldSetValue>("il2cpp_field_set_value");
    classGetMethods = symbol<ClassGetMethods>("il2cpp_class_get_methods");
    methodGetName = symbol<MethodGetName>("il2cpp_method_get_name");
    methodGetParamCount = symbol<MethodGetParamCount>("il2cpp_method_get_param_count");
    methodGetParam = symbol<MethodGetParam>("il2cpp_method_get_param");
    methodGetReturnType = symbol<MethodGetReturnType>("il2cpp_method_get_return_type");
    methodGetFlags = symbol<MethodGetFlags>("il2cpp_method_get_flags");
    typeGetName = symbol<TypeGetName>("il2cpp_type_get_name");
    il2cppFree = symbol<Il2CppFree>("il2cpp_free");
    objectGetClass = symbol<ObjectGetClass>("il2cpp_object_get_class");
    classIsAssignableFrom = symbol<ClassIsAssignableFrom>("il2cpp_class_is_assignable_from");
    classGetName = symbol<ClassGetName>("il2cpp_class_get_name");
    classGetNamespace = symbol<ClassGetName>("il2cpp_class_get_namespace");
    classGetType = symbol<ClassGetType>("il2cpp_class_get_type");
    typeGetObject = symbol<TypeGetObject>("il2cpp_type_get_object");
    stringNew = symbol<StringNew>("il2cpp_string_new");
    runtimeInvoke = symbol<RuntimeInvoke>("il2cpp_runtime_invoke");
    gcHandleNew = symbol<GcHandleNew>("il2cpp_gchandle_new");
    gcHandleGetTarget = symbol<GcHandleGetTarget>("il2cpp_gchandle_get_target");
    gcHandleNewWeak = symbol<GcHandleNew>("il2cpp_gchandle_new_weakref");
    gcHandleFree = symbol<GcHandleFree>("il2cpp_gchandle_free");
    objectUnbox = symbol<ObjectUnbox>("il2cpp_object_unbox");
    arrayNew = symbol<ArrayNew>("il2cpp_array_new");
    void* init = xdl_sym(il2cppHandle, "il2cpp_init", nullptr);
    if (!init || !stringNew) {
        LOGE("IL2CPP exports unavailable");
        installerCalled = false;
        return JNI_FALSE;
    }
    // UnityPlayer.loadNative runs before il2cpp_init. Calling il2cpp_domain_get here
    // dereferences uninitialized runtime state on IDOLY PRIDE 6.0.2.
    if (!installMethod("il2cpp_init", init, reinterpret_cast<void*>(hookInit), origInit)) {
        installerCalled = false;
        return JNI_FALSE;
    }
    return JNI_TRUE;
}
