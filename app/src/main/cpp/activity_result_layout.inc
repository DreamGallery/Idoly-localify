void alignActivityReceiveName(void* self) {
    if (!self || !fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    if (utf8(name) != "Name" || activity_result_layout::receiveChild(textObjectPath(self)) !=
        "ActivityLevel/ActivityInfo/Name") return;
    void* sizeBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_fontSize");
    if (!sizeBox || std::fabs(*static_cast<float*>(objectUnbox(sizeBox)) - 48) > .5f) return;
    void* error = nullptr;
    // The original four-character placeholder is 192 units wide. Its level
    // starts 21 units later; retain that gap after the translated name resizes.
    auto rect = [](const char* method, int count) {
        return methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", method, count);
    };
    void* transform = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform");
    void* parent = transform ? readFontProperty(transform, "UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "get_parent") : nullptr;
    static const auto find = methodInfo("UnityEngine.CoreModule.dll", "UnityEngine", "Transform", "Find", 1);
    const auto setPosition = rect("set_anchoredPosition", 1);
    if (!parent || !find || !stringNew || !setPosition) return;
    void* findArgs[] = {stringNew("Level")};
    void* level = runtimeInvoke(find, parent, findArgs, &error);
    if (error || !level) return;
    const auto read = [&](void* object, const char* method) -> void* {
        void* box = readFontProperty(object, "UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", method);
        return box ? objectUnbox(box) : nullptr;
    };
    float namePosition[2], levelPosition[2];
    for (auto* object : {transform, level}) {
        const auto* amin = static_cast<float*>(read(object, "get_anchorMin"));
        const auto* amax = static_cast<float*>(read(object, "get_anchorMax"));
        const auto* pivot = static_cast<float*>(read(object, "get_pivot"));
        if (!amin || !amax || !pivot || amin[0] != 0 || amin[1] != 1 ||
            amax[0] != 0 || amax[1] != 1 || pivot[0] != 0 || pivot[1] != .5f) return;
    }
    auto* pos = static_cast<float*>(read(transform, "get_anchoredPosition"));
    if (!pos) return;
    std::memcpy(namePosition, pos, sizeof(namePosition));
    pos = static_cast<float*>(read(level, "get_anchoredPosition"));
    if (!pos) return;
    std::memcpy(levelPosition, pos, sizeof(levelPosition));
    void* widthBox = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_preferredWidth");
    const float width = widthBox ? *static_cast<float*>(objectUnbox(widthBox)) : 0;
    if (!std::isfinite(width) || width <= 0 || width > 600 ||
        std::fabs(namePosition[1] + 23.995f) > .5f || std::fabs(levelPosition[1] + 28) > .5f) return;
    const float x = namePosition[0] + width + 21;
    if (std::isfinite(x) && std::fabs(levelPosition[0] - x) > .01f) {
        levelPosition[0] = x; void* args[] = {levelPosition};
        runtimeInvoke(setPosition, level, args, &error);
    }
}

void alignActivityResultAile(void* self) {
    if (!self || !fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox) return;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    if (utf8(name) != "Aile" || !activity_result_layout::aileLabel(textObjectPath(self))) return;
    auto* text = static_cast<Il2CppString*>(readFontProperty(self,
        "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_text"));
    if (utf8(text) != "应援") return;
    void* transform = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_rectTransform");
    void* bounds = transform ? readFontProperty(transform,
        "UnityEngine.CoreModule.dll", "UnityEngine", "RectTransform", "get_rect") : nullptr;
    const auto* rect = bounds ? static_cast<float*>(objectUnbox(bounds)) : nullptr;
    // The original 72 x 24 box is already centered inside the 88 x 40 badge.
    // Its three-character Japanese label used left alignment; two Chinese
    // characters need center alignment, without moving the badge or hit area.
    if (!rect || !std::isfinite(rect[2]) || !std::isfinite(rect[3]) ||
        std::fabs(rect[2] - 72) > .5f || std::fabs(rect[3] - 24) > .5f) return;
    void* box = readFontProperty(self, "Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "get_alignment");
    const auto* previous = box ? static_cast<int32_t*>(objectUnbox(box)) : nullptr;
    static const auto setAlignment = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_Text", "set_alignment", 1);
    if (!previous || !setAlignment || (*previous != 4097 && *previous != 4098)) return;
    if (*previous == 4097) {
        int32_t centered = 4098; void* args[] = {&centered}; void* error = nullptr;
        runtimeInvoke(setAlignment, self, args, &error);
        if (error) LOGE("Cannot center activity result Aile label");
    }
}

// FitToChild's implicit first-child selection mistakes TMP's generated atlas
// submesh for layout content. Keep its original self-sizing operation but
// skip child placement on the exact work-name text objects. Text vertices
// already use the parent's coordinates and must not be laid out a second time.
bool fitActivityTextWithoutChildLayout(void* self, int32_t axis) {
    if (!self || !fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox ||
        !fieldGetValue || !classGetFieldFromName) return false;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    if (utf8(name) != "Name" && utf8(name) != "ActivityName") return false;
    static void* klass = findClass("quaunity-ui.Runtime.dll", "Qua.UI", "FitToChild");
    static void* controlField = klass ? classGetFieldFromName(klass,"_controlSelfSize") : nullptr;
    static void* childField = klass ? classGetFieldFromName(klass,"_child") : nullptr;
    if (!controlField || !childField) return false;
    bool control = false; void* child = nullptr;
    fieldGetValue(self,controlField,&control); fieldGetValue(self,childField,&child);
    if (!activity_result_layout::preserveTextSubmeshLayout(textObjectPath(self),axis,control,child!=nullptr)) return false;
    void* text = getComponentByType(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text");
    void* transform = readFontProperty(text,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_rectTransform");
    static const auto preferred = methodInfo("UnityEngine.UI.dll","UnityEngine.UI","LayoutUtility","GetPreferredSize",2);
    static const auto setSize = methodInfo("UnityEngine.CoreModule.dll","UnityEngine","RectTransform","SetSizeWithCurrentAnchors",2);
    if (!transform || !preferred || !setSize) return false;
    void* error = nullptr; void* args[] = {transform,&axis};
    void* box = runtimeInvoke(preferred,nullptr,args,&error);
    if (error || !box) return false;
    float size = *static_cast<float*>(objectUnbox(box));
    void* bounds = readFontProperty(transform,"UnityEngine.CoreModule.dll","UnityEngine","RectTransform","get_rect");
    const auto* rect = bounds ? static_cast<float*>(objectUnbox(bounds)) : nullptr;
    if (!rect || !std::isfinite(size) || size < 0 || size > 2048) return false;
    if (std::fabs(rect[axis+2]-size) > .001f) {
        void* values[] = {&axis,&size}; runtimeInvoke(setSize,transform,values,&error);
        if (error) return false;
    }
    return true;
}

void hookActivityFitToChildLayout(void* self, int32_t axis, void* method) {
    if (!fitActivityTextWithoutChildLayout(self,axis)) origFitToChildLayout(self,axis,method);
}

// Normalize any submesh created before the scoped layout guard takes effect.
// This is idempotent after self-layout stops moving the generated children.
void synchronizeActivityNameSubmeshFrame(void* self) {
    if (!self || !fontEnabled || !replacesCjk() || !runtimeInvoke || !objectUnbox ||
        !classGetFieldFromName || !fieldGetValue) return;
    auto* name = static_cast<Il2CppString*>(readFontProperty(self,
        "UnityEngine.CoreModule.dll", "UnityEngine", "Object", "get_name"));
    if ((utf8(name) != "Name" && utf8(name) != "ActivityName") ||
        !activity_result_layout::activityName(textObjectPath(self))) return;
    static void* klass = findClass("Unity.TextMeshPro.dll", "TMPro", "TextMeshProUGUI");
    static void* subObjectsField = klass ? classGetFieldFromName(klass, "m_subTextObjects") : nullptr;
    static const auto updatePivot = methodInfo("Unity.TextMeshPro.dll", "TMPro", "TMP_SubMeshUI", "SetPivotDirty", 0);
    if (!subObjectsField || !updatePivot) {
        static bool reported = false;
        if (!reported) { LOGE("Activity submesh API missing: field=%d pivot=%d",subObjectsField!=nullptr,updatePivot!=nullptr); reported=true; }
        return;
    }
    void* transform = readFontProperty(self,"Unity.TextMeshPro.dll","TMPro","TMP_Text","get_rectTransform");
    const auto pivot = [&](void* target, float* output) {
        void* box = readFontProperty(target,"UnityEngine.CoreModule.dll","UnityEngine","RectTransform","get_pivot");
        if (!box) return false;
        std::memcpy(output,objectUnbox(box),sizeof(float)*2);
        return std::isfinite(output[0]) && std::isfinite(output[1]);
    };
    float ownerPivot[2];
    if (!transform || !pivot(transform,ownerPivot)) return;
    Il2CppByteArray* children = nullptr;
    fieldGetValue(self,subObjectsField,&children);
    if (!children || children->length > 64) return;
    auto** objects = reinterpret_cast<void**>(children->data);
    for (size_t i = 1; i < children->length; ++i) {
        void* child = objects[i];
        if (!child) continue;
        void* childTransform = readFontProperty(child,"UnityEngine.UI.dll","UnityEngine.UI","Graphic","get_rectTransform");
        void* parent = readFontProperty(childTransform,"UnityEngine.CoreModule.dll","UnityEngine","Transform","get_parent");
        float oldPivot[2];
        if (parent != transform || !pivot(childTransform,oldPivot)) continue;
        void* error = nullptr;
        if (activity_result_layout::needsPivotSync(ownerPivot[0],ownerPivot[1],oldPivot[0],oldPivot[1])) {
            runtimeInvoke(updatePivot,child,nullptr,&error);
        }
        // TMP vertices are already expressed in the owning text's local
        // coordinates. A retained center anchor/offset on the child must not
        // add another half-width or half-height translation to those vertices.
        const auto setVector = [&](const char* property,float x,float y) {
            const auto get = std::string("get_")+property,set=std::string("set_")+property;
            const auto setter = methodInfo("UnityEngine.CoreModule.dll","UnityEngine","RectTransform",set.c_str(),1);
            void* box = readFontProperty(childTransform,"UnityEngine.CoreModule.dll","UnityEngine","RectTransform",get.c_str());
            const auto* old = box ? static_cast<float*>(objectUnbox(box)) : nullptr;
            if (error || !old || !setter || !std::isfinite(old[0]) || !std::isfinite(old[1])) return;
            if (std::fabs(old[0]-x)<.0001f && std::fabs(old[1]-y)<.0001f) return;
            float desired[] = {x,y}; void* args[] = {desired};
            runtimeInvoke(setter,childTransform,args,&error);
        };
        setVector("anchorMin",0,0);
        setVector("anchorMax",1,1);
        setVector("sizeDelta",0,0);
        setVector("anchoredPosition",0,0);
        if (error) { LOGE("Cannot synchronize activity name submesh frame"); continue; }
    }
}
