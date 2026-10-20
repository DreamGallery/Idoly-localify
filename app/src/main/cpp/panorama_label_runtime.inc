// Included in localify.cpp's implementation namespace before its label fitters.
// Signature-selected overload: Component.GetComponent(System.Type), never string.
void* getComponentByType(void* component, const char* assembly, const char* ns, const char* className) {
    if (!component || !runtimeInvoke || !classGetType || !typeGetObject) return nullptr;
    static const MethodInfoHead* getComponent = []() -> const MethodInfoHead* {
        if (!classGetMethods || !methodGetName || !methodGetParamCount || !methodGetParam || !typeGetName || !il2cppFree) return nullptr;
        void* klass = findClass("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
        if (!klass) return nullptr;
        void* iter = nullptr;
        while (const auto* method = static_cast<const MethodInfoHead*>(classGetMethods(klass, &iter))) {
            if (std::strcmp(methodGetName(method), "GetComponent") || methodGetParamCount(method) != 1) continue;
            char* name = typeGetName(methodGetParam(method, 0));
            const bool matches = name && !std::strcmp(name, "System.Type");
            if (name) il2cppFree(name);
            if (matches) return method;
        }
        return nullptr;
    }();
    if (!getComponent) return nullptr;
    void* klass = findClass(assembly, ns, className);
    void* type = klass ? typeGetObject(classGetType(klass)) : nullptr;
    if (!type) return nullptr;
    void* error = nullptr; void* args[] = {type};
    void* result = runtimeInvoke(getComponent, component, args, &error);
    return error ? nullptr : result;
}


void fitPanoramaLabels(void* self) {
    if (!self || !translations.options().fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox ||
        !stringNew || !classGetType || !typeGetObject) return;
    static bool running = false;
    if (running) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    void* error = nullptr;
    if (!getName || !getText) return;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    const auto leaf = utf8(name);
    if (error || (leaf != "360" && leaf != "LIVE")) return;
    const auto path = textObjectPath(self);
    if (panorama_font_labels::labelLeaf(path) != leaf) return;
    const bool musicTitle = path == std::string(panorama_font_labels::musicTitleRoot) + leaf;
    auto* text = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error));
    if (error || utf8(text) != (leaf == "360" ? "360°" : "LIVE")) return;
    running = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{running};
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto find = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "Find", 1);
    if (!getTransform || !getParent || !find) return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* parent = !error && transform ? runtimeInvoke(getParent, transform, nullptr, &error) : nullptr;
    if (error || !parent) return;
    void* labels[2]{}; void* rects[2]{};
    const char* names[] = {"360", "LIVE"};
    for (int i = 0; i < 2; ++i) {
        void* args[] = {stringNew(names[i])};
        rects[i] = runtimeInvoke(find, parent, args, &error);
        if (error || !rects[i]) return;
        labels[i] = getComponentByType(rects[i], "Unity.TextMeshPro.dll", "TMPro", "TMP_Text");
        if (error || !labels[i]) return;
        auto* value = static_cast<Il2CppString*>(runtimeInvoke(getText, labels[i], nullptr, &error));
        if (error || utf8(value) != (i == 0 ? "360°" : "LIVE")) return;
    }
    auto tmp = [](const char* name, int count) { return methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", name, count); };
    auto rect = [](const char* name, int count) { return methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", name, count); };
    auto read = [&](const MethodInfoHead* method, void* object, float* value, size_t count) {
        if (!method || error) return false;
        void* boxed = runtimeInvoke(method, object, nullptr, &error);
        void* raw = !error && boxed ? objectUnbox(boxed) : nullptr;
        if (!raw) return false;
        std::memcpy(value, raw, count * sizeof(float));
        return std::all_of(value, value + count, [](float v) { return std::isfinite(v); });
    };
    auto setFloats = [&](void* object, const MethodInfoHead* getter, const MethodInfoHead* setter, const float* target, size_t count) {
        float current[4];
        if (!setter || count > 4 || !read(getter, object, current, count)) return false;
        bool changed = false;
        for (size_t j = 0; j < count; ++j) changed |= std::fabs(current[j] - target[j]) > 0.05f;
        if (changed) { void* args[] = {const_cast<float*>(target)}; runtimeInvoke(setter, object, args, &error); }
        return !error;
    };
    if (musicTitle) {
        // The original music-page HorizontalLayoutGroup has spacing=-35.
        // Preserve its 60-point, left-aligned title and let the existing group
        // lay out the replacement glyph widths with a proportional positive gap.
        void* group = getComponentByType(parent, "UnityEngine.UI.dll", "UnityEngine.UI", "HorizontalLayoutGroup");
        static const auto getSpacing = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "HorizontalOrVerticalLayoutGroup", "get_spacing", 0);
        static const auto setSpacing = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "HorizontalOrVerticalLayoutGroup", "set_spacing", 1);
        static const auto rebuild = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "LayoutRebuilder", "ForceRebuildLayoutImmediate", 1);
        float bounds[4], spacing;
        if (!group || !getSpacing || !setSpacing || !rebuild ||
            !read(rect("get_rect", 0), parent, bounds, 4) ||
            !read(getSpacing, group, &spacing, 1)) return;
        float gap = panorama_font_labels::musicTitleGap(bounds[2]);
        if (!(gap > 0.0f) || std::fabs(spacing - gap) <= 0.05f) return;
        const bool useReplacement = true;
        for (void* label : labels) updateFont(label, false, &useReplacement);
        void* spacingArgs[] = {&gap};
        runtimeInvoke(setSpacing, group, spacingArgs, &error);
        if (error) return;
        void* rebuildArgs[] = {parent};
        runtimeInvoke(rebuild, nullptr, rebuildArgs, &error);
        if (error) LOGE("Cannot rebuild 360 LIVE music-page title");
        return;
    }
    float parentBounds[4], bounds[2][4], pivot[2], localPosition[3];
    static const auto getLocal = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_localPosition", 0);
    if (!read(rect("get_rect", 0), parent, parentBounds, 4) || parentBounds[2] <= 0 ||
        !read(rect("get_rect", 0), rects[0], bounds[0], 4) || !read(rect("get_rect", 0), rects[1], bounds[1], 4) ||
        !read(rect("get_pivot", 0), rects[0], pivot, 2) || !read(getLocal, rects[0], localPosition, 3)) return;
    const float y = originalFontValue(labels[0], "panorama_center_y", localPosition[1] + (0.5f - pivot[1]) * bounds[0][3] - (parentBounds[1] + parentBounds[3] * 0.5f));
    const float height = originalFontValue(labels[0], "panorama_height", std::max(bounds[0][3], bounds[1][3]));
    if (!(height > 0)) return;
    float widths[2];
    const bool useReplacement = true;
    for (int i = 0; i < 2; ++i) {
        updateFont(labels[i], false, &useReplacement);
        // All additional spacing/margins are zero, making preferred width
        // proportional to font size. No reset to 40 and no per-frame dirty loop.
        float zero = 0, margin[4] = {};
        for (const auto* property : {"characterSpacing", "wordSpacing"}) {
            const std::string get = std::string("get_") + property, set = std::string("set_") + property;
            if (!setFloats(labels[i], tmp(get.c_str(), 0), tmp(set.c_str(), 1), &zero, 1)) return;
        }
        if (!setFloats(labels[i], tmp("get_margin", 0), tmp("set_margin", 1), margin, 4)) return;
        const auto* getAuto = tmp("get_enableAutoSizing", 0); const auto* setAuto = tmp("set_enableAutoSizing", 1);
        void* boxed = getAuto ? runtimeInvoke(getAuto, labels[i], nullptr, &error) : nullptr;
        if (error || !boxed || !setAuto) return;
        if (*static_cast<bool*>(objectUnbox(boxed))) { bool off = false; void* args[] = {&off}; runtimeInvoke(setAuto, labels[i], args, &error); }
        float size, preferred;
        if (!read(tmp("get_fontSize", 0), labels[i], &size, 1) || size <= 0 ||
            !read(tmp("get_preferredWidth", 0), labels[i], &preferred, 1) || preferred <= 0) return;
        widths[i] = preferred / size * 40.0f;
    }
    const auto fit = panorama_font_labels::centeredLayout(parentBounds[2], widths[0], widths[1]);
    if (!fit.valid) return;
    const float center[] = {0.5f, 0.5f}; const float size = static_cast<float>(40.0 * fit.scale);
    for (int i = 0; i < 2; ++i) {
        const float dimensions[] = {static_cast<float>(i == 0 ? fit.leftWidth : fit.rightWidth), height};
        const float position[] = {static_cast<float>(i == 0 ? fit.leftCenterX : fit.rightCenterX), y};
        if (!setFloats(labels[i], tmp("get_fontSize", 0), tmp("set_fontSize", 1), &size, 1) ||
            !setFloats(rects[i], rect("get_anchorMin", 0), rect("set_anchorMin", 1), center, 2) ||
            !setFloats(rects[i], rect("get_anchorMax", 0), rect("set_anchorMax", 1), center, 2) ||
            !setFloats(rects[i], rect("get_pivot", 0), rect("set_pivot", 1), center, 2) ||
            !setFloats(rects[i], rect("get_sizeDelta", 0), rect("set_sizeDelta", 1), dimensions, 2) ||
            !setFloats(rects[i], rect("get_anchoredPosition", 0), rect("set_anchoredPosition", 1), position, 2)) return;
    }
}
