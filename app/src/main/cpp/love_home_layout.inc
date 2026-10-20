// The home bubble has a minimum height. Center its text within the layout
// group's symmetric padding, including when the text is shorter than that
// minimum; TMP alignment alone only centers within the smaller text rectangle.
void centerLoveHomeDialogue(void* self) {
    if (!self || !translations.options().fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    static bool running = false;
    if (running) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getSize = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize", 0);
    static const auto getAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment", 0);
    static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
    if (!getName || !getSize || !getAlignment || !setAlignment) return;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    if (error || utf8(name) != "SerifLabel") return;
    static const std::string suffix = "/LoveWindow(Clone)/LoveHomeScreen(Clone)/ScreenCanvas/ScreenRoot/Serif/SerifBox/SerifLabel";
    const auto path = textObjectPath(self);
    if (path.size() < suffix.size() || path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) return;
    void* sizeBox = runtimeInvoke(getSize, self, nullptr, &error);
    if (error || !sizeBox || std::fabs(*static_cast<float*>(objectUnbox(sizeBox)) - 28.0f) > .5f) return;
    void* alignmentBox = runtimeInvoke(getAlignment, self, nullptr, &error);
    if (error || !alignmentBox) return;
    const int current = *static_cast<int*>(objectUnbox(alignmentBox));
    if (current != 257 && current != 4097) return;
    running = true;
    struct Reset { bool& value; ~Reset() { value = false; } } reset{running};
    if (current == 257) {
        int desired = 4097; // MidlineLeft
        void* args[] = {&desired};
        runtimeInvoke(setAlignment, self, args, &error);
    }
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto getChildAlignment = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "LayoutGroup", "get_childAlignment", 0);
    static const auto setChildAlignment = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "LayoutGroup", "set_childAlignment", 1);
    static const auto rebuild = methodInfo("UnityEngine.UI.dll", "UnityEngine.UI", "LayoutRebuilder", "ForceRebuildLayoutImmediate", 1);
    if (error || !getTransform || !getParent || !getChildAlignment || !setChildAlignment || !rebuild) return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* parent = !error && transform ? runtimeInvoke(getParent, transform, nullptr, &error) : nullptr;
    if (error || !parent) return;
    void* group = getComponentByType(parent, "UnityEngine.UI.dll", "UnityEngine.UI", "HorizontalLayoutGroup");
    if (!group) return;
    void* box = runtimeInvoke(getChildAlignment, group, nullptr, &error);
    if (error || !box || *static_cast<int*>(objectUnbox(box)) != 0) return;
    int middleLeft = 3;
    void* args[] = {&middleLeft};
    runtimeInvoke(setChildAlignment, group, args, &error);
    if (error) return;
    void* rebuildArgs[] = {parent};
    runtimeInvoke(rebuild, nullptr, rebuildArgs, &error);
}
