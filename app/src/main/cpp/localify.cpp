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
#include "master_blob_matcher.h"
#include "story_swap.h"
#include "user_template_matcher.h"
#include "numeric_text_template_matcher.h"
#include "venus_achiever_name_matcher.h"
#include "home_activity_name_matcher.h"
#include "shop_exchange_description_matcher.h"
#include "story_bridge.h"
#include "font_profile_config.h"
#include "font_label_geometry.h"
#include "font_outline_geometry.h"
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

std::unordered_map<std::string, std::string> textDict;
std::unordered_map<std::string, std::string> legalTextDict;
// Hooks can nest through TMP setters. Reload parses off-thread and takes this
// lock only for publishing a fully validated replacement.
std::recursive_mutex dictionaryMutex;
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
std::atomic<uint64_t> translationGeneration{1};
std::unordered_map<std::string, std::string> i18nDict;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> i18nBySource;
std::unordered_set<std::string> translatedValues;
UserTemplateMatcher translatedUserTemplates;
NumericTextTemplateMatcher numericTextTemplates;
HomeActivityNameMatcher homeActivityNames;
std::unordered_set<std::string> dumped;
std::mutex dumpMutex;
std::string dumpPath;
std::string resourceRoot;
std::string assetRoot;
bool dumpEnabled = false;
bool resourceReplace = true;
bool assetReplace = true;
bool captureOcto = false;
bool probeMaster = false;
std::string octoSettingsPath;
std::atomic<bool> octoCaptured{false};
std::mutex octoCaptureMutex;
bool fontEnabled = false;
enum class FontMode { Fallback, ReplaceTranslated, ReplaceAll, PreserveNonCjk, ReplaceUi };
FontMode fontMode = FontMode::Fallback;
uint32_t fallbackFontAssetHandle = 0;
uint32_t loadedFontHandle = 0;
uint32_t loadedCjkOnlyFontHandle = 0;
std::string standaloneFontBundlePath;
struct LabelRhrFont { uint32_t rhrHandle; uint32_t originalHandle; };
std::unordered_map<void*, LabelRhrFont> labelRhrAssets;
std::unordered_map<void*, LabelRhrFont> pickerRhrAssets;
struct ForumTitleFont { uint32_t weakHandle, fontHandle; uint64_t signature; size_t length; int diagnosticPasses; std::vector<uint32_t> retiredHandles; };
std::unordered_map<void*, ForumTitleFont> forumTitleFonts;
std::unordered_map<void*, void*> labelOriginalFonts;
struct OriginalLabelStyle { uint32_t weakHandle; uint32_t materialHandle; bool pendingRestoreMaterial = false; };
std::unordered_map<int32_t, OriginalLabelStyle> originalLabelStyles;
struct OriginalLegacyFont { uint32_t weakHandle; uint32_t fontHandle; };
std::unordered_map<void*, OriginalLegacyFont> originalLegacyFonts;
uint32_t pickerDiagnosticWeak = 0;
int pickerDiagnosticPasses = 0;
bool usesIndependentFontRouting() {
    return fontMode == FontMode::PreserveNonCjk || fontMode == FontMode::ReplaceUi;
}
bool replacesCjk() { return fontMode == FontMode::ReplaceAll || usesIndependentFontRouting(); }
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
    bool glyphNumericCount = false;
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

InitFn origInit = nullptr;
TextFn origTmpSetText = nullptr;
EnableFn origTmpOnEnable = nullptr;
EnableFn origTmpPreRender = nullptr;
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
                const mixed_font_router::UnicodeChar* forumBuffer = nullptr, size_t forumCapacity = 0, bool keepMetricVariant = false);
void updateLegacyFont(void* textObject, Il2CppString* rendered);
void routeTmpFontAfterSetter(void* textObject, Il2CppString* rendered);
void diagnosePickerGeometry();
void diagnoseForumTitleGeometry();
void diagnoseHomeTalkGlyph(void* self, const char* stage);
void synchronizeTmpRendererMaterial(void* self);
void restoreStoryNameColor(void* textObject);
void refreshProfileMaterials();
void* findClass(const char* imageName, const char* nameSpace, const char* className);
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
    std::lock_guard<std::recursive_mutex> dictionaryLock(dictionaryMutex);
    if ((!dumpEnabled && !captureAcceptingRecords.load()) || source.empty() || source.size() > 1024 * 1024 ||
        translatedValues.find(source) != translatedValues.end()) return;
    std::lock_guard<std::mutex> lock(dumpMutex);
    const std::string identity = std::string(kind) + "\n" + key + "\n" + source;
    nlohmann::json item = {{"kind", kind}, {"source", source}};
    if (!key.empty()) item["key"] = key;
    if (captureAcceptingRecords.load() && textObject) item["object_path"] = textObjectPath(textObject);
    const auto encoded = item.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    if (dumpEnabled && dumped.insert(identity).second) {
        std::ofstream output(dumpPath, std::ios::app);
        if (output) output << encoded;
    }
    if (captureAcceptingRecords.load() && capturedThisSession.count(identity) == 0) {
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

std::string translateNameTags(const std::string& source) {
    if (source.empty() || source[0] != '#') return {};
    std::string result;
    bool changed = false;
    size_t start = 0;
    while (start < source.size()) {
        const size_t end = source.find('\n', start);
        const size_t limit = end == std::string::npos ? source.size() : end;
        if (source[start] != '#') return {};
        const std::string name = source.substr(start + 1, limit - start - 1);
        const auto found = textDict.find(name);
        if (found != textDict.end() && !found->second.empty() && found->second != name) {
            result += '#';
            result += found->second;
            changed = true;
        } else {
            result.append(source, start, limit - start);
        }
        if (end == std::string::npos) break;
        result += '\n';
        start = end + 1;
    }
    return changed ? result : std::string();
}

std::string translateElapsed(const std::string& source) {
    struct Suffix { const char* original; const char* translated; };
    for (const auto& suffix : {Suffix{"日前", "天前"}, Suffix{"時間前", "小时前"},
                               Suffix{"分前", "分钟前"}}) {
        const std::string original = suffix.original;
        if (source.size() <= original.size() ||
            source.compare(source.size() - original.size(), original.size(), original)) continue;
        const std::string amount = source.substr(0, source.size() - original.size());
        if (std::all_of(amount.begin(), amount.end(),
                        [](unsigned char value) { return value >= '0' && value <= '9'; }))
            return amount + suffix.translated;
    }
    return {};
}

// Playback metadata belongs to one successful resource delivery, not merely
// to a saved alias. All accesses share dictionaryMutex with the UI hooks.
StorySwapPlaybackTitles activeStorySwapTitles;
std::atomic<uint64_t> storySwapPlaybackGeneration{1};
uint64_t canvasStorySwapGeneration = 1;
uint32_t storySwapScreenWeak = 0;
struct StorySwapLabel { uint32_t weak; uint64_t generation; std::string original, rendered; };
std::unordered_map<void*, StorySwapLabel> storySwapLabels;

uint64_t resetStorySwapPlayback() {
    std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
    activeStorySwapTitles = {};
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

Il2CppString* applyStorySwapTitle(void* self, Il2CppString* text) {
    std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
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
    std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
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
    routeTmpFontAfterSetter(self, replacement);
}

Il2CppString* translateText(void* textObject, Il2CppString* original) {
    std::lock_guard<std::recursive_mutex> dictionaryLock(dictionaryMutex);
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
    // paired layout below apply its requested RHR font independently of text.
    if (source == "360°LIVE" || source == "360°" || source == "LIVE") return original;
    // This message-picker prompt is drawn by a compact masked button. Replacing
    // its font asset made the label disappear in the old replace-all build.
    // Keep that compatibility exception there; whole-label routing uses RHR.
    const bool messagePickerPrompt = fontMode == FontMode::ReplaceAll &&
        (source == "选择消息" || source == "メッセージを選択");
    if (replacesCjk()) updateFont(textObject, messagePickerPrompt);
    if (replacesCjk() && textObject && runtimeInvoke && objectUnbox) {
        // Love event stories use a separate portrait-mode name pill. Its Text
        // child is left aligned in a 120 x 40 rect, so short replacement names
        // appear offset inside the rounded background. Main-story labels are
        // different objects and must keep their original styling.
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
        static const auto setSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSize", 1);
        static const auto getAutoSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_enableAutoSizing", 0);
        static const auto setAutoSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_enableAutoSizing", 1);
        static const auto getAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment", 0);
        static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
        static const auto getRectTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
        static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
        static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
        if (getName && getSize && setSize && getAlignment && setAlignment &&
            getRectTransform && getRect && getParent) {
            void* exception = nullptr;
            auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, textObject, nullptr, &exception));
            if (!exception && utf8(name) == "Text") {
                void* sizeBox = runtimeInvoke(getSize, textObject, nullptr, &exception);
                const float size = sizeBox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
                if (!exception && knownFontValue(textObject, "portrait-name-size", size) >= 39.0f && knownFontValue(textObject, "portrait-name-size", size) <= 64.5f) {
                    void* transform = runtimeInvoke(getRectTransform, textObject, nullptr, &exception);
                    void* rectBox = transform ? runtimeInvoke(getRect, transform, nullptr, &exception) : nullptr;
                    void* parent = transform ? runtimeInvoke(getParent, transform, nullptr, &exception) : nullptr;
                    auto* parentName = parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &exception)) : nullptr;
                    const auto* rect = rectBox ? static_cast<float*>(objectUnbox(rectBox)) : nullptr;
                    if (!exception && rect && utf8(parentName) == "Name Label" &&
                        std::fabs(rect[2] - 120.0f) < 1.0f &&
                        std::fabs(rect[3] - 40.0f) < 1.0f) {
                        originalFontValue(textObject, "portrait-name-size", size);
                        void* alignmentBox = runtimeInvoke(getAlignment, textObject, nullptr, &exception);
                        if (!exception && alignmentBox &&
                            *static_cast<int*>(objectUnbox(alignmentBox)) == 4097) {
                            int centered = 4098; // TMP midline center; preserve vertical alignment.
                            void* args[] = {&centered};
                            runtimeInvoke(setAlignment, textObject, args, &exception);
                            if (exception) LOGE("Cannot center portrait story name");
                        }
                        const auto translated = textDict.find(source);
                        const std::string& label = translated != textDict.end() && !translated->second.empty()
                            ? translated->second : source;
                        const size_t characters = std::count_if(label.begin(), label.end(),
                            [](unsigned char byte) { return (byte & 0xc0) != 0x80; });
                        void* autoBox = getAutoSize
                            ? runtimeInvoke(getAutoSize, textObject, nullptr, &exception) : nullptr;
                        const bool wasAuto = autoBox && *static_cast<bool*>(objectUnbox(autoBox));
                        bool desiredAuto = characters > profileValue("portrait_love_event_name", "auto_size_when_characters_gt");
                        if (!exception && autoBox && setAutoSize && wasAuto != desiredAuto) {
                            void* args[] = {&desiredAuto};
                            runtimeInvoke(setAutoSize, textObject, args, &exception);
                            if (exception) LOGE("Cannot set portrait story name auto size");
                        }
                        // Disabling TMP auto sizing lets the existing 40-point
                        // setting fill this pill like the original FOT glyphs.
                        const float desiredSize = profileValue("portrait_love_event_name", "font_size_base_unity") * profileValue("portrait_love_event_name", "font_size_ratio");
                        if (!exception && std::fabs(size - desiredSize) > 0.5f) {
                            float chosenSize = desiredSize;
                            void* args[] = {&chosenSize};
                            runtimeInvoke(setSize, textObject, args, &exception);
                            if (exception) LOGE("Cannot size portrait story name");
                        }
                    }
                }
            }
            if (!exception && utf8(name) == "Message Text") {
                // The full SC rounded font uses slightly taller line metrics
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
        // rounded font appears larger than the game's FOT glyphs at its
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
        if (!replacesCjk() && translatedValues.count(source))
            updateFont(textObject, messagePickerPrompt);
        return original;
    }
    const auto legal = legalTextDict.find(source);
    if (legal != legalTextDict.end() && stringNew) {
        updateFont(textObject, messagePickerPrompt);
        return stringNew(legal->second.c_str());
    }
    if (textObject && VenusAchieverNameMatcher::mayMatch(source)) {
        const auto pattern = i18nDict.find("venus.firstAchiever.name");
        if (pattern != i18nDict.end()) {
            const auto translated = VenusAchieverNameMatcher::translate(
                source, textObjectPath(textObject), pattern->second);
            if (!translated.empty() && stringNew) {
                updateFont(textObject, messagePickerPrompt);
                return stringNew(translated.c_str());
            }
        }
    }
    // Only these captured home-work labels contain the activity-name parameter.
    // Check the text first so ordinary labels never pay for a hierarchy walk.
    if (textObject && homeActivityNames.mayMatch(source)) {
        const auto path = textObjectPath(textObject);
        const auto translated = homeActivityNames.translate(source, path);
        if (!translated.empty() && stringNew) {
            updateFont(textObject, messagePickerPrompt);
            return stringNew(translated.c_str());
        }
        if (homeActivityNames.isRendered(source, path)) {
            updateFont(textObject, messagePickerPrompt);
            return original;
        }
    }
    // Observed exchange-only concatenation. The complete body and stock label
    // must both have reviewed exact translations; unknown tails stay original.
    if (textObject && ShopExchangeDescriptionMatcher::mayMatch(source)) {
        const auto translated = ShopExchangeDescriptionMatcher::translate(source, textObjectPath(textObject), textDict);
        if (!translated.empty() && stringNew) {
            updateFont(textObject, messagePickerPrompt);
            return stringNew(translated.c_str());
        }
    }
    if (textObject && ShopScopedQuantityNameMatcher::mayMatch(source)) {
        const auto translated = ShopScopedQuantityNameMatcher::translate(source, textObjectPath(textObject), textDict);
        if (!translated.empty() && stringNew) {
            updateFont(textObject, messagePickerPrompt);
            return stringNew(translated.c_str());
        }
    }
    auto found = textDict.find(source);
    if (found != textDict.end() && !found->second.empty() && stringNew) {
        updateFont(textObject, messagePickerPrompt);
        return stringNew(found->second.c_str());
    }
    const std::string numeric = numericTextTemplates.translate(source);
    if (!numeric.empty() && stringNew) {
        updateFont(textObject, messagePickerPrompt);
        return stringNew(numeric.c_str());
    }
    const std::string elapsed = translateElapsed(source);
    if (!elapsed.empty() && stringNew) {
        updateFont(textObject, messagePickerPrompt);
        return stringNew(elapsed.c_str());
    }
    const std::string nameTags = translateNameTags(source);
    if (!nameTags.empty() && stringNew) {
        updateFont(textObject, messagePickerPrompt);
        return stringNew(nameTags.c_str());
    }
    const bool exactTranslated = translatedValues.find(source) != translatedValues.end() ||
        numericTextTemplates.isRendered(source);
    const bool playerTemplate = !exactTranslated && translatedUserTemplates.matches(source);
    if (exactTranslated || playerTemplate) {
        if (playerTemplate && fontEnabled && textObject) {
            static std::atomic<int> reported{0};
            if (reported.fetch_add(1) < 4)
                LOGI("Matched rendered player-name text for font replacement");
        }
        updateFont(textObject, messagePickerPrompt);
        return original;
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
    std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
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
        keyed ? resolved->second.key : std::string(), keyed ? 0 : translationGeneration.load()});
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
    const uint64_t generation = translationGeneration.load(std::memory_order_acquire);
    if (generation == 1) return;
    std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
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
        const auto variants = i18nBySource.find(it->second.i18nKey);
        if (variants != i18nBySource.end()) {
            const auto selected = variants->second.find(it->second.source);
            if (selected != variants->second.end() && !selected->second.empty())
                desired = selected->second;
        } else {
            const auto selected = i18nDict.find(it->second.i18nKey);
            if (selected != i18nDict.end() && !selected->second.empty())
                desired = selected->second;
        }
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
        routeTmpFontAfterSetter(self, replacement);
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
    routeTmpFontAfterSetter(self, translated);
    restoreStoryNameColor(self);
    relayoutBondTalkName(self);
}

void hookTmpSetTextOne(void* self, Il2CppString* value, void* method) {
    auto* translated = applyStorySwapTitle(self, translateText(self, value));
    rememberTranslatedText(self, value, translated);
    origTmpSetTextOne(self, translated, method);
    routeTmpFontAfterSetter(self, translated);
    restoreStoryNameColor(self);
    relayoutBondTalkName(self);
}

void hookTmpSetTextTwo(void* self, Il2CppString* value, bool syncInput, void* method) {
    auto* translated = applyStorySwapTitle(self, translateText(self, value));
    rememberTranslatedText(self, value, translated);
    origTmpSetTextTwo(self, translated, syncInput, method);
    routeTmpFontAfterSetter(self, translated);
    restoreStoryNameColor(self);
    relayoutBondTalkName(self);
}

// OnPreRenderCanvas is called only for dirty UI elements. Schedule a bounded
// batch before Unity dispatches willRenderCanvases, so static labels are added
// to this frame's graphic rebuild queue after a data/profile/capture change.
void hookCanvasWillRender(void* method) {
    diagnosePickerGeometry();
    diagnoseForumTitleGeometry();
    const uint64_t textGeneration = translationGeneration.load(std::memory_order_acquire);
    const uint64_t fontGeneration = fontProfileGeneration.load(std::memory_order_acquire);
    const uint64_t captureGeneration = layoutCaptureGeneration.load(std::memory_order_acquire);
    const uint64_t storyGeneration = storySwapPlaybackGeneration.load(std::memory_order_acquire);
    const bool storyChanged = storyGeneration != canvasStorySwapGeneration;
    const bool textChanged = textGeneration != canvasTextGeneration;
    const bool fontChanged = fontGeneration != canvasFontGeneration;
    const bool captureChanged = captureGeneration != canvasCaptureGeneration &&
        textCaptureEnabled.load(std::memory_order_acquire);
    if (storyChanged || textChanged || fontChanged || captureGeneration != canvasCaptureGeneration) {
        std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
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
    if (!self || !fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
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
    static int stateReports = 0;
    if (stateReports < 12 && classGetFieldFromName && fieldGetValue) {
        static void* klass = findClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Text");
        static void* field = klass ? classGetFieldFromName(klass, "m_TextProcessingArray") : nullptr;
        Il2CppByteArray* array = nullptr;
        if (field) fieldGetValue(self, field, &array);
        std::string codepoints;
        if (array && array->length <= 65536) {
            const auto* chars = reinterpret_cast<const mixed_font_router::UnicodeChar*>(array->data);
            for (size_t i = 0; i < std::min<size_t>(array->length, 12) && chars[i].unicode; ++i)
                codepoints += std::to_string(chars[i].unicode) + ",";
        }
        ++stateReports;
        LOGI("Love episode pre-render: text=%s buffer=%s", utf8(current).c_str(), codepoints.c_str());
    }
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
    if (!self || !fontEnabled || fontMode != FontMode::ReplaceUi || !runtimeInvoke) return;
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
        if (getterKnown) getterNumeric = ui_font_policy::isNumericCount(static_cast<size_t>(text->length),
            [&](size_t i) { return text->chars[i]; });
    }
    const auto route = ui_font_policy::preRenderRoute(path, glyphKnown,
        glyphKnown && baseline->glyphNumericCount, getterKnown, getterNumeric);
    if (route == ui_font_policy::PreRenderRoute::Defer) return;
    const bool useRhr = route == ui_font_policy::PreRenderRoute::Replacement;
    // Preserve an existing picker/forum metric variant when only refreshing
    // its material or rebinding after a prefab reset. Glyph routing remains
    // authoritative for selecting a new variant after the text changes.
    updateFont(self, false, &useRhr, false, nullptr, 0, true);
}

void hookTmpPreRender(void* self, void* method) {
    std::lock_guard<std::recursive_mutex> frameLock(dictionaryMutex);
    registerTmpUi(self);
    refreshTranslatedText(self);
    refreshStorySwapTitle(self);
    refreshLoveEpisodeLabel(self);
    // Resolve after set_text and again after prefab font resets, before mesh creation.
    refreshDefaultUiFont(self);
    fitPanoramaLabels(self);
    alignGallerySearchFooter(self);
    if (self && fontProfileGeneration.load() > 1 && runtimeInvoke) {
        std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
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
    if (self && fontEnabled && replacesCjk() && runtimeInvoke && objectUnbox) {
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
            if (!exception && homeRankOrSupportLayoutCandidate(utf8(label), fontMode == FontMode::ReplaceAll)) {
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
    captureTextLayout(self);
    origTmpPreRender(self, method);
    synchronizeTmpRendererMaterial(self);
    diagnoseHomeTalkGlyph(self, "post_prerender");
}

void hookLegacySetText(void* self, Il2CppString* value, void* method) {
    auto* translated = translateText(nullptr, value);
    updateLegacyFont(self, translated);
    origLegacySetText(self, translated, method);
}

void hookI18nSetValue(void* self, Il2CppString* key, Il2CppString* value, void* method) {
    std::lock_guard<std::recursive_mutex> dictionaryLock(dictionaryMutex);
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
    const auto variants = i18nBySource.find(keyText);
    if (variants != i18nBySource.end()) {
        const auto selected = variants->second.find(source);
        if (selected != variants->second.end() && !selected->second.empty() && stringNew) {
            applyResolved(selected->second);
            return;
        }
    }
    auto found = i18nDict.find(keyText);
    if (variants == i18nBySource.end() && found != i18nDict.end() &&
        !found->second.empty() && stringNew) {
        applyResolved(found->second);
    } else {
        dump("i18n", source, keyText);
        origI18nSetValue(self, key, value, method);
    }
}

void captureOctoSettings(void* settings) {
    if (!captureOcto || !settings || !runtimeInvoke || !objectGetClass ||
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
    if (resourceReplace && name.size() >= 4 &&
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
                        std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
                        // A nested/newer adventure request owns its own playback.
                        if (storySwapPlaybackGeneration == playbackGeneration) {
                            activeStorySwapTitles = std::move(playbackTitles);
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
    if (!probeMaster || !classGetMethods || !methodGetName || !methodGetParamCount ||
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
    std::lock_guard<std::recursive_mutex> lock(dictionaryMutex);
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
    if (!assetReplace || name.empty() || name.find('/') != std::string::npos ||
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
    if (dumpEnabled && (name.find("banner") != std::string::npos ||
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

enum class StoryStyle { Subtitle, SpeakerName };

std::unordered_map<void*, uint32_t> storyMaterials[2];

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
            if (auto* material = gcHandleGetTarget(item.second))
                applyStoryMaterial(material, static_cast<StoryStyle>(style));
    for (const auto& item : rouletteOutlineMaterials)
        if (auto* material = gcHandleGetTarget(item.second.copyHandle)) applyRouletteOutline(material,item.second);
    applied = generation;
}
void* storyMaterial(void* shared, StoryStyle style) {
    static const auto instantiate = instantiateUnityObject();
    auto& cache = storyMaterials[static_cast<int>(style)];
    const auto found = cache.find(shared);
    if (found != cache.end() && gcHandleGetTarget) {
        void* cached = gcHandleGetTarget(found->second);
        if (cached) { applyStoryMaterial(cached, style); return cached; }
    }
    if (!instantiate || !gcHandleNew || !runtimeInvoke) return nullptr;
    void* args[] = {shared};
    void* exception = nullptr;
    void* copy = runtimeInvoke(instantiate, nullptr, args, &exception);
    if (exception || !copy || !applyStoryMaterial(copy, style)) return nullptr;
    cache[shared] = gcHandleNew(copy, false);
    LOGI("Created isolated story material style=%d", static_cast<int>(style));
    return copy;
}

void restoreStoryNameColor(void* textObject) {
    if (!fontEnabled || !replacesCjk() || !textObject ||
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

void* loadedCjkFont(bool independentRhr = false) {
    std::lock_guard<std::recursive_mutex> lock(fontMutex);
    auto& handle = independentRhr ? loadedCjkOnlyFontHandle : loadedFontHandle;
    if (handle && gcHandleGetTarget) {
        void* cached = gcHandleGetTarget(handle);
        if (cached) return cached;
    }
    // A standalone module may never have an RHR font in the game's resources.
    // Keep retrying for fonts loaded by later scenes, without scanning on every
    // text callback. Each source has its own schedule so modes cannot starve it.
    using FontScanClock = std::chrono::steady_clock;
    struct FontScanState {
        FontScanClock::time_point nextScan{};
        FontScanClock::time_point nextDiagnostic{};
    };
    static FontScanState scans[2];
    auto& scan = scans[independentRhr ? 1 : 0];
    const auto now = FontScanClock::now();
    if (now < scan.nextScan) return nullptr;
    scan.nextScan = now + std::chrono::seconds(2);
    const bool report = now >= scan.nextDiagnostic;
    if (report) scan.nextDiagnostic = now + std::chrono::seconds(60);
    // Prefer the module-owned font so updating the plugin also updates fonts
    // on older LSPatch packages that still contain an embedded RHR font.
    if (independentRhr) {
        if (void* standalone = loadStandaloneRhrFont(report)) return standalone;
    }
    const char* wanted = independentRhr ? "Localify-ResourceHanRoundedSC-ExtraBold" : "FOT-UDKakugo_LargePro-DB";
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
            if (independentRhr && !setCachedFontRetention(font, true)) {
                if (report) LOGE("Cannot retain independent RHR source font");
                return nullptr;
            }
            if (gcHandleNew) handle = gcHandleNew(font, false);
            if (handle || report) LOGI("Found Unity source font: %s", wanted);
            return font;
        }
    }
    if (report) LOGI("Unity source font unavailable: %s; retrying while text is active", wanted);
    return nullptr;
}

void updateLegacyFont(void* textObject, Il2CppString* rendered) {
    if (!fontEnabled || !usesIndependentFontRouting() || !textObject || !rendered ||
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
    const bool useRhr = fontMode == FontMode::ReplaceUi
        ? ui_font_policy::usesReplacement(uiFontPolicyPath(textObject),
            rendered->length >= 0 && ui_font_policy::isNumericCount(static_cast<size_t>(rendered->length),
                [&](size_t i) { return rendered->chars[i]; }))
        : (rendered->length > 0 &&
           mixed_font_router::hasCjk(rendered->chars, static_cast<size_t>(rendered->length)));
    // Classification uses the translated setter argument, never the previous text.
    if (!useRhr && saved == originalLegacyFonts.end()) return;
    void* exception = nullptr;
    void* current = runtimeInvoke(getFont, textObject, nullptr, &exception);
    if (exception || !current) return;
    void* replacement = useRhr ? loadedCjkFont(true) : gcHandleGetTarget(saved->second.fontHandle);
    if (!replacement) return;
    if (useRhr && current != replacement) {
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
    if (!useRhr) {
        release(saved->second);
        originalLegacyFonts.erase(saved);
    }
    // Fixed public UI phrase only; never log arbitrary text or player values.
    if (utf8(rendered).find("开始") != std::string::npos) {
        static int reported = 0;
        if (reported++ < 12) {
            static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
            void* error = nullptr;
            auto* oldName = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, current, nullptr, &error)) : nullptr;
            const std::string sourceName = error ? "?" : utf8(oldName);
            error = nullptr;
            auto* newName = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, replacement, nullptr, &error)) : nullptr;
            LOGI("Legacy start label font route: source=%s target=%s replacement=%d changed=%d",
                 sourceName.c_str(), error ? "?" : utf8(newName).c_str(), useRhr, current != replacement);
        }
    }
}

void* createPickerRhrClone(void* normalRhr, const std::string& glyphText = "选择消息") {
    if (!normalRhr || !runtimeInvoke || !classGetMethods || !methodGetName ||
        !methodGetParamCount || !methodGetParam || !typeGetName || !objectGetClass || !stringNew || !objectUnbox)
        return nullptr;
    const MethodInfoHead* addCharacters = nullptr;
    const MethodInfoHead* hasCharacters = nullptr;
    void* iterator = nullptr;
    while (const auto* candidate = classGetMethods(objectGetClass(normalRhr), &iterator)) {
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
        void* result = runtimeInvoke(hasCharacters, normalRhr, args, &error);
        return {result && !error && *static_cast<bool*>(objectUnbox(result)), error || !result};
    };
    const bool covered = ensureFontCoverage(checkCoverage, [&]() {
        attempted = true;
        void* args[] = {phrase, &missing, &includeFeatures};
        error = nullptr;
        void* result = runtimeInvoke(addCharacters, normalRhr, args, &error);
        addResult = result && !error && *static_cast<bool*>(objectUnbox(result));
        return !error && result != nullptr;
    });
    if (!covered) {
        LOGE("Cannot prewarm constrained RHR glyphs: exception=%d attempted=%d result=%d missing_length=%d",
             error != nullptr, attempted, addResult, missing ? missing->length : -1);
        return nullptr;
    }
    if (!attempted || !addResult) {
        static int recovered = 0;
        if (recovered++ < 8) LOGI("Constrained RHR coverage verified despite no new glyph addition: attempted=%d", attempted);
    }
    static const auto instantiate = instantiateUnityObject();
    static const auto setPopulation = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "set_atlasPopulationMode", 1);
    if (!instantiate || !setPopulation) return nullptr;
    void* cloneArgs[] = {normalRhr};
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
    // retains the normal RHR character/glyph tables, atlas and material.
    LOGI("Created static constrained clone sharing prewarmed normal RHR atlas");
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
    // Only this independently created picker asset receives original vertical
    // line metrics. RHR glyphs, atlas, point size, scale and widths stay intact.
    void* args[] = {objectUnbox(target)};
    runtimeInvoke(setFace, replacement, args, &error);
    if (error) return false;
    LOGI("Normalized picker RHR line metrics: original_point=%d target_point=%d ratio=%.4f", sourcePoint, targetPoint, ratio);
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
void reportForumRoute(void* self, const char* stage, void* normal, void* original,
                      void* selected, float size, int overflow, const float* rect, bool pathMatch, size_t length) {
    static int reports = 0;
    if (reports++ >= 96) return;
    const auto n = verticalMetrics(normal), o = verticalMetrics(original), c = verticalMetrics(selected);
    void* current = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font");
    void* shared = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSharedMaterial");
    static void* assetClass = findClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Asset");
    static void* materialField = assetClass && classGetFieldFromName ? classGetFieldFromName(assetClass, "material") : nullptr;
    void* fontMaterial = nullptr;
    if (selected && materialField && fieldGetValue) fieldGetValue(selected, materialField, &fontMaterial);
    LOGI("Forum route: stage=%s object=%p current=%p selected=%p original=%p normal=%p path_match=%d length=%zu size=%.2f overflow=%d rect_w=%.2f rect_h=%.2f normal_point=%.1f normal_scale=%.3f normal_ascent=%.3f normal_descent=%.3f original_point=%.1f original_scale=%.3f original_ascent=%.3f original_descent=%.3f selected_point=%.1f selected_scale=%.3f selected_ascent=%.3f selected_descent=%.3f selected_height=%.3f shared_atlas=%p selected_atlas=%p",
         stage, self, current, selected, original, normal, pathMatch, length, size, overflow, rect ? rect[2] : -1, rect ? rect[3] : -1,
         n.pointSize,n.scale,n.ascent,n.descent,o.pointSize,o.scale,o.ascent,o.descent,c.pointSize,c.scale,c.ascent,c.descent,fontLineHeight(size,c),
         shared ? materialTexture(shared,"_MainTex") : nullptr, fontMaterial ? materialTexture(fontMaterial,"_MainTex") : nullptr);
}
void synchronizeTmpRendererMaterial(void* self) {
    if (!usesIndependentFontRouting() || !fontEnabled || !isTmpUi(self)) return;
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
    static void* textClass = findClass("Unity.TextMeshPro.dll","TMPro","TMP_Text");
    static void* dirtyField = textClass && classGetFieldFromName ? classGetFieldFromName(textClass,"m_isMaterialDirty") : nullptr;
    bool dirty=false;
    if (dirtyField && fieldGetValue) fieldGetValue(self,dirtyField,&dirty);
    void* error=nullptr;
    runtimeInvoke(updateMaterial,self,nullptr,&error);
    void* after=actualTexture();
    if (!baseline) baseline=fontBaseline(self);
    if (baseline) baseline->pendingRendererSubmit=pendingTmpRendererAfterSubmit(!error,after==expected);
    static const auto getId=methodInfo("UnityEngine.CoreModule.dll","UnityEngine","Object","GetInstanceID",0);
    const auto id=[&](void* object)->int32_t {
        if (!object || !getId || !objectUnbox) return 0;
        void* exception=nullptr; void* box=runtimeInvoke(getId,object,nullptr,&exception);
        return !exception && box ? *static_cast<int32_t*>(objectUnbox(box)) : 0;
    };
    static int reports=0;
    if (reports++ < 32) LOGI("TMP main renderer synchronization: object_id=%d font_id=%d pending=%d material_dirty=%d before_atlas=%d expected_atlas=%d after_atlas=%d exception=%d matched=%d",
         id(self),id(font),pending,dirty,id(before),id(expected),id(after),error!=nullptr,after==expected);
}
void diagnoseHomeTalkGlyph(void* self, const char* stage) {
    static int reports = 0;
    if (reports >= 24 || !self || !fieldGetValueObject || !fieldGetValue || !classGetFieldFromName || !objectGetClass || !objectUnbox) return;
    // Exact public dialogue control; never inspect account or player text.
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,"UnityEngine.CoreModule.dll","UnityEngine","Object","get_name"));
    if (utf8(name) != "Serif") return;
    const std::string path = textObjectPath(self);
    static const std::string suffix = "/Bottom/SerifWIndow/Serif";
    if (path.size() < suffix.size() || path.compare(path.size()-suffix.size(), suffix.size(), suffix)) return;
    const auto field = [&](void* object, const char* name) -> void* {
        void* f = object ? classGetFieldFromName(objectGetClass(object), name) : nullptr;
        return f ? fieldGetValueObject(f, object) : nullptr;
    };
    const auto integer = [&](void* object, const char* name) -> int32_t {
        void* box = field(object,name); return box ? *static_cast<int32_t*>(objectUnbox(box)) : -1;
    };
    const auto real = [&](void* object, const char* name) -> float {
        void* box = field(object,name); return box ? *static_cast<float*>(objectUnbox(box)) : -1;
    };
    static const MethodInfoHead* getValue = []() -> const MethodInfoHead* {
        void* klass = findClass("mscorlib.dll", "System", "Array");
        if (!klass || !classGetMethods || !methodGetName || !methodGetParamCount || !methodGetParam || !typeGetName) return nullptr;
        void* iterator = nullptr;
        while (const auto* m = classGetMethods(klass, &iterator)) {
            if (std::strcmp(methodGetName(m),"GetValue") || methodGetParamCount(m)!=1) continue;
            char* type = typeGetName(methodGetParam(m,0));
            bool match = type && !std::strcmp(type,"System.Int32");
            if (type && il2cppFree) il2cppFree(type);
            if (match) return m;
        }
        return nullptr;
    }();
    if (!getValue) return;
    const auto at = [&](void* array, int32_t index) -> void* {
        if (!array) return nullptr;
        void* error=nullptr; void* args[]={&index};
        void* box=runtimeInvoke(getValue,array,args,&error); return error ? nullptr : box;
    };
    void* info = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_textInfo");
    const int32_t count = integer(info,"characterCount");
    if (count <= 0 || count > 128) return;
    void* characters = field(info,"characterInfo");
    int32_t target=-1;
    for (int32_t i=0;i<count;++i) {
        void* character=at(characters,i); void* box=field(character,"character");
        if (box && *static_cast<uint16_t*>(objectUnbox(box)) == 0x540e) { target=i; break; }
    }
    if (target<0) return;
    ++reports;
    void* primary = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_font");
    for (int32_t i=std::max(0,target-1);i<=std::min(count-1,target+1);++i) {
        void* character=at(characters,i); void* box=field(character,"character");
        const uint32_t cp=box ? *static_cast<uint16_t*>(objectUnbox(box)) : 0;
        void* font=field(character,"fontAsset"); void* element=field(character,"textElement");
        void* glyph=readFontProperty(element,"Unity.TextMeshPro.dll","TMPro","TMP_TextElement","get_glyph");
        void* owner=readFontProperty(element,"Unity.TextMeshPro.dll","TMPro","TMP_TextElement","get_textAsset");
        const auto glyphProperty = [&](const char* name) -> void* {
            const auto method = glyph && classGetMethodFromName ? classGetMethodFromName(objectGetClass(glyph),name,0) : nullptr;
            void* error=nullptr;
            void* value=method ? runtimeInvoke(method,glyph,nullptr,&error) : nullptr;
            return error ? nullptr : value;
        };
        void* atlasBox=glyphProperty("get_atlasIndex");
        const int32_t atlas=atlasBox ? *static_cast<int32_t*>(objectUnbox(atlasBox)) : -1;
        void* glyphBox=glyphProperty("get_index");
        const uint32_t glyphIndex=glyphBox ? *static_cast<uint32_t*>(objectUnbox(glyphBox)) : 0;
        void* rect=glyphProperty("get_glyphRect");
        void* textures=readFontProperty(font,"Unity.TextMeshPro.dll","TMPro","TMP_FontAsset","get_atlasTextures");
        void* texture=atlas>=0 ? at(textures,atlas) : nullptr;
        void* material=field(character,"material"); void* actual=material ? materialTexture(material,"_MainTex") : nullptr;
        const auto dimension=[&](void* t,const char* property) -> int32_t {
            void* value=t ? readFontProperty(t,"UnityEngine.CoreModule.dll","UnityEngine","Texture",property) : nullptr;
            return value ? *static_cast<int32_t*>(objectUnbox(value)) : -1;
        };
        void* bl=field(character,"vertex_BL"); void* tr=field(character,"vertex_TR");
        void* uvBL=field(bl,"uv"); void* uvTR=field(tr,"uv");
        LOGI("HomeTalk glyph diagnostic: stage=%s object=%p index=%d cp=%u primary=%p char_font=%p element_owner=%p glyph=%u atlas_index=%d rect_x=%d rect_y=%d rect_w=%d rect_h=%d material_index=%d material=%p expected_texture=%p expected_w=%d expected_h=%d actual_texture=%p actual_w=%d actual_h=%d uv_bl_x=%.6f uv_bl_y=%.6f uv_tr_x=%.6f uv_tr_y=%.6f uv_present=%d",
             stage,self,i,cp,primary,font,owner,glyphIndex,atlas,integer(rect,"m_X"),integer(rect,"m_Y"),integer(rect,"m_Width"),integer(rect,"m_Height"),integer(character,"materialReferenceIndex"),material,texture,dimension(texture,"get_width"),dimension(texture,"get_height"),actual,dimension(actual,"get_width"),dimension(actual,"get_height"),real(uvBL,"x"),real(uvBL,"y"),real(uvTR,"x"),real(uvTR,"y"),uvBL && uvTR);
    }
}
void releaseForumTitleFont(const ForumTitleFont& saved) {
    if (!gcHandleGetTarget || !gcHandleFree) return;
    if (void* font = gcHandleGetTarget(saved.fontHandle)) labelOriginalFonts.erase(font);
    gcHandleFree(saved.weakHandle); gcHandleFree(saved.fontHandle);
    for (const auto handle : saved.retiredHandles) {
        if (void* font = gcHandleGetTarget(handle)) labelOriginalFonts.erase(font);
        gcHandleFree(handle);
    }
}
void commitForumTitleFont(void* self, void* assigned) {
    auto saved = forumTitleFonts.find(self);
    if (saved == forumTitleFonts.end()) return;
    if (gcHandleGetTarget(saved->second.fontHandle) != assigned) {
        releaseForumTitleFont(saved->second); forumTitleFonts.erase(saved); return;
    }
    for (const auto handle : saved->second.retiredHandles) {
        if (void* font = gcHandleGetTarget(handle)) labelOriginalFonts.erase(font);
        gcHandleFree(handle);
    }
    saved->second.retiredHandles.clear();
}
void diagnoseForumTitleGeometry() {
    if (!gcHandleGetTarget || !objectUnbox) return;
    for (auto it = forumTitleFonts.begin(); it != forumTitleFonts.end();) {
        void* self = gcHandleGetTarget(it->second.weakHandle);
        if (!self || self != it->first) {
            releaseForumTitleFont(it->second); it = forumTitleFonts.erase(it); continue;
        }
        if (it->second.diagnosticPasses > 0) {
            --it->second.diagnosticPasses;
            void* bounds = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_textBounds");
            if (bounds) {
                auto* v = static_cast<float*>(objectUnbox(bounds));
                LOGI("Forum title geometry after: object=%p length=%zu center_x=%.2f center_y=%.2f extent_x=%.2f extent_y=%.2f",
                     self, it->second.length, v[0], v[1], v[3], v[4]);
                void* font = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font");
                reportForumRoute(self, "post_geometry", nullptr, nullptr, font, 48, -1, nullptr, true, it->second.length);
            }
        }
        ++it;
    }
}
void* forumTitleFont(void* self, void* original, void* normal,
                    const mixed_font_router::UnicodeChar* chars, size_t capacity) {
    if (!objectUnbox || !gcHandleGetTarget || !gcHandleNewWeak || !gcHandleNew || !gcHandleFree) return normal;
    auto existing = forumTitleFonts.find(self);
    if (existing != forumTitleFonts.end() && gcHandleGetTarget(existing->second.weakHandle) != self) {
        releaseForumTitleFont(existing->second); forumTitleFonts.erase(existing); existing = forumTitleFonts.end();
    }
    void* sizeBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize");
    void* overflowBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_overflowMode");
    void* transform = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform");
    void* rectBox = transform ? readFontProperty(transform, "UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect") : nullptr;
    if (!sizeBox || !overflowBox || !rectBox) return normal;
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    const int32_t overflow = *static_cast<int32_t*>(objectUnbox(overflowBox));
    auto* rect = static_cast<float*>(objectUnbox(rectBox));
    const auto rhrMetrics = verticalMetrics(normal), originalMetrics = verticalMetrics(original);
    const std::string path = textObjectPath(self);
    const bool pathMatch = path.find("/ForumThreadListCell(Clone)/Button/Title") != std::string::npos;
    if (!forumTitleMetricsGuard(path, rect[2], rect[3], size, overflow, rhrMetrics, originalMetrics)) {
        if (pathMatch || existing != forumTitleFonts.end())
            reportForumRoute(self, "guard_false", normal, original, normal, size, overflow, rect, pathMatch, 0);
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
    if (existing != forumTitleFonts.end() && existing->second.signature == signature && existing->second.length == length) {
        if (void* cached = gcHandleGetTarget(existing->second.fontHandle)) {
            const auto mapped = labelOriginalFonts.find(cached);
            if (mapped != labelOriginalFonts.end() && mapped->second == original) {
                reportForumRoute(self, "cache_hit", normal, original, cached, size, overflow, rect, pathMatch, length);
                static int resamples = 0;
                if (resamples++ < 16) existing->second.diagnosticPasses = 3;
                return cached;
            }
        }
    }
    // Static clones also need the glyph TMP inserts when a long title is elided.
    glyphs.push_back(u'\u2026');
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    void* clone = createPickerRhrClone(normal, converter.to_bytes(glyphs));
    if (!clone || !normalizePickerFaceInfo(clone, original)) {
        reportForumRoute(self, "clone_failed", normal, original, normal, size, overflow, rect, pathMatch, length);
        return normal;
    }
    reportForumRoute(self, "new_clone", normal, original, clone, size, overflow, rect, pathMatch, length);
    const uint32_t weak = gcHandleNewWeak(self, false);
    if (!weak) return normal;
    const uint32_t handle = gcHandleNew(clone, false);
    if (!handle) { gcHandleFree(weak); return normal; }
    std::vector<uint32_t> retired;
    if (existing != forumTitleFonts.end()) {
        void* currentFont = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font");
        retired = std::move(existing->second.retiredHandles);
        retired.push_back(existing->second.fontHandle);
        gcHandleFree(existing->second.weakHandle);
        forumTitleFonts.erase(existing);
        // Retain the font still assigned to this label until both font and
        // material setters commit. Failed earlier candidates need no retention.
        for (auto it = retired.begin(); it != retired.end();) {
            void* old = gcHandleGetTarget(*it);
            if (old != currentFont) {
                labelOriginalFonts.erase(old); gcHandleFree(*it); it = retired.erase(it);
            } else ++it;
        }
    }
    static int reports = 0;
    const int diagnostics = reports++ < 16 ? 3 : 0;
    forumTitleFonts.emplace(self, ForumTitleFont{weak, handle, signature, length, diagnostics, std::move(retired)});
    labelOriginalFonts[clone] = original;
    if (diagnostics) {
        void* bounds = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_textBounds");
        auto* v = bounds ? static_cast<float*>(objectUnbox(bounds)) : nullptr;
        LOGI("Forum title metric guard before: length=%zu overflow=%d size=%.1f rect_height=%.1f rhr_height=%.2f original_height=%.2f bounds_x=%.2f bounds_y=%.2f",
             length, overflow, size, rect[3], fontLineHeight(size, rhrMetrics), fontLineHeight(size, originalMetrics), v ? v[3] : 0, v ? v[4] : 0);
    }
    return clone;
}

void updateFont(void* textObject, bool preserveOriginalFont, const bool* replacementOverride, bool pickerMetrics,
                const mixed_font_router::UnicodeChar* forumBuffer, size_t forumCapacity, bool keepMetricVariant) {
    // Formatted Solis labels can keep get_text empty while their Unicode buffer
    // contains CJK. Only the glyph-lookup buffer is authoritative in this mode;
    // getter-based setter/pre-render calls must not undo its font/material.
    if (usesIndependentFontRouting() && !replacementOverride) return;
    if (!fontEnabled || !textObject || !runtimeInvoke) return;
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
    const FontMode effectiveMode = preserveLegacyFont ? FontMode::Fallback : fontMode;
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
    if (usesIndependentFontRouting()) {
        // Route complete labels through an independent RHR asset. The caller
        // selects the current policy; snapshots retain original font/material
        // for numeric counts and design exceptions.
        const auto original = labelOriginalFonts.find(asset);
        const bool usingRhr = original != labelOriginalFonts.end();
        void* originalAsset = usingRhr ? original->second : asset;
        static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
        auto* text = !replacementOverride && getText
            ? static_cast<Il2CppString*>(runtimeInvoke(getText, textObject, nullptr, &exception)) : nullptr;
        const bool useRhr = replacementOverride ? *replacementOverride :
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
        if (!useRhr && !usingRhr) {
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
                commitForumTitleFont(textObject, originalAsset);
                releaseSnapshot(saved->second);
                originalLabelStyles.erase(saved);
            }
            return;
        }
        void* sourceMaterial = getSharedMaterial
            ? runtimeInvoke(getSharedMaterial, textObject, nullptr, &exception) : nullptr;
        if (exception || !sourceMaterial || !setSharedMaterial || !gcHandleNew) return;
        if (!useRhr) {
            if (usingRhr) {
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
                commitForumTitleFont(textObject, originalAsset);
                releaseSnapshot(saved->second);
                originalLabelStyles.erase(saved);
            }
            if (!useRhr) return;
            asset = originalAsset;
        } else {
            void* rhr = keepMetricVariant && usingRhr ? asset : nullptr;
            auto& targets = pickerMetrics ? pickerRhrAssets : labelRhrAssets;
            const auto cached = targets.find(originalAsset);
            if (!rhr && cached != targets.end() && gcHandleGetTarget)
                rhr = gcHandleGetTarget(cached->second.rhrHandle);
            if (!rhr) {
                void* normal = nullptr;
                if (pickerMetrics) {
                    const auto normalCached = labelRhrAssets.find(originalAsset);
                    if (normalCached != labelRhrAssets.end() && gcHandleGetTarget)
                        normal = gcHandleGetTarget(normalCached->second.rhrHandle);
                }
                if (!normal) {
                    void* font = loadedCjkFont(true);
                    if (!font) return;
                    void* createArgs[] = {font};
                    normal = runtimeInvoke(createFallback, nullptr, createArgs, &exception);
                    if (exception || !normal) return;
                    if (!setCachedFontRetention(normal, true)) {
                        LOGE("Cannot retain cached RHR font resources");
                        return;
                    }
                    if (pickerMetrics) {
                        const uint32_t normalHandle = gcHandleNew(normal, false);
                        if (!normalHandle) return;
                        const uint32_t originalHandle = gcHandleNew(originalAsset, false);
                        if (!originalHandle) { gcHandleFree(normalHandle); return; }
                        labelRhrAssets[originalAsset] = {normalHandle, originalHandle};
                        labelOriginalFonts[normal] = originalAsset;
                    }
                }
                rhr = pickerMetrics ? createPickerRhrClone(normal) : normal;
                if (!rhr) return;
                if (pickerMetrics && !normalizePickerFaceInfo(rhr, originalAsset)) {
                    LOGE("Cannot normalize independent picker font metrics");
                    return;
                }
                // Unlike forum-title clones, this picker variant also belongs
                // to the process-lifetime cache and needs its own native root.
                if (pickerMetrics && !setCachedFontRetention(rhr, true)) return;
                const uint32_t rhrHandle = gcHandleNew(rhr, false);
                if (!rhrHandle) return;
                const uint32_t originalHandle = gcHandleNew(originalAsset, false);
                if (!originalHandle) { gcHandleFree(rhrHandle); return; }
                // Pair lifetime matches the small per-original-font target cache.
                // No original glyph fallback is attached to the full RHR asset.
                targets[originalAsset] = {rhrHandle, originalHandle};
                labelOriginalFonts[rhr] = originalAsset;
                LOGI("Created full RHR label font: original=%p replacement=%p", originalAsset, rhr);
            }
            if (forumBuffer) rhr = forumTitleFont(textObject, originalAsset, rhr, forumBuffer, forumCapacity);
            if (!usingRhr) {
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
            labelReplacement = rhr;
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
            void* localMaterial = rouletteOutlineMaterial(textObject,sourceMaterial);
            if (localMaterial != sourceMaterial) {
                void* localArgs[] = {localMaterial};
                runtimeInvoke(setSharedMaterial,textObject,localArgs,&exception);
                if (exception) return;
            }
            commitForumTitleFont(textObject, fallback);
            return;
        }
        if (asset == fallback) {
            static int reportedRebinds = 0;
            if (reportedRebinds++ < 16)
                LOGI("Rebinding TMP material after scene reset");
        }
        void* materialArgs[] = {sourceMaterial, targetMaterial};
        void* styledMaterial = runtimeInvoke(getStyledMaterial, nullptr, materialArgs, &exception);
        if (exception || !styledMaterial) {
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
            const bool scaleBound = scaleRead && setMaterialFloat(styledMaterial, "_GradientScale", newScale);
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
        if (materialFloat(sourceMaterial, "_GradientScale", sourceScale) &&
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
            static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
            static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
            void* labelException = nullptr;
            auto* label = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, textObject, nullptr, &labelException)) : nullptr;
            void* sizeBox = getSize ? runtimeInvoke(getSize, textObject, nullptr, &labelException) : nullptr;
            const float size = sizeBox && objectUnbox ? *static_cast<float*>(objectUnbox(sizeBox)) : 0.0f;
            float originalWidth = 0.0f;
            if (!labelException && materialFloat(sourceMaterial, "_OutlineWidth", originalWidth)) {
                const std::string labelName = utf8(label);
                const bool subtitle = labelName == "Message Label" &&
                    size >= 50.0f && size <= 58.0f &&
                    originalWidth >= 0.30f && originalWidth <= 0.40f;
                const bool speakerName = labelName == "Name Label" &&
                    size >= 30.0f && size <= 34.0f &&
                    originalWidth >= 0.45f && originalWidth <= 0.55f;
                if (subtitle || speakerName) {
                    const StoryStyle style = subtitle ? StoryStyle::Subtitle :
                        StoryStyle::SpeakerName;
                    void* isolated = storyMaterial(styledMaterial, style);
                    if (isolated) assignedMaterial = isolated;
                    else LOGE("Cannot isolate story material");
                }
            }
        }
        assignedMaterial = rouletteOutlineMaterial(textObject,assignedMaterial);
        void* args[] = {fallback};
        runtimeInvoke(setFont, textObject, args, &exception);
        if (exception) { LOGE("TMP_Text.set_font failed"); return; }
        void* styleArgs[] = {assignedMaterial};
        runtimeInvoke(setSharedMaterial, textObject, styleArgs, &exception);
        if (exception) { LOGE("TMP_Text.set_fontSharedMaterial failed"); return; }
        commitForumTitleFont(textObject, fallback);
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

void routeTmpFontAfterSetter(void* textObject, Il2CppString* rendered) {
    if (!fontEnabled || !usesIndependentFontRouting() || !textObject) return;
    // 3D TMP does not receive TextMeshProUGUI.OnPreRenderCanvas. The setter's
    // completed value is the authoritative classification for every TMP type.
    const std::string text = utf8(rendered);
    const bool target = text.find("开始") != std::string::npos || text.find("開始") != std::string::npos;
    static int reported = 0;
    const bool logTarget = target && reported++ < 16;
    const auto report = [&](const char* stage) {
        static const auto getFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font", 0);
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        void* error = nullptr;
        void* font = getFont ? runtimeInvoke(getFont, textObject, nullptr, &error) : nullptr;
        auto* name = !error && font && getName
            ? static_cast<Il2CppString*>(runtimeInvoke(getName, font, nullptr, &error)) : nullptr;
        void* klass = objectGetClass ? objectGetClass(textObject) : nullptr;
        const char* type = klass && classGetName ? classGetName(klass) : "?";
        const std::string path = textObjectPath(textObject);
        LOGI("TMP start label font route: stage=%s path=%s type=%s ui=%d font=%s",
             stage, path.c_str(), type ? type : "?", isTmpUi(textObject), error ? "?" : utf8(name).c_str());
    };
    if (logTarget) report("before-route");
    updateFont(textObject);
    if (logTarget) report("after-route");
}

void diagnosePickerGeometry() {
    if (!pickerDiagnosticWeak || !gcHandleGetTarget || !runtimeInvoke || !objectUnbox) return;
    void* self = gcHandleGetTarget(pickerDiagnosticWeak);
    if (!self) {
        if (gcHandleFree) gcHandleFree(pickerDiagnosticWeak);
        pickerDiagnosticWeak = 0;
        return;
    }
    if (pickerDiagnosticPasses >= 3) return;
    ++pickerDiagnosticPasses;
    nlohmann::json info = {{"pass", pickerDiagnosticPasses}};
    const auto invoke = [&](const char* assembly, const char* ns, const char* type, const char* method, void* object) -> void* {
        const auto candidate = methodInfo(assembly, ns, type, method, 0);
        void* error = nullptr;
        void* result = candidate ? runtimeInvoke(candidate, object, nullptr, &error) : nullptr;
        return error ? nullptr : result;
    };
    void* font = invoke("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font", self);
    const auto face = [&](void* fontAsset) {
        nlohmann::json result = nlohmann::json::object();
        if (!fontAsset) return result;
        void* boxed = invoke("Unity.TextMeshPro.dll", "TMPro", "TMP_FontAsset", "get_faceInfo", fontAsset);
        void* klass = boxed && objectGetClass ? objectGetClass(boxed) : nullptr;
        if (!klass || !classGetFieldFromName || !fieldGetValue) return result;
        const char* fields[] = {"m_Scale", "m_LineHeight", "m_AscentLine", "m_CapLine", "m_MeanLine", "m_Baseline", "m_DescentLine"};
        for (const char* name : fields) {
            void* field = classGetFieldFromName(klass, name);
            float value = 0.0f;
            if (field) { fieldGetValue(boxed, field, &value); result[name] = value; }
        }
        void* pointField = classGetFieldFromName(klass, "m_PointSize");
        int32_t point = 0;
        if (pointField) { fieldGetValue(boxed, pointField, &point); result["m_PointSize"] = point; }
        return result;
    };
    info["face"] = face(font);
    const auto original = labelOriginalFonts.find(font);
    if (original != labelOriginalFonts.end()) info["original_face"] = face(original->second);
    void* overflow = invoke("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_overflowMode", self);
    if (overflow) info["overflow_mode"] = *static_cast<int32_t*>(objectUnbox(overflow));
    void* size = invoke("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", self);
    if (size) info["font_size"] = *static_cast<float*>(objectUnbox(size));
    void* color = invoke("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_color", self);
    if (color) info["alpha"] = static_cast<float*>(objectUnbox(color))[3];
    void* bounds = invoke("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_textBounds", self);
    if (bounds) {
        auto* v = static_cast<float*>(objectUnbox(bounds));
        info["text_bounds_center"] = {v[0], v[1], v[2]};
        info["text_bounds_extents"] = {v[3], v[4], v[5]};
    }
    void* transform = invoke("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", self);
    void* rect = transform ? invoke("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", transform) : nullptr;
    if (rect) { auto* v = static_cast<float*>(objectUnbox(rect)); info["rect"] = {v[0],v[1],v[2],v[3]}; }
    void* renderer = invoke("UnityEngine.UI.dll", "UnityEngine.UI", "Graphic", "get_canvasRenderer", self);
    void* rendererAlpha = renderer ? invoke("UnityEngine.UIModule.dll", "UnityEngine", "CanvasRenderer", "GetAlpha", renderer) : nullptr;
    if (rendererAlpha) info["canvas_renderer_alpha"] = *static_cast<float*>(objectUnbox(rendererAlpha));
    void* cull = renderer ? invoke("UnityEngine.UIModule.dll", "UnityEngine", "CanvasRenderer", "get_cull", renderer) : nullptr;
    if (cull) info["canvas_cull"] = *static_cast<bool*>(objectUnbox(cull));
    void* clipping = renderer ? invoke("UnityEngine.UIModule.dll", "UnityEngine", "CanvasRenderer", "get_hasRectClipping", renderer) : nullptr;
    if (clipping) info["rect_clipping"] = *static_cast<bool*>(objectUnbox(clipping));
    // This diagnostic was scheduled only by the exact four-character buffer;
    // its output contains numeric geometry/metrics, never label or player text.
    LOGI("Message picker geometry: %s", info.dump().c_str());
}

void routeTmpFontBeforeGlyphLookup(void* self, void* array) {
    if (!fontEnabled || !usesIndependentFontRouting() || !self || !array) return;
    // This 64-bit IL2CPP array carries final UnicodeChar text, including TMP's
    // formatted SetText/SetCharArray paths whose string getter may lag behind.
    auto* data = static_cast<char*>(array);
    const size_t count = *reinterpret_cast<size_t*>(data + 24);
    if (count > 65536) return;
    const auto* chars = reinterpret_cast<const mixed_font_router::UnicodeChar*>(data + 32);
    const bool numericCount = ui_font_policy::isNumericBuffer(chars, count);
    if (fontMode == FontMode::ReplaceUi && isTmpUi(self)) {
        if (auto* baseline = fontBaseline(self)) {
            baseline->glyphFontRouteKnown = true;
            baseline->glyphNumericCount = numericCount;
        }
    }
    const bool useRhr = fontMode == FontMode::ReplaceUi
        ? ui_font_policy::usesReplacement(uiFontPolicyPath(self), numericCount)
        : (mixed_font_router::hasCjkBuffer(chars, count) ||
           (count >= 5 && (chars[0].unicode == '3' || chars[0].unicode == 'L') &&
            panorama_font_labels::shouldUseRhr(textObjectPath(self), chars, count)) ||
           (mixed_font_router::isKnownShowcaseBgmBuffer(chars, count) &&
            mixed_font_router::isShowcaseBgmHeader(textObjectPath(self))));
    const bool messagePicker = count >= 5 && chars[0].unicode == 0x9009 &&
        chars[1].unicode == 0x62e9 && chars[2].unicode == 0x6d88 &&
        chars[3].unicode == 0x606f && chars[4].unicode == 0;
    static int messageReports = 0;
    const bool logMessage = messagePicker && messageReports++ < 12;
    if (messagePicker && gcHandleNewWeak && gcHandleGetTarget && gcHandleFree &&
        (!pickerDiagnosticWeak || gcHandleGetTarget(pickerDiagnosticWeak) != self)) {
        if (pickerDiagnosticWeak) gcHandleFree(pickerDiagnosticWeak);
        pickerDiagnosticWeak = gcHandleNewWeak(self, false);
        pickerDiagnosticPasses = 0;
    }
    static int reported = 0;
    bool logTarget = false;
    if (reported < 16 && runtimeInvoke) {
        static const auto getObjectName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        void* error = nullptr;
        auto* name = getObjectName
            ? static_cast<Il2CppString*>(runtimeInvoke(getObjectName, self, nullptr, &error)) : nullptr;
        if (!error && utf8(name) == "LabelText") {
            const std::string path = textObjectPath(self);
            logTarget = path.find("/League/StartTimeLabelRoot/LabelText") != std::string::npos;
            if (logTarget) ++reported;
        }
    }
    const auto report = [&](const char* stage) {
        static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
        static const auto getFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font", 0);
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        void* error = nullptr;
        auto* text = getText ? static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error)) : nullptr;
        const bool getterCjk = !error && text && text->length > 0 &&
            mixed_font_router::hasCjk(text->chars, static_cast<size_t>(text->length));
        const int32_t getterLength = !error && text ? text->length : -1;
        error = nullptr;
        void* font = getFont ? runtimeInvoke(getFont, self, nullptr, &error) : nullptr;
        auto* name = !error && font && getName
            ? static_cast<Il2CppString*>(runtimeInvoke(getName, font, nullptr, &error)) : nullptr;
        LOGI("League glyph lookup font route: stage=%s getter_cjk=%d getter_length=%d use_replacement=%d buffer_capacity=%zu font=%s",
             stage, getterCjk, getterLength, useRhr, count, error ? "?" : utf8(name).c_str());
    };
    const auto reportMessage = [&](const char* stage) {
        static const auto getFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font", 0);
        static const auto getMaterial = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSharedMaterial", 0);
        static const auto getId = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "GetInstanceID", 0);
        static void* maskClass = findClass("UnityEngine.UI.dll", "UnityEngine.UI", "MaskableGraphic");
        static void* maskField = maskClass && classGetFieldFromName
            ? classGetFieldFromName(maskClass, "m_MaskMaterial") : nullptr;
        void* error = nullptr;
        void* font = getFont ? runtimeInvoke(getFont, self, nullptr, &error) : nullptr;
        error = nullptr;
        void* shared = getMaterial ? runtimeInvoke(getMaterial, self, nullptr, &error) : nullptr;
        void* mask = nullptr;
        if (maskField && fieldGetValue) fieldGetValue(self, maskField, &mask);
        void* sharedAtlas = shared ? materialTexture(shared, "_MainTex") : nullptr;
        void* maskAtlas = mask ? materialTexture(mask, "_MainTex") : nullptr;
        const auto id = [&](void* object) -> int32_t {
            if (!object || !getId || !objectUnbox) return 0;
            void* exception = nullptr;
            void* box = runtimeInvoke(getId, object, nullptr, &exception);
            return !exception && box ? *static_cast<int32_t*>(objectUnbox(box)) : 0;
        };
        LOGI("Message picker glyph font route: stage=%s font_id=%d shared_atlas_id=%d cached_mask_atlas_id=%d cached_mask_present=%d atlas_equal=%d",
             stage, id(font), id(sharedAtlas), id(maskAtlas), mask != nullptr,
             sharedAtlas && sharedAtlas == maskAtlas);
    };
    if (logMessage) reportMessage("before");
    if (logTarget) report("before");
    // Switch before SetArraySizes chooses glyphs, materials and UV references.
    // The original method below rebuilds them coherently from the chosen font.
    bool forumTitle = false;
    if (!messagePicker) {
        static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
        void* error = nullptr;
        auto* name = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error)) : nullptr;
        forumTitle = !error && utf8(name) == "Title";
    }
    const bool trackSubmit = usesIndependentFontRouting() && isTmpUi(self);
    void* beforeFont = trackSubmit ? readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_font") : nullptr;
    void* beforeMaterial = trackSubmit ? readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_fontSharedMaterial") : nullptr;
    updateFont(self, false, &useRhr, messagePicker, forumTitle ? chars : nullptr, forumTitle ? count : 0);
    if (trackSubmit) {
        void* afterFont = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_font");
        void* afterMaterial = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_fontSharedMaterial");
        if (beforeFont != afterFont || beforeMaterial != afterMaterial) {
            if (auto* baseline = fontBaseline(self)) baseline->pendingRendererSubmit = true;
        }
    }
    if (logTarget) report("after");
    if (logMessage) reportMessage("after");
}
void* prepareLoveEpisodeGlyphs(void* self, void* array) {
    static thread_local bool updating = false;
    if (updating || !self || !array || !runtimeInvoke || !fieldGetValue ||
        !classGetFieldFromName || !stringNew) return array;
    const auto* input = static_cast<const Il2CppByteArray*>(array);
    if (input->length > 65536) return array;
    const auto* chars = reinterpret_cast<const mixed_font_router::UnicodeChar*>(input->data);
    const auto number = love_episode_label::numberBuffer(input->length,
        [&](size_t i) { return chars[i].unicode; });
    if (!number || textObjectPath(self) != love_episode_label::path) return array;
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
        if (reports++ < 4) LOGE("Love episode buffer API unavailable: field=%d setter=%d", processingField != nullptr, setText != nullptr);
        return array;
    }
    const auto desired = love_episode_label::format(love_episode_label::path, *number);
    auto* replacement = desired ? stringNew(desired->c_str()) : nullptr;
    if (!replacement) return array;
    updating = true;
    // SetText rebuilds the backing/processing arrays even if m_text already
    // equals this string. set_text alone can short-circuit on that equality.
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
    if (!complete) {
        LOGE("Cannot rebuild Love episode glyph buffer");
        return array;
    }
    static int reports = 0;
    if (reports++ < 8) LOGI("Restored complete Love episode glyph label: episode=%s", number->c_str());
    return updated;
}

int32_t hookTmpUiSetArraySizes(void* self, void* array, void* method) {
    array = prepareLoveEpisodeGlyphs(self, array);
    routeTmpFontBeforeGlyphLookup(self, array);
    const int32_t result = origTmpUiSetArraySizes(self, array, method);
    diagnoseHomeTalkGlyph(self, "post_array_sizes");
    return result;
}
int32_t hookTmp3dSetArraySizes(void* self, void* array, void* method) {
    routeTmpFontBeforeGlyphLookup(self, array);
    const int32_t result = origTmp3dSetArraySizes(self, array, method);
    diagnoseHomeTalkGlyph(self, "post_array_sizes_3d");
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

void installTextHooks() {
    if (hooksInstalled.exchange(true)) return;
    int installed = 0;
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
    installed += installMethod("TextMeshProUGUI.OnEnable",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI", "OnEnable", 0),
        reinterpret_cast<void*>(hookTmpOnEnable), origTmpOnEnable);
    installed += installMethod("TextMeshProUGUI.OnPreRenderCanvas",
        methodPointer("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI", "OnPreRenderCanvas", 0),
        reinterpret_cast<void*>(hookTmpPreRender), origTmpPreRender);
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
    // Observe the game's own badge decision without changing viewed state.
    // Signature-check both instance and static variants before installing.
    const auto* marketNotify = methodInfo("Assembly-CSharp.dll", "Solis.OutGame",
        "ShopNotiManager", "IsMarketNotify", 0);
    if (marketNotify && methodGetFlags && methodGetParamCount && methodGetReturnType &&
        typeGetName && il2cppFree && methodGetParamCount(marketNotify) == 0) {
        char* resultType = typeGetName(methodGetReturnType(marketNotify));
        const bool booleanResult = resultType && std::strcmp(resultType, "System.Boolean") == 0;
        if (resultType) il2cppFree(resultType);
        uint32_t implementationFlags = 0;
        if (booleanResult) {
            const bool isStatic = (methodGetFlags(marketNotify, &implementationFlags) & 0x0010u) != 0;
            if (isStatic) {
                static bool (*original)(void*) = nullptr;
                auto observer = +[](void* method) -> bool {
                    const bool result = original(method);
                    static std::atomic<unsigned> count{0};
                    if (count.fetch_add(1) < 8) LOGI("Shop badge: game IsMarketNotify=%d", result);
                    return result;
                };
                installMethod("ShopNotiManager.IsMarketNotify(observer)", marketNotify->methodPointer,
                    reinterpret_cast<void*>(observer), original);
            } else {
                static bool (*original)(void*, void*) = nullptr;
                auto observer = +[](void* self, void* method) -> bool {
                    const bool result = original(self, method);
                    static std::atomic<unsigned> count{0};
                    if (count.fetch_add(1) < 8) LOGI("Shop badge: game IsMarketNotify=%d", result);
                    return result;
                };
                installMethod("ShopNotiManager.IsMarketNotify(observer)", marketNotify->methodPointer,
                    reinterpret_cast<void*>(observer), original);
            }
        }
    }
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
    if (captureOcto) {
        installMethod("Octo.OctoManager.Setup",
            methodPointer("Octo.dll", "Octo", "OctoManager", "Setup", 2),
            reinterpret_cast<void*>(hookOctoSetup), origOctoSetup);
    }
    if (resourceReplace) {
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
    if (assetReplace && classGetMethods && methodGetName && methodGetParamCount &&
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
            if (dumpEnabled) LOGI("Octo asset callback type: %s", callbackType.c_str());
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

bool loadDictionary(const std::string& path, bool reload = false) {
    std::ifstream input(path);
    if (!input) { LOGE("Dictionary missing: %s", path.c_str()); return false; }
    try {
        nlohmann::json root = nlohmann::json::parse(input);
        if (!root.is_object()) throw std::runtime_error("dictionary must be an object");
        if (reload && (!root.contains("text") || !root.contains("i18n")))
            throw std::runtime_error("reload requires text and i18n maps");
        decltype(textDict) nextText;
        decltype(legalTextDict) nextLegal;
        decltype(i18nDict) nextI18n;
        decltype(i18nBySource) nextVariants;
        std::unordered_set<std::string> nextTranslatedValues;
        UserTemplateMatcher nextUserTemplates;
        NumericTextTemplateMatcher nextNumericTemplates;
        HomeActivityNameMatcher nextHomeActivityNames;
        auto loadMap = [](const nlohmann::json& node, auto& destination) {
            if (!node.is_object()) throw std::runtime_error("dictionary map must be an object");
            for (auto it = node.begin(); it != node.end(); ++it) {
                if (!it.value().is_string() || it.value().get<std::string>().empty())
                    throw std::runtime_error("dictionary map contains an invalid value");
                destination[it.key()] = it.value().get<std::string>();
            }
        };
        loadMap(root.value("text", nlohmann::json::object()), nextText);
        const auto numericEntries = root.value("numeric_text_templates", nlohmann::json::object());
        if (!numericEntries.is_object() || numericEntries.size() > 128)
            throw std::runtime_error("invalid numeric_text_templates map");
        for (auto it = numericEntries.begin(); it != numericEntries.end(); ++it) {
            if (!it.value().is_string() || !nextNumericTemplates.add(it.key(), it.value().get<std::string>()))
                throw std::runtime_error("invalid numeric text template");
        }
        const auto activityNames = root.value("home_activity_names", nlohmann::json::object());
        if (!activityNames.is_object() || activityNames.size() > 64)
            throw std::runtime_error("invalid home_activity_names map");
        for (auto it = activityNames.begin(); it != activityNames.end(); ++it) {
            if (!it.value().is_string() ||
                    !nextHomeActivityNames.add(it.key(), it.value().get<std::string>()))
                throw std::runtime_error("invalid home activity name");
        }
        const auto legalEntries = root.value("legal_by_type", nlohmann::json::object());
        if (!legalEntries.is_object())
            throw std::runtime_error("legal_by_type must be an object");
        for (auto it = legalEntries.begin(); it != legalEntries.end(); ++it) {
            const std::string type = it.key();
            if (type.size() != 1 || type[0] < '1' || type[0] > '7' ||
                !it.value().is_object() || it.value().size() != 2 ||
                !it.value().contains("source") || !it.value().contains("translation") ||
                !it.value()["source"].is_string() ||
                !it.value()["translation"].is_string())
                throw std::runtime_error("invalid legal_by_type entry");
            const auto source = it.value()["source"].get<std::string>();
            const auto translated = it.value()["translation"].get<std::string>();
            if (source.empty() || translated.empty() ||
                source.size() > 1024 * 1024 || translated.size() > 1024 * 1024 ||
                source.find('\0') != std::string::npos ||
                translated.find('\0') != std::string::npos)
                throw std::runtime_error("legal_by_type text size invalid");
            const auto previous = nextLegal.find(source);
            if (previous != nextLegal.end() && previous->second != translated)
                throw std::runtime_error("legal_by_type source conflict");
            nextLegal.emplace(source, translated);
        }
        loadMap(root.value("i18n", nlohmann::json::object()), nextI18n);
        const auto variants = root.value("i18n_by_source", nlohmann::json::object());
        if (!variants.is_object())
            throw std::runtime_error("i18n_by_source must be an object");
        for (auto it = variants.begin(); it != variants.end(); ++it)
            loadMap(it.value(), nextVariants[it.key()]);
        const bool nextDumpEnabled = root.value("dump", false);
        const bool nextResourceReplace = root.value("resource_replace", true);
        const bool nextAssetReplace = root.value("asset_replace", true);
        const bool nextCaptureOcto = root.value("capture_octo", false);
        const bool nextProbeMaster = root.value("probe_master", false);
        const bool nextFontEnabled = root.value("font", false);
        const std::string mode = root.value("font_mode", std::string("fallback"));
        FontMode nextFontMode;
        if (mode == "replace-translated") nextFontMode = FontMode::ReplaceTranslated;
        else if (mode == "replace-all") nextFontMode = FontMode::ReplaceAll;
        else if (mode == "preserve-non-cjk") nextFontMode = FontMode::PreserveNonCjk;
        else if (mode == "replace-ui") nextFontMode = FontMode::ReplaceUi;
        else if (mode == "fallback") nextFontMode = FontMode::Fallback;
        else throw std::runtime_error("font_mode must be fallback, replace-translated, replace-all, preserve-non-cjk, or replace-ui");
        for (const auto& [source, translated] : nextText)
            nextTranslatedValues.insert(translated);
        for (const auto& [source, translated] : nextLegal)
            nextTranslatedValues.insert(translated);
        for (const auto& [key, translated] : nextI18n)
            nextTranslatedValues.insert(translated);
        size_t variantCount = 0;
        for (const auto& [key, translations] : nextVariants) {
            for (const auto& [source, translated] : translations) {
                if (source != translated) nextTranslatedValues.insert(translated);
                ++variantCount;
            }
        }
        const auto storyValues = root.value("translated_values", nlohmann::json::array());
        if (!storyValues.is_array())
            throw std::runtime_error("translated_values must be an array");
        for (const auto& value : storyValues) {
            if (!value.is_string())
                throw std::runtime_error("translated_values entries must be strings");
            const std::string translated = value.get<std::string>();
            if (!translated.empty()) nextTranslatedValues.insert(translated);
        }
        for (const auto& translated : nextTranslatedValues)
            nextUserTemplates.add(translated);
        std::lock_guard<std::recursive_mutex> dictionaryLock(dictionaryMutex);
        if (reload && (nextDumpEnabled != dumpEnabled ||
                nextResourceReplace != resourceReplace || nextAssetReplace != assetReplace ||
                nextCaptureOcto != captureOcto || nextProbeMaster != probeMaster ||
                nextFontEnabled != fontEnabled || nextFontMode != fontMode))
            throw std::runtime_error("runtime configuration changes require restarting the game");
        for (const auto& source : masterBlobPatches.suppressedSources())
            nextText.erase(source);
        textDict.swap(nextText);
        legalTextDict.swap(nextLegal);
        i18nDict.swap(nextI18n);
        i18nBySource.swap(nextVariants);
        translatedValues.swap(nextTranslatedValues);
        translatedUserTemplates = std::move(nextUserTemplates);
        numericTextTemplates = std::move(nextNumericTemplates);
        homeActivityNames = std::move(nextHomeActivityNames);
        if (reload) translationGeneration.fetch_add(1, std::memory_order_release);
        if (!reload) {
            dumpEnabled = nextDumpEnabled;
            resourceReplace = nextResourceReplace;
            assetReplace = nextAssetReplace;
            captureOcto = nextCaptureOcto;
            probeMaster = nextProbeMaster;
            fontEnabled = nextFontEnabled;
            fontMode = nextFontMode;
        }
        LOGI("Loaded %zu text, %zu legal, %zu i18n, %zu source variants, %zu unique translated values and %zu player-name templates; dump=%d",
             textDict.size(), legalTextDict.size(), i18nDict.size(), variantCount, translatedValues.size(),
             translatedUserTemplates.size(), dumpEnabled);
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
    std::lock_guard<std::recursive_mutex> frameLock(dictionaryMutex);
    std::lock_guard<std::mutex> lock(profileMutex);
    fontProfiles = std::move(candidate);
    fontProfileGeneration.fetch_add(1);
    return JNI_TRUE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_dreamgallery_idoly_localify_HookEntry_nativeResetFontProfiles(JNIEnv*, jclass) {
    std::lock_guard<std::recursive_mutex> frameLock(dictionaryMutex);
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
    JNIEnv* env, jclass clazz, jstring libraryPath, jstring dictionaryPath, jstring outputPath) {
    if (installerCalled.exchange(true)) return JNI_TRUE;
    story_bridge::initialize(env, clazz);
    dumpPath = javaString(env, outputPath);
    octoSettingsPath = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/octo-settings.json";
    resourceRoot = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/resource";
    assetRoot = dumpPath.substr(0, dumpPath.find_last_of('/')) + "/asset";
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
            size_t suppressed = 0;
            for (const auto& source : masterBlobPatches.suppressedSources())
                suppressed += textDict.erase(source);
            LOGI("Loaded %zu MasterDB Blob patches; suppressed %zu global Text sources",
                 patches, suppressed);
        } catch (const std::exception& error) {
            LOGE("MasterDB Blob patches unavailable: %s", error.what());
        }
    }
    il2cppHandle = xdl_open(library.c_str(), RTLD_NOW);
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
