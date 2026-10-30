// Use Unity's existing LayoutElement minimum, so FitToChild retains its own
// padding, touch area, placement and expansion for longer translations.
void preserveButtonMinimumWidth(void* self, Il2CppString* input) {
    if (measuringButtonLabel || !self || !input || !runtimeInvoke || !objectUnbox ||
        !classGetFieldFromName || !fieldGetValue || !classGetType || !typeGetObject || !stringNew) return;
    std::lock_guard<std::recursive_mutex> lock(translations.frameMutex());
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    if (utf8(name) != "Text" && utf8(name) != "Label") return;
    if (knownFontValue(self, "button_minimum_width", -1) >= 0) return;
    std::string source = utf8(input);
    const auto tagged = resolvedI18nStrings.find(input);
    if (tagged != resolvedI18nStrings.end() && gcHandleGetTarget &&
        gcHandleGetTarget(tagged->second.weakHandle) == input) source = tagged->second.source;
    if (!button_minimum_layout::plainLabel(source)) return;

    void* rect = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform");
    void* frame = readFontProperty(rect, "UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent");
    void* root = readFontProperty(frame, "UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent");
    void* fit = getComponentByType(frame, "quaunity-ui.Runtime.dll", "Qua.UI", "FitToChild");
    void* button = getComponentByType(root, "Assembly-CSharp.dll", "Solis.Common", "TextButton");
    if (!rect || !fit || !button) return;
    static void* buttonClass = findClass("Assembly-CSharp.dll", "Solis.Common", "TextButton");
    static void* fitClass = findClass("quaunity-ui.Runtime.dll", "Qua.UI", "FitToChild");
    static void* textField = buttonClass ? classGetFieldFromName(buttonClass, "_text") : nullptr;
    static void* childField = fitClass ? classGetFieldFromName(fitClass, "_child") : nullptr;
    static void* controlField = fitClass ? classGetFieldFromName(fitClass, "_controlSelfSize") : nullptr;
    static void* alignField = fitClass ? classGetFieldFromName(fitClass, "_childAlignment") : nullptr;
    static const auto childAt = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "GetChild", 1);
    if (!textField || !childField || !controlField || !alignField || !childAt) return;
    void* label = nullptr; void* child = nullptr; bool control = false; int alignment = -1;
    fieldGetValue(button, textField, &label); fieldGetValue(fit, childField, &child);
    fieldGetValue(fit, controlField, &control); fieldGetValue(fit, alignField, &alignment);
    void* error = nullptr;
    if (!child) {
        int zero = 0; void* args[] = {&zero};
        child = runtimeInvoke(childAt, frame, args, &error);
    }
    if (error || !button_minimum_layout::eligible(label == self, control, child == rect, alignment)) return;

    static const auto measure = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "GetPreferredValues", 1);
    static const auto setFont = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_font", 1);
    static const auto setMaterial = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_fontSharedMaterial", 1);
    static const auto setMinimum = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "LayoutElement", "set_minWidth", 1);
    static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
    const auto* setTextOne = origTmpSetTextOne ? tmpStringSetter(1) : nullptr;
    const auto* setTextTwo = origTmpSetTextTwo ? tmpStringSetter(2) : nullptr;
    if (!measure || !setFont || !setMaterial || !setMinimum || !setAlignment || (!setTextOne && !setTextTwo)) return;
    void* font = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_font");
    void* material = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSharedMaterial");
    void* sizeBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize");
    auto* previousText = static_cast<Il2CppString*>(readFontProperty(self,
        "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text"));
    if (!font || !material || !sizeBox || !previousText) return;
    const float size = *static_cast<float*>(objectUnbox(sizeBox));
    void* layout = getComponentByType(self, "UnityEngine.UI.dll", "UnityEngine.UI", "LayoutElement");
    float oldMinimum = -1;
    if (layout) {
        void* box = readFontProperty(layout, "UnityEngine.UI.dll", "UnityEngine.UI", "LayoutElement", "get_minWidth");
        void* ignore = readFontProperty(layout, "UnityEngine.UI.dll", "UnityEngine.UI", "LayoutElement", "get_ignoreLayout");
        if (!box || !ignore || *static_cast<bool*>(objectUnbox(ignore))) return;
        oldMinimum = *static_cast<float*>(objectUnbox(box));
    }
    // Measure with the original font even if the game's setter ran after its
    // OnEnable. Glyph hooks must not route this temporary measurement to CJK.
    const auto original = labelOriginalFonts.find(font);
    void* originalFont = original == labelOriginalFonts.end() ? font : original->second;
    if (!unityObjectAlive(originalFont)) return;
    measuringButtonLabel = true;
    struct MeasurementScope { ~MeasurementScope() { measuringButtonLabel = false; } } scope;
    if (originalFont != font) {
        void* args[] = {originalFont}; runtimeInvoke(setFont, self, args, &error);
    }
    void* measureArgs[] = {stringNew(source.c_str())};
    void* measured = !error && measureArgs[0] ? runtimeInvoke(measure, self, measureArgs, &error) : nullptr;
    float extent[2]{};
    if (!error && measured) std::memcpy(extent, objectUnbox(measured), sizeof(extent));
    void* restoreError = nullptr;
    if (originalFont != font) {
        void* fontArgs[] = {font}; runtimeInvoke(setFont, self, fontArgs, &restoreError);
        void* materialArgs[] = {material}; runtimeInvoke(setMaterial, self, materialArgs, &restoreError);
    }
    // GetPreferredValues temporarily rewrites TMP's processing array. Restore
    // the old content even when the upcoming setter assigns the same string
    // (set_text would otherwise return early and leave measurement glyphs).
    if (setTextOne) origTmpSetTextOne(self, previousText, const_cast<MethodInfoHead*>(setTextOne));
    else origTmpSetTextTwo(self, previousText, true, const_cast<MethodInfoHead*>(setTextTwo));
    if (error || restoreError) return;
    const auto minimum = button_minimum_layout::minimumWidth(extent[0], extent[1], size, oldMinimum);
    if (!minimum) return;
    if (!layout) {
        static const auto addComponent = []() -> const MethodInfoHead* {
            if (!classGetMethods || !methodGetName || !methodGetParamCount || !methodGetParam || !typeGetName || !il2cppFree) return nullptr;
            void* klass = findClass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
            void* iterator = nullptr;
            if (!klass) return nullptr;
            while (const auto* m = classGetMethods(klass, &iterator)) {
                if (std::strcmp(methodGetName(m), "AddComponent") || methodGetParamCount(m) != 1) continue;
                char* type = typeGetName(methodGetParam(m, 0));
                const bool matches = type && !std::strcmp(type, "System.Type");
                if (type) il2cppFree(type);
                if (matches) return m;
            }
            return nullptr;
        }();
        void* klass = findClass("UnityEngine.UI.dll", "UnityEngine.UI", "LayoutElement");
        void* type = klass ? typeGetObject(classGetType(klass)) : nullptr;
        void* go = readFontProperty(self, "UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_gameObject");
        if (!addComponent || !type || !go) return;
        void* args[] = {type}; layout = runtimeInvoke(addComponent, go, args, &error);
    }
    if (error || !layout) return;
    float width = *minimum; void* widthArgs[] = {&width};
    runtimeInvoke(setMinimum, layout, widthArgs, &error);
    if (error) return;
    void* alignBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment");
    if (alignBox) {
        int centered = (*static_cast<int*>(objectUnbox(alignBox)) & ~0xff) | 2;
        void* args[] = {&centered}; runtimeInvoke(setAlignment, self, args, &error);
    }
    originalFontValue(self, "button_minimum_width", width);
}
