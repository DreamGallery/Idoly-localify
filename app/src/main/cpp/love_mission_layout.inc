// Japanese receive/check labels have different character counts and the prefab
// compensates with different button positions. Their Chinese labels both have
// two characters: align the whole receive button to its sibling, including its
// hit target. Keep the game's child fitting and vertical placement intact.
void alignLoveMissionLabels(void* self) {
    if (!self || !fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox || !stringNew) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getTransform = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto getText = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text", 0);
    if (!getName || !getTransform || !getParent || !getText) return;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    if (error || utf8(name) != "Text") return;
    void* transform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* parent = !error && transform ? runtimeInvoke(getParent, transform, nullptr, &error) : nullptr;
    auto* hostName = !error && parent ? static_cast<Il2CppString*>(runtimeInvoke(getName, parent, nullptr, &error)) : nullptr;
    if (error || !hostName) return;
    const auto host = utf8(hostName);
    if (host != "ReceiveButton" && host != "Base") return;
    const bool bulk = host == "Base";
    if (!loveMissionLabel(textObjectPath(self), bulk)) return;
    auto* text = static_cast<Il2CppString*>(runtimeInvoke(getText, self, nullptr, &error));
    if (error || utf8(text) != (bulk ? "一键领取" : "领取")) return;

    if (bulk) {
        static const auto getRect = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect", 0);
        static const auto setBounds = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "SetSizeWithCurrentAnchors", 2);
        if (!getRect || !setBounds) return;
        void* box = runtimeInvoke(getRect, parent, nullptr, &error);
        const auto* rect = !error && box ? static_cast<float*>(objectUnbox(box)) : nullptr;
        // Reviewed LoveMissionScreen prefab: the circular Base is 280 x 280;
        // four characters at the original size 40 fit in 160 Unity units.
        if (!rect || !std::isfinite(rect[2]) || !std::isfinite(rect[3]) ||
            std::fabs(rect[2] - 280.0f) > 1 || std::fabs(rect[3] - 280.0f) > 1) return;
        const float width = rect[2] * (4.0f / 7.0f);
        box = runtimeInvoke(getRect, transform, nullptr, &error);
        rect = !error && box ? static_cast<float*>(objectUnbox(box)) : nullptr;
        if (!rect) return;
        if (std::fabs(rect[2] - width) > 0.01f) {
            int32_t axis = 0; float value = width; void* args[] = {&axis, &value};
            runtimeInvoke(setBounds, transform, args, &error);
        }
        return;
    }
    static const auto find = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "Find", 1);
    static const auto getPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
    static const auto setPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    if (!find || !getPosition || !setPosition) return;
    void* cell = runtimeInvoke(getParent, parent, nullptr, &error);
    void* childName = stringNew("DetailButton"); void* findArgs[] = {childName};
    void* reference = !error && cell && childName ? runtimeInvoke(find, cell, findArgs, &error) : nullptr;
    if (error || !reference) return;
    auto position = [&](void* target, float (&value)[2]) {
        void* box = !error ? runtimeInvoke(getPosition, target, nullptr, &error) : nullptr;
        void* raw = !error && box ? objectUnbox(box) : nullptr;
        if (!raw) return false;
        std::memcpy(value, raw, sizeof(value));
        return std::isfinite(value[0]) && std::isfinite(value[1]);
    };
    float receive[2], detail[2];
    if (!position(parent, receive) || !position(reference, detail) ||
        std::fabs(receive[1] - detail[1]) > 0.01f ||
        std::fabs(detail[0] - receive[0] - 40.0f) > 0.1f) return;
    receive[0] = detail[0];
    void* args[] = {receive};
    runtimeInvoke(setPosition, parent, args, &error);
}
