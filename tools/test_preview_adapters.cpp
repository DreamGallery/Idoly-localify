// Exercise the actual adapter's failure paths without loading Android/Unity.
#include "runtime/preview_access.cpp"
#include <cassert>
#include <future>

int __android_log_print(int, const char*, const char*, ...) { return 0; }
void* shadowhook_hook_func_addr(void*, void*, void**) { assert(false); return nullptr; }

using namespace localify::preview_access;
namespace {
int sentinel;
void* assembly = &sentinel;
std::promise<void> resolving, releaseResolver, reading;
std::shared_future<void> allowResolver;
void* unavailable(const char*) {
    resolving.set_value();
    allowResolver.wait();
    return nullptr;
}

void verifyEditorWriteBarriers() {
    // Exercise the production hook entry points, not just their policy helpers.
    // Every original callback below stands for an account-writing operation.
    static int presenter, wardrobeModelObject, profileModelObject, fields[2];
    static std::array<int, 7> writes{};
    static int initializations = 0;
    static bool wardrobeAlive = true, profileAlive = true;
    const auto previousApi = api;
    presenterModelField = &fields[0];
    profileModelField = &fields[1];
    toastMethod = nullptr;
    api.fieldGet = [](void* object, void* field, void* result) {
        assert(object == &presenter);
        assert(field == &fields[0] || field == &fields[1]);
        *static_cast<void**>(result) = field == &fields[0]
            ? static_cast<void*>(&wardrobeModelObject) : &profileModelObject;
    };
    api.target = [](uint32_t handle) -> void* {
        assert(handle == 71 || handle == 72);
        return handle == 71 ? (wardrobeAlive ? &wardrobeModelObject : nullptr)
                            : (profileAlive ? &profileModelObject : nullptr);
    };
    api.release = [](uint32_t handle) { assert(handle == 71 || handle == 72); };
    // Stale or unisolated editors must stop before reading/changing any model.
    api.invoke = [](const Method*, void*, void**, void**) -> void* {
        assert(false); return nullptr;
    };
    originalCheckCostume = [](void*, void*, const Method*) { ++writes[0]; };
    originalCheckHair = [](void*, void*, const Method*) { ++writes[1]; };
    originalCheckBulk = [](void*, Cancellation, const Method*) { ++writes[2]; return Task{}; };
    originalSave = [](void*, Cancellation, const Method*) { ++writes[3]; return Task{}; };
    originalSaveIds = [](void*, void*, void*, Cancellation, const Method*) { ++writes[4]; return Task{}; };
    originalProfileSave = [](void*, Cancellation, const Method*) { ++writes[5]; return Task{}; };
    originalDecorationRecord = [](void*, const Method*) { ++writes[6]; return true; };

    auto check = [&](uint32_t enabled, bool forwardWardrobe, bool forwardProfile) {
        policy.enabled = enabled;
        writes.fill(0);
        checkCostume(&presenter, nullptr, nullptr);
        checkHair(&presenter, nullptr, nullptr);
        checkBulk(&presenter, {}, nullptr);
        saveLocal(&presenter, {}, nullptr);
        saveLocalIds(&presenter, nullptr, nullptr, {}, nullptr);
        saveDecoration(&presenter, {}, nullptr);
        assert(decorationRecord(&sentinel, nullptr) == forwardProfile);
        for (size_t i = 0; i < 5; ++i) assert(writes[i] == int(forwardWardrobe));
        assert(writes[5] == int(forwardProfile));
        assert(writes[6] == int(forwardProfile));
    };
    // Ordinary pages and unrelated toggles keep the original behavior.
    check(0, true, true);
    check(Poses, true, true);
    // Late enable or isolation failure: no page record exists yet.
    check(Costumes, false, true);
    check(Hair, false, true);
    check(Decorations, true, false);
    check(WardrobeCategories | Decorations, false, false);

    auto wardrobe = std::make_unique<WardrobeSession>();
    wardrobe->model = 71;
    wardrobe->categories = WardrobeCategories;
    wardrobe->generation = policy.generation.load();
    wardrobe->revision = appearances.revision();
    wardrobe->account = accountEpoch;
    wardrobe->failed = true;
    auto decoration = std::make_unique<DecorationPage>();
    decoration->model = 72;
    decoration->generation = policy.generation.load();
    decoration->revision = decorationOverride.revision();
    decoration->account = accountEpoch;
    decoration->failed = true;
    wardrobes.push_back(std::move(wardrobe));
    decorationPages.push_back(std::move(decoration));
    check(WardrobeCategories | Decorations, false, false);
    wardrobes.front()->failed = false;
    decorationPages.front()->failed = false;
    // Turning off all switches cannot turn an old preview into a real save.
    check(0, false, false);
    ++policy.generation;
    check(WardrobeCategories | Decorations, false, false);
    wardrobes.front()->generation = policy.generation.load();
    decorationPages.front()->generation = policy.generation.load();
    clearAppearances();
    clearDecorationOverride();
    check(WardrobeCategories | Decorations, false, false);

    wardrobes.front()->revision = appearances.revision();
    decorationPages.front()->revision = decorationOverride.revision();
    assert(appearances.put(std::to_string(accountEpoch), "idol", false,
                          {"costume", "hair", Costumes}));
    assert(decorationOverride.set(accountEpoch, "decoration"));
    const auto previousAccount = accountEpoch;
    originalUserInitialization = [](void*, void*, const Method*) {
        ++initializations;
        assert(!appearances.size());
        assert(!decorationOverride.get(accountEpoch, true));
    };
    initializeUser(nullptr, nullptr, nullptr);
    assert(initializations == 1 && accountEpoch == previousAccount + 1);
    // Even if revisions match, the old account's editor is no longer current.
    wardrobes.front()->revision = appearances.revision();
    decorationPages.front()->revision = decorationOverride.revision();
    check(WardrobeCategories | Decorations, false, false);
    check(0, false, false);
    // Collected old models do not block unrelated new, ordinary editors.
    wardrobeAlive = profileAlive = false;
    check(0, true, true);

    wardrobes.clear();
    decorationPages.clear();
    api = previousApi;
    policy.enabled = 0;
    presenterModelField = profileModelField = nullptr;
    originalCheckCostume = originalCheckHair = nullptr;
    originalCheckBulk = originalSave = originalProfileSave = nullptr;
    originalSaveIds = nullptr;
    originalDecorationRecord = nullptr;
    originalUserInitialization = nullptr;
}

void verifyPoseSceneReads() {
    static int ownedInventory, detachedInventory, character, scene, expression;
    static Method callMethod{};
    const auto previousApi = api;
    auto& poses = categories[2];
    policy.ready = Poses;
    policy.enabled = Poses;
    poses.snapshot = 7;
    poses.generation = policy.generation.load();
    static void* detached = &detachedInventory;
    api.target = [](uint32_t handle) -> void* { assert(handle == 7); return detached; };
    static void* expectedInventory;
    static void* expectedCharacter = &character;
    static bool expectedReleased;
    static void* owned = &ownedInventory;
    static void* expectedScene = &scene;
    static void* expectedExpression = &expression;
    static bool expectedPreview;
    originalPoses = [](void* inventory, void* actor, bool released, const Method* method) -> void* {
        assert(inventory == expectedInventory);
        assert(actor == expectedCharacter);
        assert(released == expectedReleased);
        assert(method == &callMethod);
        return inventory;
    };
    originalCreatePoseScene = [](void* self, void* data, bool preview, const Method* method) {
        assert(self == expectedScene && data == expectedExpression);
        assert(preview == expectedPreview && method == &callMethod);
        assert(poseConstructionScope == Poses);
        readPoses(owned, expectedCharacter, false, method);
        // Released-only reads retain their original constraint in both modes.
        const auto previousReleased = expectedReleased;
        expectedReleased = true;
        readPoses(owned, expectedCharacter, true, method);
        expectedReleased = previousReleased;
        // Other categories remain original even when their toggles are enabled.
        assert(view(categories[0], owned, false) == owned);
        assert(view(categories[1], owned, false) == owned);
        assert(view(categories[3], owned, false) == owned);
    };
    auto check = [&](bool preview, bool enabled, bool ready) {
        policy.enabled = WardrobeCategories | Decorations | (enabled ? Poses : 0);
        policy.ready = WardrobeCategories | Decorations | (ready ? Poses : 0);
        expectedPreview = preview;
        expectedInventory = enabled && ready ? detached : owned;
        expectedReleased = expectedInventory == detached;
        createPoseScene(&scene, &expression, preview, &callMethod);
        assert(poseConstructionScope == 0);
    };
    for (bool preview : {false, true})
        for (bool enabled : {false, true})
            for (bool ready : {false, true}) check(preview, enabled, ready);
    // Nested shooting uses the same policy and restores the enclosing scope.
    policy.ready = Poses;
    policy.enabled = Poses;
    {
        Scope outer(poseConstructionScope, Poses);
        expectedInventory = detached;
        expectedReleased = true;
        expectedPreview = false;
        readPoses(owned, expectedCharacter, false, &callMethod);
        createPoseScene(&scene, &expression, false, &callMethod);
        assert(poseConstructionScope == Poses);
        // An active outer scope cannot bypass a subsequently disabled toggle.
        policy.enabled = 0;
        expectedInventory = owned;
        expectedReleased = false;
        createPoseScene(&scene, &expression, false, &callMethod);
        assert(poseConstructionScope == Poses);
        policy.enabled = Poses;
        expectedInventory = detached;
        expectedReleased = true;
        readPoses(owned, expectedCharacter, false, &callMethod);
    }
    assert(poseConstructionScope == 0);
    // Actor-construction scope must unwind for either scene mode on failure.
    originalCreatePoseScene = [](void*, void*, bool, const Method*) { throw std::runtime_error("builder failed"); };
    for (bool preview : {false, true}) {
        try { createPoseScene(&scene, &expression, preview, &callMethod); assert(false); }
        catch (const std::runtime_error&) {}
        assert(poseConstructionScope == 0);
        Scope outer(poseConstructionScope, Poses);
        try { createPoseScene(&scene, &expression, preview, &callMethod); assert(false); }
        catch (const std::runtime_error&) {}
        assert(poseConstructionScope == Poses);
    }
    assert(poseConstructionScope == 0);
    poses.snapshot = 0;
    policy.ready = policy.enabled = 0;
    originalCreatePoseScene = nullptr;
    originalPoses = nullptr;
    api = previousApi;
}
}

int main() {
    verifyEditorWriteBarriers();
    verifyPoseSceneReads();
    // No API dereferences when a required game class disappears.
    api.domain = []() -> void* { return &sentinel; };
    api.assemblies = [](void*, size_t* count) { *count=1; return &assembly; };
    api.image = [](void* value) { return value; };
    api.imageName = [](void*) { return "Assembly-CSharp.dll"; };
    api.findClass = [](void*, const char*, const char*) -> void* { return nullptr; };
    api.field = [](void*, const char*) -> void* { assert(false); return nullptr; };
    assert(!installWardrobe());
    assert(!wardrobeReady);
    assert(!setEnabled(Costumes, true));
    // The domain can be absent even when an export is resolvable.
    api.domain = []() -> void* { return nullptr; };
    api.assemblies = [](void*, size_t*) -> void** { assert(false); return nullptr; };
    assert(!gameClass("Solis.OutGame", "CharacterCostumeScreenModel"));
    assert(!requiredCostumeHair(nullptr));

    // JNI settings cannot observe a partly populated installation. A missing
    // export becomes a disabled optional feature, not an escaping exception.
    allowResolver=releaseResolver.get_future().share();
    auto initialize=std::async(std::launch::async, [] { install(unavailable); });
    resolving.get_future().wait();
    auto reader=std::async(std::launch::async, [] { reading.set_value(); return status(); });
    reading.get_future().wait();
    assert(reader.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    releaseResolver.set_value();
    initialize.get();
    auto result=nlohmann::json::parse(reader.get());
    assert(!result.at("installation_error").get<std::string>().empty());
    for (const auto& category:result.at("categories")) {
        assert(category.at("available") == false);
        assert(category.at("enabled") == false);
    }
    assert(!setEnabled(Decorations, true));
}
