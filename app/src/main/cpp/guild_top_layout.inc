// TMP supports selecting a font within a single label. Register the retained
// game font and apply it only to this control's numeric suffix, including commas.
std::string guildMedalRenderedText(void* self, const std::string& plain) {
    if (!self || !fontEnabled || !usesIndependentFontRouting() || !runtimeInvoke ||
        !objectUnbox || !stringNew) return {};
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    if (guild_medal_font::decorate(plain, "probe").empty()) return {};
    std::lock_guard<std::recursive_mutex> lock(fontMutex);
    void* font = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font");
    const auto found = labelOriginalFonts.find(font);
    void* original = found == labelOriginalFonts.end() ? font : found->second;
    if (!unityObjectAlive(original) || !getName) return {};
    void* error = nullptr;
    auto* fontName = static_cast<Il2CppString*>(runtimeInvoke(getName, original, nullptr, &error));
    const auto rendered = !error ? guild_medal_font::decorate(plain, utf8(fontName)) : std::string();
    if (rendered.empty()) return {};
    static const auto addFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "MaterialReferenceManager", "AddFontAsset", 1);
    static const auto findFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "MaterialReferenceManager", "TryGetFontAsset", 2);
    static const auto getRich = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_richText", 0);
    static const auto setRich = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_richText", 1);
    static void* assetClass = findClass("Unity.TextMeshPro.dll", "TMPro", "TMP_Asset");
    static void* hashField = assetClass && classGetFieldFromName
        ? classGetFieldFromName(assetClass, "hashCode") : nullptr;
    if (!addFont || !findFont || !getRich || !setRich || !hashField || !fieldGetValue) return {};
    int32_t hash = 0; fieldGetValue(original, hashField, &hash);
    void* addArgs[] = {original}; runtimeInvoke(addFont, nullptr, addArgs, &error);
    void* registered = nullptr; void* findArgs[] = {&hash, &registered};
    void* foundBox = !error ? runtimeInvoke(findFont, nullptr, findArgs, &error) : nullptr;
    auto* success = !error && foundBox ? static_cast<bool*>(objectUnbox(foundBox)) : nullptr;
    if (!success || !*success || registered != original) return {};
    void* richBox = runtimeInvoke(getRich, self, nullptr, &error);
    auto* rich = !error && richBox ? static_cast<bool*>(objectUnbox(richBox)) : nullptr;
    if (!rich) return {};
    if (!*rich) { bool enabled = true; void* args[] = {&enabled}; runtimeInvoke(setRich, self, args, &error); }
    return error ? std::string() : rendered;
}

// These two titles have different left-pivot bounds in the original prefab.
// Center each existing text box in its own tile, preserving vertical placement.
void alignGuildTopTitles(void* self) {
    if (!self || !runtimeInvoke || !objectUnbox) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    if (!getName || !getText) return;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    if (error || utf8(name) != "Title" || !guildTopButtonTitle(textObjectPath(self))) return;
    auto* text = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error));
    if (error || (utf8(text) != "联盟演唱会" && utf8(text) != "工作人员培养")) return;
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
    static const auto getPivot = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_pivot", 0);
    static const auto getMin = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchorMin", 0);
    static const auto getMax = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchorMax", 0);
    static const auto getPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
    static const auto setPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    static const auto getAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment", 0);
    static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
    if (!getTransform || !getParent || !getRect || !getPivot || !getMin || !getMax ||
        !getPosition || !setPosition || !getAlignment || !setAlignment) return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* parent = !error && transform ? runtimeInvoke(getParent, transform, nullptr, &error) : nullptr;
    if (error || !parent) return;
    auto read = [&](const MethodInfoHead* method, void* object, float* value, size_t count) {
        void* box = !error ? runtimeInvoke(method, object, nullptr, &error) : nullptr;
        void* raw = !error && box ? objectUnbox(box) : nullptr;
        if (!raw) return false;
        std::memcpy(value, raw, count * sizeof(float));
        return std::all_of(value, value + count, [](float v) { return std::isfinite(v); });
    };
    float rect[4], parentRect[4], pivot[2], amin[2], amax[2], position[2], x;
    if (!read(getRect, transform, rect, 4) || !read(getRect, parent, parentRect, 4) ||
        !read(getPivot, transform, pivot, 2) || !read(getMin, transform, amin, 2) ||
        !read(getMax, transform, amax, 2) || !read(getPosition, transform, position, 2) ||
        !centeredLabelX(parentRect[0], parentRect[2], rect[2], amin[0], amax[0], pivot[0], x)) return;
    if (std::fabs(position[0] - x) > .01f) {
        float centered[] = {x, position[1]}; void* args[] = {centered};
        runtimeInvoke(setPosition, transform, args, &error);
    }
    void* box = !error ? runtimeInvoke(getAlignment, self, nullptr, &error) : nullptr;
    auto* alignment = !error && box ? static_cast<int32_t*>(objectUnbox(box)) : nullptr;
    if (alignment && *alignment != 4098) {
        int32_t centered = 4098; void* args[] = {&centered};
        runtimeInvoke(setAlignment, self, args, &error);
    }
    if (error) LOGE("Cannot center Guild top tile title");
}

void alignGuildPointNumbers(void* self) {
    if (!self || !fontEnabled || fontMode != FontMode::ReplaceUi ||
        !runtimeInvoke || !objectUnbox || !stringNew) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    void* error = nullptr;
    auto* name = getName ? static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error)) : nullptr;
    if (error || utf8(name) != "Point") return;
    using guild_points_layout::Kind;
    const auto kind = guild_points_layout::classify(textObjectPath(self));
    if (kind != Kind::Ranking) return;
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto find = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "Find", 1);
    if (!getTransform || !getParent || !find) return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* parent = !error && transform ? runtimeInvoke(getParent, transform, nullptr, &error) : nullptr;
    if (error || !parent) return;
    void* findArgs[] = {stringNew("PointLabel")};
    void* labelTransform = runtimeInvoke(find, parent, findArgs, &error);
    void* label = !error && labelTransform ? getComponentByType(labelTransform, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text") : nullptr;
    if (error || !label) return;
    auto* text = static_cast<Il2CppString*>(readFontProperty(label, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text"));
    if (utf8(text) != "总Pt：") return;
    // The number can render before its sibling on the first frame. Resolve
    // the sibling's replacement face before reading its baseline metrics.
    const bool replacement = true;
    updateFont(label, false, &replacement, false, nullptr, 0, true);
    auto tmp = [](const char* name, int count) { return methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", name, count); };
    auto rect = [](const char* name, int count) { return methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", name, count); };
    auto read = [&](const MethodInfoHead* method, void* object, float* value, size_t count) {
        if (!method || error) return false;
        void* box = runtimeInvoke(method, object, nullptr, &error);
        void* raw = !error && box ? objectUnbox(box) : nullptr;
        if (!raw) return false;
        std::memcpy(value, raw, count * sizeof(float));
        return std::all_of(value, value + count, [](float v) { return std::isfinite(v); });
    };
    float numberSize, labelSize, numberRect[4], numberMargin[4];
    if (!read(tmp("get_fontSize", 0), self, &numberSize, 1) ||
        !read(tmp("get_fontSize", 0), label, &labelSize, 1) ||
        !read(rect("get_rect", 0), transform, numberRect, 4) ||
        !read(tmp("get_margin", 0), self, numberMargin, 4)) return;
    // Only the captured Bottom alignment uses this formula. PreviousSeason
    // retains the game's original 40-point size and geometry-centered layout.
    for (void* item : {self, label}) {
        void* box = readFontProperty(item, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment");
        if (!box || (*static_cast<int32_t*>(objectUnbox(box)) & 0xff00) != 1024) return;
    }
    float labelRect[4], labelMargin[4], numberPosition[3], labelPosition[3], numberDescent, labelDescent;
    const auto numberFace = verticalMetrics(readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font"));
    const auto labelFace = verticalMetrics(readFontProperty(label, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font"));
    static const auto getLocal = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_localPosition", 0);
    static const auto setLocal = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "set_localPosition", 1);
    if (!setLocal || !read(rect("get_rect", 0), labelTransform, labelRect, 4) ||
        !read(tmp("get_margin", 0), label, labelMargin, 4) ||
        !read(getLocal, transform, numberPosition, 3) || !read(getLocal, labelTransform, labelPosition, 3) ||
        !guild_points_layout::scaledDescent(numberFace, numberSize, numberDescent) ||
        !guild_points_layout::scaledDescent(labelFace, labelSize, labelDescent)) return;
    const float desired = guild_points_layout::bottomBaselineY(labelPosition[1], labelRect[1], labelMargin[3],
        labelDescent, numberRect[1], numberMargin[3], numberDescent);
    if (std::isfinite(desired) && std::fabs(desired - numberPosition[1]) > .01f) {
        const float shift = desired - numberPosition[1];
        numberPosition[1] = desired; void* args[] = {numberPosition};
        runtimeInvoke(setLocal, transform, args, &error);
        static int reports = 0;
        if (!error && reports++ < 4) LOGI("Guild ranking Pt baseline shift: %.2f", shift);
    }
    if (error) LOGE("Cannot align Guild Pt number");
}
