// Included inside preview_access's private namespace. Profile-specific hooks
// share its checked IL2CPP adapter, but never replace account profile getters.
DecorationOverride decorationOverride;
thread_local uint32_t decorationReadScope = 0;
struct DecorationPage {
    uint32_t model = 0;
    uint64_t account = 0, generation = 0, revision = 0;
    bool failed = false;
    ~DecorationPage() { if (model) api.release(model); }
};
struct DecorationSheet {
    uint32_t presenter = 0, model = 0;
    ~DecorationSheet() { if (presenter) api.release(presenter); if (model) api.release(model); }
};
std::vector<std::unique_ptr<DecorationPage>> decorationPages;
std::vector<std::unique_ptr<DecorationSheet>> decorationSheets;
void* profilePresenterClass = nullptr;
void* profileModelField = nullptr;
void* beforeDecorationField = nullptr;
void* profileViewField = nullptr;
size_t decorationStateOwnerOffset = 0;
const Method *profileGet = nullptr, *profileClone = nullptr, *profileDecorationSetter = nullptr;
const Method* decorationGet = nullptr;
const Method *selectedDecoration = nullptr, *selectDecoration = nullptr;
const Method *profileIsPhoto = nullptr, *profileCheckBackground = nullptr;
using ProfileCtor = void (*)(void*, bool, const Method*);
using DecorationSetup = Task (*)(void*, void*, Cancellation, const Method*);
using ProfileCardData = void (*)(void*, void*, void*, void*, void*, void*, bool, const Method*);
ProfileCtor originalProfileCtor = nullptr;
DecorationSetup originalDecorationSetup = nullptr;
Void originalDecorationMoveNext = nullptr, originalDecorationButton = nullptr;
StaticRead originalDecorationCollection = nullptr;
ReadBool originalDecorationNew = nullptr;
using ReadBoolean = bool (*)(void*, const Method*);
ReadBoolean originalDecorationHasNew = nullptr, originalDecorationRecord = nullptr;
Save originalProfileSave = nullptr;
ProfileCardData originalProfileCardData = nullptr;

void clearDecorationOverride() { decorationOverride.clear(); }

template <typename T, typename Predicate>
void pruneViews(std::vector<std::unique_ptr<T>>& views, Predicate alive) {
    for (auto it = views.begin(); it != views.end();) {
        if (!alive(**it)) it = views.erase(it); else ++it;
    }
}

DecorationPage* decorationPage(void* model) {
    if (!model) return nullptr;
    for (auto& page : decorationPages) if (api.target(page->model) == model) return page.get();
    return nullptr;
}

void* profileModel(void* presenter) {
    void* value = nullptr;
    if (presenter) api.fieldGet(presenter, profileModelField, &value);
    return value;
}

bool currentDecorationPage(const DecorationPage& page) {
    return !page.failed && currentDecorationSession(policy.enabled.load() & Decorations,
        page.account, accountEpoch, page.generation, policy.generation.load(),
        page.revision, decorationOverride.revision());
}

void setPrivateDecoration(void* model, void* id) {
    void* args[] = {id};
    invoke(selectDecoration, model, args);
    api.fieldSet(model, beforeDecorationField, &id);
}

void profileCtor(void* model, bool photo, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!(policy.enabled.load() & Decorations)) { originalProfileCtor(model, photo, method); return; }
    try { snapshot(categories[3]); }
    catch (const std::exception& error) {
        categories[3].error = error.what();
        originalProfileCtor(model, photo, method);
        return;
    }
    pruneViews(decorationPages, [](const auto& page) { return api.target(page.model); });
    auto page = std::make_unique<DecorationPage>();
    page->model = api.weak(model, false);
    if (!page->model) { originalProfileCtor(model, photo, method); return; }
    page->account = accountEpoch;
    page->generation = policy.generation.load();
    page->revision = decorationOverride.revision();
    auto* entry = page.get();
    decorationPages.push_back(std::move(page));
    originalProfileCtor(model, photo, method);
    try {
        if (auto id = decorationOverride.get(accountEpoch, true)) {
            Root selected(api.newString(id->c_str()));
            setPrivateDecoration(model, selected.get());
        }
        __android_log_print(ANDROID_LOG_INFO, Tag, "Local decoration editor opened");
    } catch (const std::exception& error) {
        entry->failed = true;
        categories[3].error = error.what();
    }
}

void decorationButton(void* presenter, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto* page = decorationPage(profileModel(presenter));
    Scope active(decorationReadScope, page ? Decorations : 0);
    originalDecorationButton(presenter, method);
}

Task decorationSetup(void* self, void* id, Cancellation ct, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    bool registered = false;
    try {
        Root scene(invoke(sceneInstance, nullptr));
        Root screen(scene.get() ? invoke(topScreen, scene.get()) : nullptr);
        if (screen.get() && api.objectClass(screen.get()) == profilePresenterClass) {
            void* model = profileModel(screen.get());
            if (decorationPage(model)) {
                pruneViews(decorationSheets, [](const auto& sheet) { return api.target(sheet.presenter); });
                auto sheet = std::make_unique<DecorationSheet>();
                sheet->presenter = api.weak(self, false);
                sheet->model = api.weak(model, false);
                if (!sheet->presenter || !sheet->model) throw std::runtime_error("Cannot isolate decoration sheet");
                decorationSheets.push_back(std::move(sheet));
                registered = true;
            }
        }
    } catch (const std::exception& error) { categories[3].error = error.what(); }
    if ((policy.enabled.load() & Decorations) && !registered) {
        toast("请退出并重新打开名片编辑页后预览装饰");
        __android_log_print(ANDROID_LOG_WARN, Tag, "Decoration sheet has no isolated editor; setup skipped");
        return {};
    }
    return originalDecorationSetup(self, id, ct, method);
}

void decorationMoveNext(void* state, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    void* owner = nullptr;
    std::memcpy(&owner, static_cast<uint8_t*>(state) + decorationStateOwnerOffset, sizeof(owner));
    bool isolated = false;
    for (const auto& sheet : decorationSheets)
        if (owner && api.target(sheet->presenter) == owner && decorationPage(api.target(sheet->model))) {
            isolated = true;
            break;
        }
    // Scope each continuation separately; never keep a global flag across an
    // await. It also protects the sheet's RecordCurrentDecoration call.
    Scope active(decorationReadScope, isolated ? Decorations : 0);
    originalDecorationMoveNext(state, method);
}

bool isDecorationView(void* collection) {
    return categories[3].snapshot && api.target(categories[3].snapshot) == collection;
}

void* decorationCollection(const Method* method) {
    void* original = originalDecorationCollection(method);
    if (!(decorationReadScope & Decorations)) return original;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try { ++categories[3].uses; return snapshot(categories[3]); }
    catch (const std::exception& error) { categories[3].error = error.what(); }
    return original;
}

bool decorationNew(void* self, void* id, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return (decorationReadScope || isDecorationView(self)) ? false : originalDecorationNew(self, id, method);
}
bool decorationHasNew(void* self, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return (decorationReadScope || isDecorationView(self)) ? false : originalDecorationHasNew(self, method);
}
bool decorationRecord(void* self, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    // Fail closed if sheet ownership could not be resolved, or a disabled
    // preview page still exists. This method only records decoration badges.
    const bool previewPageAlive = std::any_of(decorationPages.begin(), decorationPages.end(),
        [](const auto& page) { return api.target(page->model) != nullptr; });
    if (decorationReadScope || isDecorationView(self) ||
        (policy.enabled.load() & Decorations) || previewPageAlive) {
        __android_log_print(ANDROID_LOG_INFO, Tag, "Decoration viewed-state write bypassed");
        return false;
    }
    return originalDecorationRecord(self, method);
}

bool loadedDecoration(void* presenter, void* model, void* row) {
    void* rawView = nullptr;
    api.fieldGet(presenter, profileViewField, &rawView);
    Root view(rawView);
    const bool photo = scalar<bool>(invoke(profileIsPhoto, model));
    Root resource(referenceField(view.get(), photo ? "_decorationForPhoto" : "_decorationForCard"));
    if (!resource.get()) return false;
    Root rawImage(invoke(checked(api.objectClass(resource.get()), "get_ImageComponent", "UnityEngine.UI.RawImage"), resource.get()));
    if (!rawImage.get()) return false;
    Root texture(invoke(checked(api.objectClass(rawImage.get()), "get_texture", "UnityEngine.Texture"), rawImage.get()));
    if (!texture.get()) return false;
    Root master(invoke(checked(api.objectClass(row), "get_Decoration", "Solis.Common.Proto.Master.Decoration"), row));
    if (!master.get()) return false;
    const auto expected = idText(invoke(checked(api.objectClass(master.get()), "GetFullAssetName", "System.String"), master.get()));
    const auto actual = idText(invoke(checked(api.objectClass(texture.get()), "get_name", "System.String"), texture.get()));
    if (!expected.empty() && actual == expected) return true;
    __android_log_print(ANDROID_LOG_WARN, Tag, "Decoration texture pending: expected=%s actual=%s", expected.c_str(), actual.c_str());
    return false;
}

Task saveDecoration(void* presenter, Cancellation ct, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    void* model = profileModel(presenter);
    auto* page = decorationPage(model);
    if (canForwardDecorationWrite(page != nullptr, policy.enabled.load()))
        return originalProfileSave(presenter, ct, method);
    if (!page) {
        // Enabling on an already-open editor, or failing to create its isolated
        // view, must not silently turn a local confirmation into an API save.
        toast("请退出并重新打开名片编辑页后预览装饰");
        return {};
    }
    // The editor's save combines decoration and background API requests. Once
    // isolated, never fall through to either request, even after disabling.
    try {
        if (!currentDecorationPage(*page)) {
            toast("预览设置已变化，请退出并重新打开名片编辑页");
            return {};
        }
        for (bool photo : {false, true}) {
            void* args[] = {&photo};
            if (scalar<bool>(invoke(profileCheckBackground, model, args))) {
                toast("此模式仅应用本机装饰；更换背景请关闭开关后重新进入");
                return {};
            }
        }
        Root selected(invoke(selectedDecoration, model));
        const auto id = idText(selected.get());
        if (!selected.get() || (api.stringLength(selected.get()) && id.empty()))
            throw std::runtime_error("Invalid decoration selection");
        Root profile(invoke(profileGet, nullptr));
        if (!profile.get()) throw std::runtime_error("Account profile unavailable");
        const auto* hash = checked(api.objectClass(profile.get()), "GetHashCode", "System.Int32");
        const auto before = scalar<int32_t>(invoke(hash, profile.get()));
        bool owned = id.empty();
        if (!id.empty()) {
            void* args[] = {selected.get()};
            Root collection(snapshot(categories[3]));
            const auto* find = checked(api.objectClass(collection.get()), "FindById",
                "Solis.Common.Proto.Transaction.UserDecoration", {"System.String"});
            Root row(invoke(find, collection.get(), args));
            if (!row.get()) throw std::runtime_error("Decoration not in preview list");
            if (!loadedDecoration(presenter, model, row.get())) {
                toast("请等待所选装饰下载并显示完成");
                __android_log_print(ANDROID_LOG_WARN, Tag, "Local decoration deferred: texture not ready");
                return {};
            }
            Root real(invoke(decorationGet, nullptr));
            owned = invoke(find, real.get(), args) != nullptr;
        }
        setPrivateDecoration(model, selected.get());
        if (!decorationOverride.set(accountEpoch, id)) throw std::runtime_error("Invalid decoration identity");
        categories[3].error.clear();
        const bool unchanged = before == scalar<int32_t>(invoke(hash, profile.get()));
        __android_log_print(ANDROID_LOG_INFO, Tag,
            "Local decoration applied: owned=%d, accountUnchanged=%d; profile save bypassed", owned, unchanged);
        toast("已应用本机名片装饰，账号名片保持不变");
    } catch (const std::exception& error) {
        categories[3].error = error.what();
        __android_log_print(ANDROID_LOG_WARN, Tag, "Local decoration rejected: %s", error.what());
        toast("装饰预览未能应用，账号名片保持不变");
    }
    return {};
}

void profileCardData(void* self, void* user, void* profile, void* userId, void* rank,
                     void* types, bool full, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try {
        if (auto id = decorationOverride.get(accountEpoch, policy.enabled.load() & Decorations)) {
            Root own(invoke(profileGet, nullptr));
            if (profile && own.get() == profile) {
                Root clone(invoke(profileClone, profile));
                if (!clone.get() || clone.get() == profile) throw std::runtime_error("Cannot isolate profile renderer");
                setId(profileDecorationSetter, clone.get(), *id);
                originalProfileCardData(self, user, clone.get(), userId, rank, types, full, method);
                return;
            }
        }
    } catch (const std::exception& error) { categories[3].error = error.what(); }
    originalProfileCardData(self, user, profile, userId, rank, types, full, method);
}

const Method* decorationContinuation(void* owner) {
    void* iterator = nullptr;
    void* match = nullptr;
    while (void* nested = api.nestedTypes(owner, &iterator)) {
        const char* name = api.className(nested);
        if (!name || std::string(name).rfind("<SetupAsync>d__", 0) != 0) continue;
        if (match) return nullptr;
        match = nested;
    }
    uint32_t alignment = 0;
    if (!match || !api.isValueType(match)) return nullptr;
    const auto size = api.valueSize(match, &alignment);
    if (size <= 0) return nullptr;
    auto* field = api.field(match, "<>4__this");
    if (!field || typeName(api.fieldType(field)) != "Solis.OutGame.ProfileDecorationActionSheetPresenter") return nullptr;
    const auto offset = api.fieldOffset(field);
    if (offset < 2 * sizeof(void*) || offset - 2 * sizeof(void*) + sizeof(void*) > static_cast<size_t>(size)) return nullptr;
    decorationStateOwnerOffset = offset - 2 * sizeof(void*);
    return checked(match, "MoveNext", "System.Void");
}

bool installDecorations() {
    auto& error = categories[3].error;
    error = "Profile decoration layout unavailable";
    auto* model = gameClass("Solis.OutGame", "ProfileSettingScreenModel");
    auto* presenter = gameClass("Solis.OutGame", "ProfileSettingScreenPresenter");
    auto* sheet = gameClass("Solis.OutGame", "ProfileDecorationActionSheetPresenter");
    auto* collection = gameClass("Solis.Common.User", "UserDecorationCollection");
    auto* profile = gameClass("Solis.Common.Proto.Transaction", "UserProfile");
    auto* manager = gameClass("Solis.Common.User", "UserDataManager");
    if (!model || !presenter || !sheet || !collection || !profile || !manager) return false;
    const char* string = "System.String";
    const char* boolean = "System.Boolean";
    const char* userProfile = "Solis.Common.Proto.Transaction.UserProfile";
    const char* ct = "System.Threading.CancellationToken";
    const char* task = "Cysharp.Threading.Tasks.UniTask";
    error = "Profile decoration async layout unsupported";
    if (!layout(assemblyClass("UniTask.dll", "Cysharp.Threading.Tasks", "UniTask"), sizeof(Task),
                {{"source", 0}, {"token", offsetof(Task, token)}}) ||
        !layout(assemblyClass("mscorlib.dll", "System.Threading", "CancellationToken"), sizeof(Cancellation), {{"_source", 0}})) return false;
    profilePresenterClass = presenter;
    profileModelField = api.field(presenter, "_model");
    profileViewField = api.field(presenter, "_view");
    beforeDecorationField = api.field(model, "<BeforeDecorationId>k__BackingField");
    error = "Profile decoration model fields unavailable";
    if (!profileModelField || typeName(api.fieldType(profileModelField)) != "Solis.OutGame.ProfileSettingScreenModel" ||
        !beforeDecorationField || typeName(api.fieldType(beforeDecorationField)) != string ||
        !profileViewField || typeName(api.fieldType(profileViewField)) != "Solis.OutGame.ProfileSettingScreenView") return false;
    profileGet = checked(manager, "get_Profile", userProfile, {}, true);
    decorationGet = checked(manager, "get_Decorations", "Solis.Common.User.UserDecorationCollection", {}, true);
    profileClone = checked(profile, "Clone", userProfile);
    profileDecorationSetter = checked(profile, "set_DecorationId", "System.Void", {string});
    selectedDecoration = checked(model, "get_SelectDecorationId", string);
    selectDecoration = checked(model, "set_SelectDecorationId", "System.Void", {string});
    profileIsPhoto = checked(model, "get_IsPhotoSelectMode", boolean);
    profileCheckBackground = checked(model, "CheckUpdateId", boolean, {boolean});
    const Method* continuation = decorationContinuation(sheet);
    if (!profileGet || !decorationGet || !profileClone || !profileDecorationSetter || !selectedDecoration ||
        !selectDecoration || !profileIsPhoto || !profileCheckBackground || !continuation || !toastMethod) {
        error = "Profile decoration accessors unavailable";
        __android_log_print(ANDROID_LOG_WARN, Tag,
            "Decoration accessors: profile=%d collection=%d clone=%d setter=%d selection=%d select=%d photo=%d background=%d continuation=%d",
            bool(profileGet), bool(decorationGet), bool(profileClone), bool(profileDecorationSetter),
            bool(selectedDecoration), bool(selectDecoration), bool(profileIsPhoto),
            bool(profileCheckBackground), bool(continuation));
        return false;
    }
    error = "Profile decoration hooks unavailable";
    bool ok = hook(checked(presenter, "ExecuteBackgroundAsync", task, {ct}), saveDecoration, originalProfileSave);
    ok &= hook(checked(collection, "RecordCurrentDecoration", boolean), decorationRecord, originalDecorationRecord);
    ok &= hook(checked(collection, "IsNew", boolean, {string}), decorationNew, originalDecorationNew);
    ok &= hook(checked(collection, "HasNewDecoration", boolean), decorationHasNew, originalDecorationHasNew);
    ok &= hook(decorationGet, decorationCollection, originalDecorationCollection);
    ok &= hook(checked(presenter, "DecorationButtonInitialize", "System.Void"), decorationButton, originalDecorationButton);
    ok &= hook(continuation, decorationMoveNext, originalDecorationMoveNext);
    ok &= hook(checked(sheet, "SetupAsync", task, {string, ct}), decorationSetup, originalDecorationSetup);
    ok &= hook(checked(gameClass("Solis.OutGame", "ProfileCard"), "SetUserData", "System.Void",
        {"Solis.Common.Proto.Transaction.User", userProfile, string, "Solis.Common.Proto.Master.HierarchyDetailGrade",
         "Solis.Common.Proto.Transaction.ProfileTypes", boolean}), profileCardData, originalProfileCardData);
    if (!ok) return false;
    ok = hook(checked(model, ".ctor", "System.Void", {boolean}), profileCtor, originalProfileCtor);
    if (ok) error.clear();
    __android_log_print(ANDROID_LOG_INFO, Tag, "Profile decoration adapter available: %d", ok);
    return ok;
}
