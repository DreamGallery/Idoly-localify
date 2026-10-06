// The original prompts use left alignment and Japanese manual line breaks.
// Their RectTransforms already span the intended area; center only the text.
void alignGachaPickupLabels(void* self) {
    if (!self || !runtimeInvoke || !objectUnbox) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    static const auto getAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment", 0);
    static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
    if (!getName || !getText || !getAlignment || !setAlignment) return;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    const auto labelName = utf8(name);
    if (error || (labelName != "Description" && labelName != "description")) return;
    auto* text = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error));
    if (error || !text) return;
    const auto label = gacha_pickup_layout::classify(textObjectPath(self), utf8(text));
    if (label == gacha_pickup_layout::Label::None) return;
    void* box = runtimeInvoke(getAlignment, self, nullptr, &error);
    auto* alignment = !error && box ? static_cast<int32_t*>(objectUnbox(box)) : nullptr;
    if (!alignment) return;
    if (*alignment != 4098) {
        int32_t centered = 4098; void* args[] = {&centered};
        runtimeInvoke(setAlignment, self, args, &error);
    }
    if (error || label != gacha_pickup_layout::Label::DrawPrompt) return;
    static const auto getTransform = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform", 0);
    static const auto getPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
    static const auto setPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    if (!getTransform || !getPosition || !setPosition) return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    box = !error && transform ? runtimeInvoke(getPosition, transform, nullptr, &error) : nullptr;
    auto* position = !error && box ? static_cast<float*>(objectUnbox(box)) : nullptr;
    // The reviewed prefab offsets its centered prompt upward by 12 canvas
    // units for three Japanese lines. The Chinese one-line prompt needs no offset.
    if (!position || std::fabs(position[0]) > 0.01f || std::fabs(position[1] - 12.0f) > 0.01f) return;
    float centered[] = {position[0], 0.0f}; void* args[] = {centered};
    runtimeInvoke(setPosition, transform, args, &error);
    if (error) LOGE("Cannot center gacha pickup prompt");
}
