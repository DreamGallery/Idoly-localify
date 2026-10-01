// Only the ID search button's parent offset differs in the original prefab.
// Keep FitToChild, TMP alignment, dimensions and horizontal position intact.
void alignGallerySearchFooter(void* self) {
    if (!self || !fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox || !stringNew) return;
    static const auto getName = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name", 0);
    static const auto getTransform = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Component", "get_transform", 0);
    static const auto getParent = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent", 0);
    static const auto find = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "Find", 1);
    static const auto getPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_anchoredPosition", 0);
    static const auto setPosition = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "set_anchoredPosition", 1);
    if (!getName || !getTransform || !getParent || !find || !getPosition || !setPosition) return;
    void* error = nullptr;
    auto* name = static_cast<Il2CppString*>(runtimeInvoke(getName, self, nullptr, &error));
    if (error || utf8(name) != "Text") return;
    constexpr std::string_view target = "WindowSystem/WindowRoot/ShowcaseWindow(Clone)/ShowcaseGalleryTopScreen(Clone)/ScreenCanvas/ScreenRoot/footer_menu/IdSearchButton/Text";
    if (textObjectPath(self) != target) return;
    void* textTransform = runtimeInvoke(getTransform, self, nullptr, &error);
    void* idButton = !error && textTransform ? runtimeInvoke(getParent, textTransform, nullptr, &error) : nullptr;
    void* footer = !error && idButton ? runtimeInvoke(getParent, idButton, nullptr, &error) : nullptr;
    if (error || !footer) return;
    auto sibling = [&](const char* childName) -> void* {
        if (error) return nullptr;
        void* childString = stringNew(childName);
        if (!childString) return nullptr;
        void* args[] = {childString};
        return runtimeInvoke(find, footer, args, &error);
    };
    void* friendButton = sibling("FriendSearchButton");
    void* tagButton = sibling("TagSearchButton");
    if (error || !friendButton || !tagButton) return;
    auto position = [&](void* transform, float (&value)[2]) {
        if (error) return false;
        void* boxed = runtimeInvoke(getPosition, transform, nullptr, &error);
        void* raw = !error && boxed ? objectUnbox(boxed) : nullptr;
        if (!raw) return false;
        std::memcpy(value, raw, sizeof(value));
        return std::isfinite(value[0]) && std::isfinite(value[1]);
    };
    float id[2], friendPos[2], tag[2];
    if (!position(idButton, id) || !position(friendButton, friendPos) || !position(tagButton, tag)) return;
    // A disagreeing reference can indicate an animated/transitional layout.
    if (std::fabs(friendPos[1] - tag[1]) > 0.01f || std::fabs(id[1] - friendPos[1]) <= 0.01f) return;
    id[1] = friendPos[1];
    void* args[] = {id};
    runtimeInvoke(setPosition, idButton, args, &error);
}
