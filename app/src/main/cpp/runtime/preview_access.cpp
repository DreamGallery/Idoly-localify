#include "preview_access.h"
#include "preview_access_policy.h"
#include "appearance_overrides.h"
#include "profile_decoration_state.h"
#include "inline_hook.h"
#include "json.hpp"
#include <android/log.h>
#include <shadowhook.h>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cstddef>
#include <set>
#include <algorithm>

namespace localify::preview_access {
namespace {
constexpr auto Tag = "IdolyPreview";
struct Method { void* pointer; };
struct Api {
    void* (*domain)();
    void** (*assemblies)(void*, size_t*);
    void* (*image)(void*);
    const char* (*imageName)(void*);
    void* (*findClass)(void*, const char*, const char*);
    const Method* (*method)(void*, const char*, int);
    const void* (*returnType)(const Method*);
    const void* (*paramType)(const Method*, uint32_t);
    uint32_t (*paramCount)(const Method*);
    uint32_t (*flags)(const Method*, uint32_t*);
    char* (*typeName)(const void*);
    void (*free)(void*);
    void* (*objectClass)(void*);
    void* (*classFromType)(const void*);
    bool (*isValueType)(void*);
    void* (*newObject)(void*);
    void* (*invoke)(const Method*, void*, void**, void**);
    void* (*unbox)(void*);
    uint32_t (*retain)(void*, bool);
    void* (*target)(uint32_t);
    void (*release)(uint32_t);
    uint32_t (*weak)(void*, bool);
    void* (*field)(void*, const char*);
    const void* (*fieldType)(void*);
    size_t (*fieldOffset)(void*);
    void (*fieldGet)(void*, void*, void*);
    void (*fieldSet)(void*, void*, void*);
    void* (*nestedTypes)(void*, void**);
    const char* (*className)(void*);
    void (*staticGet)(void*, void*);
    int32_t (*valueSize)(void*, uint32_t*);
    bool (*isEnum)(void*);
    int32_t (*stringLength)(void*);
    const char16_t* (*stringChars)(void*);
    void* (*newString)(const char*);
} api{};
Policy policy;
std::recursive_mutex mutex;
thread_local uint32_t scope = 0;
thread_local uint32_t wardrobeScope = 0;
// The 3D scene outlives the OutGame photography window. Both viewing and
// shooting build poses in this synchronous scope, without expanding other reads.
thread_local uint32_t poseConstructionScope = 0;
thread_local void* wardrobeModel = nullptr;
struct WardrobeContext {
    Scope active;
    void* previous;
    WardrobeContext(void* model, uint32_t mask) : active(wardrobeScope, mask), previous(wardrobeModel) { wardrobeModel = model; }
    ~WardrobeContext() { wardrobeModel = previous; }
};
std::atomic<bool> installed{false};
std::atomic<bool> wardrobeReady{false};
AppearanceOverrides appearances;
uint64_t accountEpoch = 1;
std::string wardrobeError;
std::string installationError;
void clearDecorationOverride();
const Method* sceneInstance = nullptr;
const Method* topScreen = nullptr;
const Method* screenParent = nullptr;
const Method* toastMethod = nullptr;
void* photographyWindowClass = nullptr;

struct Root {
    uint32_t handle = 0;
    explicit Root(void* object) : handle(object ? api.retain(object, false) : 0) {
        if (object && !handle) throw std::runtime_error("Cannot retain preview object");
    }
    ~Root() { if (handle) api.release(handle); }
    void* get() const { return handle ? api.target(handle) : nullptr; }
    Root(const Root&) = delete;
    Root& operator=(const Root&) = delete;
};

struct CategoryState {
    uint32_t bit;
    const char* name;
    const char* master;
    const char* collection;
    const char* idSetter;
    const char* releaseGetter;
    uint32_t snapshot = 0;
    uint64_t generation = 0;
    int count = 0;
    uint64_t uses = 0;
    std::string error;
};
std::array<CategoryState, 4> categories{{
    {Costumes, "costumes", "Costume", "UserCostumeCollection", "set_CostumeId", "get_ReleaseDate"},
    {Hair, "hair", "Hair", "UserHairCollection", "set_HairId", "get_ReleaseTime"},
    {Poses, "poses", "PhotoPose", "UserPhotoPoseCollection", "set_PhotoPoseId", nullptr},
    {Decorations, "decorations", "Decoration", "UserDecorationCollection", "set_DecorationId", nullptr},
}};

struct WardrobeSession {
    uint32_t model = 0, character = 0, categories = 0;
    uint32_t requiredHair = 0;
    std::string requiredHairId;
    uint64_t generation = 0, revision = 0, account = 0;
    bool failed = false;
    ~WardrobeSession() {
        if (model) api.release(model);
        if (character) api.release(character);
        if (requiredHair) api.release(requiredHair);
    }
};
std::vector<std::unique_ptr<WardrobeSession>> wardrobes;

void* assemblyClass(const char* assemblyName, const char* ns, const char* name) {
    void* domain = api.domain();
    if (!domain) return nullptr;
    size_t count = 0;
    void** assemblies = api.assemblies(domain, &count);
    for (size_t i = 0; assemblies && i < count; ++i) {
        void* image = api.image(assemblies[i]);
        const char* imageName = api.imageName(image);
        if (imageName && std::strcmp(imageName, assemblyName) == 0)
            return api.findClass(image, ns, name);
    }
    return nullptr;
}

void* gameClass(const char* ns, const char* name) {
    return assemblyClass("Assembly-CSharp.dll", ns, name);
}

std::string typeName(const void* type) {
    char* raw = type ? api.typeName(type) : nullptr;
    std::string result = raw ? raw : "";
    if (raw) api.free(raw);
    return result;
}

const Method* checked(void* klass, const char* name, const char* result,
                      std::initializer_list<const char*> params = {}, bool isStatic = false) {
    const auto* method = klass ? api.method(klass, name, static_cast<int>(params.size())) : nullptr;
    if (!method || !method->pointer || api.paramCount(method) != params.size()) return nullptr;
    uint32_t implementation = 0;
    if (((api.flags(method, &implementation) & 0x10u) != 0) != isStatic) return nullptr;
    if (result && typeName(api.returnType(method)) != result) return nullptr;
    if (!result) {
        void* resultClass = api.classFromType(api.returnType(method));
        if (!resultClass || api.isValueType(resultClass)) return nullptr;
    }
    size_t index = 0;
    for (const auto* expected : params) {
        auto actual = typeName(api.paramType(method, index++));
        if (!expected) {
            // The only wildcard is the known managed reference delegate used
            // by the costume picker. Value-type arguments must never use it.
            if (actual.rfind("System.Func<", 0) != 0 && actual.rfind("System.Func`", 0) != 0)
                return nullptr;
        } else if (actual != expected) return nullptr;
    }
    return method;
}

void* invoke(const Method* method, void* object, void** args = nullptr) {
    if (!method) throw std::runtime_error("Preview method unavailable");
    void* exception = nullptr;
    void* result = api.invoke(method, object, args, &exception);
    if (exception) throw std::runtime_error("Preview read failed");
    return result;
}

template <typename T> T scalar(void* box) {
    void* value = box ? api.unbox(box) : nullptr;
    if (!value) throw std::runtime_error("Invalid preview value");
    return *static_cast<T*>(value);
}

void* construct(void* klass) {
    const auto* constructor = checked(klass, ".ctor", "System.Void");
    if (!constructor) throw std::runtime_error("Preview constructor unavailable");
    void* object = api.newObject(klass);
    if (!object) throw std::runtime_error("Cannot allocate preview object");
    Root root(object);
    invoke(constructor, object);
    return object;
}

// The picker is also used by persisted wardrobe/contest flows. Check the
// current typed screen parent on each read, including async initialization;
// never infer this boundary from a text label, an object name, or a stale flag.
bool inPhotographyWindow() {
    if (!sceneInstance || !topScreen || !screenParent || !photographyWindowClass) return false;
    try {
        Root scene(invoke(sceneInstance, nullptr));
        if (!scene.get()) return false;
        Root screen(invoke(topScreen, scene.get()));
        if (!screen.get()) return false;
        Root parent(invoke(screenParent, screen.get()));
        return parent.get() && api.objectClass(parent.get()) == photographyWindowClass;
    } catch (const std::exception&) {
        return false;
    }
}

// Build an unrelated collection from static MasterDB rows. Add/set methods are
// invoked ONLY on newly allocated objects; the original inventory is not even
// passed into this routine. Do not replace this with UserDataManager updates.
void* snapshot(CategoryState& state) {
    const auto generation = policy.generation.load();
    if (state.snapshot && state.generation != generation) {
        api.release(state.snapshot);
        state.snapshot = 0;
        state.count = 0;
    }
    if (state.snapshot) return api.target(state.snapshot);
    void* managerClass = gameClass("Solis.Common.Master", "MasterManager");
    std::string masterClassName = std::string(state.master) + "Master";
    std::string masterType = "Solis.Common.Master." + masterClassName;
    auto* getter = checked(managerClass, ("get_" + masterClassName).c_str(), masterType.c_str(), {}, true);
    Root master(invoke(getter, nullptr));
    if (!master.get()) throw std::runtime_error("Master table unavailable");
    std::string sortType = std::string("Solis.Common.Master.") + state.master + "SortType";
    auto* all = checked(api.objectClass(master.get()), "GetAllWithSortByKey", nullptr, {sortType.c_str()});
    int32_t sort = 0;
    void* allArgs[] = {&sort};
    Root rows(invoke(all, master.get(), allArgs));
    if (!rows.get()) throw std::runtime_error("Master rows unavailable");
    auto* rowClass = api.objectClass(rows.get());
    auto* countMethod = checked(rowClass, "get_Count", "System.Int32");
    std::string rowType = std::string("Solis.Common.Proto.Master.") + state.master;
    auto* itemMethod = checked(rowClass, "get_Item", rowType.c_str(), {"System.Int32"});
    const auto count = scalar<int32_t>(invoke(countMethod, rows.get()));
    if (count < 0 || count > 10000 || !itemMethod) throw std::runtime_error("Unexpected master list");
    void* collectionClass = gameClass("Solis.Common.User", state.collection);
    std::string userType = std::string("Solis.Common.Proto.Transaction.User") + state.master;
    void* userClass = gameClass("Solis.Common.Proto.Transaction", ("User" + std::string(state.master)).c_str());
    auto* add = checked(collectionClass, "Add", "System.Void", {userType.c_str()});
    auto* setId = checked(userClass, state.idSetter, "System.Void", {"System.String"});
    const bool hasChecked = state.bit == Costumes || state.bit == Hair;
    auto* setChecked = hasChecked ? checked(userClass, "set_Checked", "System.Void", {"System.Boolean"}) : nullptr;
    if (!add || !setId || (hasChecked && !setChecked))
        throw std::runtime_error("Preview collection shape changed");
    Root collection(construct(collectionClass));
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int visible = 0;
    for (int32_t i = 0; i < count; ++i) {
        void* itemArgs[] = {&i};
        Root row(invoke(itemMethod, rows.get(), itemArgs));
        if (!row.get()) throw std::runtime_error("Invalid master row");
        void* klass = api.objectClass(row.get());
        if (state.bit == Poses) {
            if (!scalar<bool>(invoke(checked(klass, "IsReleased", "System.Boolean"), row.get()))) continue;
        } else if (state.bit != Decorations) {
            if (scalar<bool>(invoke(checked(klass, "get_IsDisableAdmin", "System.Boolean"), row.get()))) continue;
            if (!released(scalar<int64_t>(invoke(checked(klass, state.releaseGetter, "System.Int64"), row.get())), now)) continue;
        } else {
            // Decoration has no release timestamp. Only include concrete
            // image-backed MasterDB entries; never manufacture resource IDs.
            Root asset(invoke(checked(klass, "get_AssetId", "System.String"), row.get()));
            if (!asset.get() || api.stringLength(asset.get()) == 0) continue;
        }
        Root id(invoke(checked(klass, "get_Id", "System.String"), row.get()));
        if (!id.get()) throw std::runtime_error("Missing resource id");
        Root data(construct(userClass));
        void* idArgs[] = {id.get()};
        invoke(setId, data.get(), idArgs);
        if (setChecked) {
            bool yes = true;
            void* checkedArgs[] = {&yes};
            invoke(setChecked, data.get(), checkedArgs);
        }
        void* addArgs[] = {data.get()};
        invoke(add, collection.get(), addArgs);
        ++visible;
    }
    if (!visible) throw std::runtime_error("No released preview resources");
    state.snapshot = api.retain(collection.get(), false);
    if (!state.snapshot) throw std::runtime_error("Cannot retain resource view");
    state.generation = generation;
    state.count = visible;
    state.error.clear();
    __android_log_print(ANDROID_LOG_INFO, Tag, "Detached %s view ready: %d rows", state.name, visible);
    return api.target(state.snapshot);
}

void* view(CategoryState& state, void* original, bool scoped) {
    if (!original) return original;
    const bool inWardrobe = wardrobeReady.load() && (wardrobeScope & state.bit);
    if (poseConstructionScope & Poses) {
        if (state.bit != Poses || !policy.allows(Poses, 0, false)) return original;
    } else if (!inWardrobe && (!policy.allows(state.bit, scope, scoped) || !inPhotographyWindow())) return original;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try {
        void* result = snapshot(state);
        if (result) { ++state.uses; return result; }
    } catch (const std::exception& error) {
        if (state.error != error.what())
            __android_log_print(ANDROID_LOG_WARN, Tag, "%s: %s; original inventory retained", state.name, error.what());
        state.error = error.what();
    }
    return original;
}

using ReadOne = void* (*)(void*, void*, const Method*);
using ReadHairs = void* (*)(void*, void*, void*, bool, const Method*);
using ReadPoses = void* (*)(void*, void*, bool, const Method*);
using ReadBool = bool (*)(void*, void*, const Method*);
using SetOne = void (*)(void*, void*, const Method*);
using SetTwo = void (*)(void*, void*, void*, const Method*);
using CreatePoseScene = void (*)(void*, void*, bool, const Method*);
ReadOne originalCostumes = nullptr, originalOrnaments = nullptr;
ReadHairs originalHairs = nullptr;
ReadPoses originalPoses = nullptr;
ReadBool originalHasOrnaments = nullptr;
SetOne originalCostumeScope = nullptr, originalHairScope = nullptr;
SetTwo originalOrnamentScope = nullptr;
CreatePoseScene originalCreatePoseScene = nullptr;

void* readCostumes(void* self, void* character, const Method* method) {
    return originalCostumes(view(categories[0], self, true), character, method);
}
void* readHairs(void* self, void* character, void* costume, bool baseOnly, const Method* method) {
    return originalHairs(view(categories[1], self, true), character, costume, baseOnly, method);
}
void* readOrnaments(void* self, void* hair, const Method* method) {
    return originalOrnaments(view(categories[1], self, true), hair, method);
}
bool hasOrnaments(void* self, void* character, const Method* method) {
    // This read-only getter gates the async picker initialization, before the
    // synchronous list-building scopes below are entered.
    return originalHasOrnaments(view(categories[1], self, false), character, method);
}
void* readPoses(void* self, void* character, bool checkReleased, const Method* method) {
    void* source = view(categories[2], self, false);
    return originalPoses(source, character, source != self || checkReleased, method);
}
void createPoseScene(void* self, void* expression, bool preview, const Method* method) {
    Scope active(poseConstructionScope, Poses);
    // The original builder retains costume/pose compatibility filters and
    // its own resource loading. No original inventory or transition is edited.
    originalCreatePoseScene(self, expression, preview, method);
}
void costumeScope(void* self, void* filter, const Method* method) {
    Scope active(scope, Costumes);
    originalCostumeScope(self, filter, method);
}
void hairScope(void* self, void* hair, const Method* method) {
    Scope active(scope, Hair);
    originalHairScope(self, hair, method);
}
void ornamentScope(void* self, void* ornament, void* base, const Method* method) {
    Scope active(scope, Hair);
    originalOrnamentScope(self, ornament, base, method);
}

template <typename F> bool hook(const Method* method, F replacement, F& original) {
    if (!method) return false;
    return installInlineHook(method->pointer, reinterpret_cast<void*>(replacement),
                             original, shadowhook_hook_func_addr);
}

// IL2CPP's non-generic UniTask is a source pointer followed by a short token.
// Its layout and CancellationToken are checked before installing these hooks.
struct Task { void* source; int16_t token; };
struct Cancellation { void* source; };
struct HomeActor { void* character; void* costume; };
using Read = void* (*)(void*, const Method*);
using Void = void (*)(void*, const Method*);
using Save = Task (*)(void*, Cancellation, const Method*);
using SaveIds = Task (*)(void*, void*, void*, Cancellation, const Method*);
using Actor = void* (*)(void*, bool, const Method*);
using HomeDescriptor = void* (*)(HomeActor, void*, const Method*);
using StaticRead = void* (*)(const Method*);
SetOne originalWardrobeCtor = nullptr, originalUserSetter = nullptr;
SetOne originalCheckCostume = nullptr, originalCheckHair = nullptr;
SetOne originalModelOrnaments = nullptr, originalUserInitialization = nullptr;
Void originalModelHairs = nullptr;
Save originalSave = nullptr, originalCheckBulk = nullptr;
SaveIds originalSaveIds = nullptr;
Actor originalActor = nullptr;
HomeDescriptor originalHomeDescriptor = nullptr;
StaticRead originalCostumeCollection = nullptr, originalHairCollection = nullptr;
const Method *modelCharacter = nullptr, *userCharacters = nullptr, *findCharacter = nullptr;
const Method *userClone = nullptr, *characterId = nullptr, *modeGetter = nullptr;
const Method *selectedCostume = nullptr, *selectedHair = nullptr;
const Method *costumeId = nullptr, *hairId = nullptr, *userCostume = nullptr, *userHair = nullptr;
const Method *updateHairs = nullptr, *forcePrivateCharacter = nullptr;
std::array<const Method*, 2> setCostumeIds{}, setHairIds{}, selectCostumes{}, selectHairs{};
std::array<const Method*, 2> costumeGetters{}, hairGetters{};
void* presenterModelField = nullptr;
void* privateCharacterField = nullptr;

std::string idText(void* value) {
    // Resource identifiers contain ASCII only. Reject unexpected shapes instead
    // of truncating them or accepting arbitrary display text as an identity.
    if (!value) return {};
    int32_t length = api.stringLength(value);
    if (length <= 0 || length > 256) return {};
    const char16_t* chars = api.stringChars(value);
    if (!chars) return {};
    std::string result;
    for (int32_t i = 0; i < length; ++i) {
        if (chars[i] < 0x21 || chars[i] > 0x7e) return {};
        result += static_cast<char>(chars[i]);
    }
    return result;
}

void toast(const char* text) {
    if (!toastMethod) return;
    try {
        Root message(api.newString(text));
        void* args[] = {message.get()};
        invoke(toastMethod, nullptr, args);
    } catch (const std::exception&) {}
}

WardrobeSession* session(void* model) {
    if (!model) return nullptr;
    for (auto& entry : wardrobes)
        if (api.target(entry->model) == model) return entry.get();
    return nullptr;
}

void* presenterModel(void* presenter) {
    void* model = nullptr;
    if (presenter && presenterModelField) api.fieldGet(presenter, presenterModelField, &model);
    return model;
}

void* referenceField(void* object, const char* name) {
    if (!object) return nullptr;
    void* field = api.field(api.objectClass(object), name);
    void* type = field ? api.classFromType(api.fieldType(field)) : nullptr;
    if (!type || api.isValueType(type)) return nullptr;
    void* value = nullptr;
    api.fieldGet(object, field, &value);
    return value;
}

bool loadedAppearance(void* presenter, void* costume, void* hair) {
    // The original wardrobe loader obtains missing bundles through Octo and
    // builds the complete actor. Check its built resources, not inventory or
    // a non-null MasterDB row: an old actor must not validate a failed download.
    Root environment(referenceField(presenter, "_costumeEnvironment"));
    Root environmentModel(referenceField(environment.get(), "_model"));
    if (!environmentModel.get()) return false;
    Root actor(invoke(checked(api.objectClass(environmentModel.get()), "get_Actor",
                             "Solis.Common.ISolisActorController"), environmentModel.get()));
    if (!actor.get()) return false;
    Root resources(invoke(checked(api.objectClass(actor.get()), "GetBuildResources", nullptr), actor.get()));
    if (!resources.get()) return false;
    auto* enumeratorMethod = api.method(api.objectClass(resources.get()), "GetEnumerator", 0);
    if (!enumeratorMethod || api.paramCount(enumeratorMethod))
        throw std::runtime_error("Loaded actor resource enumeration unavailable");
    Root enumerator(invoke(enumeratorMethod, resources.get()));
    if (!enumerator.get()) return false;
    auto* iteratorClass = api.objectClass(enumerator.get());
    void* iterator = api.isValueType(iteratorClass) ? api.unbox(enumerator.get()) : enumerator.get();
    auto* next = checked(iteratorClass, "MoveNext", "System.Boolean");
    auto* current = api.method(iteratorClass, "get_Current", 0);
    if (!current || api.paramCount(current)) throw std::runtime_error("Loaded resource iterator unavailable");
    std::set<std::string> names;
    int size = 0;
    while (scalar<bool>(invoke(next, iterator))) {
        if (++size > 256) return false;
        Root item(invoke(current, iterator));
        if (!item.get()) continue;
        // The engine may expose a dictionary or a list of built prefabs.
        void* itemClass = api.objectClass(item.get());
        auto* value = api.isValueType(itemClass) ? checked(itemClass, "get_Value", nullptr) : nullptr;
        Root resource(value ? invoke(value, api.unbox(item.get())) : item.get());
        if (resource.get()) names.insert(idText(invoke(checked(api.objectClass(resource.get()),
                                                               "get_name", "System.String"), resource.get())));
    }
    for (const auto& [object, getter] : {std::pair<void*, const char*>{costume, "GetBodyAssetName"},
                                         {hair, "GetFaceAssetName"}, {hair, "GetHairAssetName"}}) {
        auto expected = idText(invoke(checked(api.objectClass(object), getter, "System.String"), object));
        if (expected.empty() || !names.count(expected)) return false;
    }
    return true;
}

// This lookup is against the real current account collection. Render hooks
// never match another player's UserCharacter merely by the same character ID.
void* ownedCharacter(void* id) {
    Root collection(invoke(userCharacters, nullptr));
    if (!collection.get() || !id) return nullptr;
    void* args[] = {id};
    return invoke(findCharacter, collection.get(), args);
}

void setId(const Method* setter, void* object, const std::string& value) {
    Root id(api.newString(value.c_str()));
    void* args[] = {id.get()};
    invoke(setter, object, args);
}

void applyAppearance(void* clone, const std::string& character, bool live) {
    const auto appearance = appearances.get(std::to_string(accountEpoch), character, live, policy.enabled.load());
    if (!appearance) return;
    setId(setCostumeIds[live], clone, appearance->costume);
    setId(setHairIds[live], clone, appearance->hair);
}

void* detachedCharacter(void* source) {
    Root clone(invoke(userClone, source));
    if (!clone.get() || clone.get() == source) throw std::runtime_error("Cannot isolate wardrobe character");
    const auto id = idText(invoke(characterId, source));
    if (id.empty()) throw std::runtime_error("Invalid character identity");
    applyAppearance(clone.get(), id, false);
    applyAppearance(clone.get(), id, true);
    return clone.get();
}

void retainCharacter(WardrobeSession& entry, void* clone) {
    const auto handle = api.retain(clone, false);
    if (!handle) throw std::runtime_error("Cannot retain wardrobe character");
    if (entry.character) api.release(entry.character);
    entry.character = handle;
}

void replacePrivateCharacter(void* model, void* clone) {
    void* rawProperty = nullptr;
    api.fieldGet(model, privateCharacterField, &rawProperty);
    Root property(rawProperty);
    if (!property.get()) throw std::runtime_error("Private wardrobe property unavailable");
    void* args[] = {clone};
    invoke(forcePrivateCharacter, property.get(), args);
    // Protobuf compares records by content. A normal reactive setter discards
    // an identical clone; force replacement is necessary to isolate identity.
    if (invoke(modelCharacter, model) != clone) throw std::runtime_error("Cannot isolate wardrobe property");
}

void setModelCharacter(void* model, void* value, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto* entry = session(model);
    if (!entry || !value) { originalUserSetter(model, value, method); return; }
    try {
        if (entry->character && api.target(entry->character) == value) {
            replacePrivateCharacter(model, value);
            return;
        }
        Root clone(detachedCharacter(value));
        retainCharacter(*entry, clone.get());
        replacePrivateCharacter(model, clone.get());
    } catch (const std::exception& error) {
        entry->failed = true;
        wardrobeError = error.what();
        // Keep the existing private view. Never remove the write barrier after
        // an error once this page has exposed resources outside the inventory.
    }
}

void* userRow(uint32_t category, void* master) {
    if (!master) throw std::runtime_error("Missing appearance master row");
    const char* name = category == Costumes ? "UserCostume" : "UserHair";
    void* klass = gameClass("Solis.Common.Proto.Transaction", name);
    Root row(construct(klass));
    Root id(invoke(checked(api.objectClass(master), "get_Id", "System.String"), master));
    void* args[] = {id.get()};
    invoke(checked(klass, category == Costumes ? "set_CostumeId" : "set_HairId",
                   "System.Void", {"System.String"}), row.get(), args);
    bool yes = true;
    void* checkedArgs[] = {&yes};
    invoke(checked(klass, "set_Checked", "System.Void", {"System.Boolean"}), row.get(), checkedArgs);
    return row.get();
}

void wardrobeCtor(void* model, void* param, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    uint32_t mask = wardrobeReady.load() ? policy.enabled.load() & (Costumes | Hair) : 0;
    if (!mask) { originalWardrobeCtor(model, param, method); return; }
    // Preflight before expanding anything. If unavailable, run an ordinary page.
    try {
        for (auto& category : categories) if (mask & category.bit) snapshot(category);
    } catch (const std::exception& error) {
        wardrobeError = error.what();
        originalWardrobeCtor(model, param, method);
        return;
    }
    for (auto it = wardrobes.begin(); it != wardrobes.end();) {
        if (!api.target((*it)->model)) it = wardrobes.erase(it); else ++it;
    }
    auto entry = std::make_unique<WardrobeSession>();
    entry->model = api.weak(model, false);
    if (!entry->model) { originalWardrobeCtor(model, param, method); return; }
    entry->categories = mask;
    entry->generation = policy.generation.load();
    entry->revision = appearances.revision();
    entry->account = accountEpoch;
    auto* current = entry.get();
    wardrobes.push_back(std::move(entry));
    WardrobeContext active(model, mask);
    originalWardrobeCtor(model, param, method);
    try {
        Root source(invoke(modelCharacter, model));
        Root clone(detachedCharacter(source.get()));
        retainCharacter(*current, clone.get());
        replacePrivateCharacter(model, clone.get());
        // Restore saved local selections only. An explicit shop-to-wardrobe
        // selection keeps the game's normal preview when there is no override.
        auto id = idText(invoke(characterId, source.get()));
        for (int mode = 0; mode < 2; ++mode) {
            if (!appearances.get(std::to_string(accountEpoch), id, mode != 0, policy.enabled.load())) continue;
            Root costume(invoke(costumeGetters[mode], clone.get()));
            bool favorite = mode == 0;
            void* hairArgs[] = {&favorite};
            Root hair(invoke(hairGetters[mode], clone.get(), hairArgs));
            Root costumeData(userRow(Costumes, costume.get()));
            Root hairData(userRow(Hair, hair.get()));
            void* costumeArgs[] = {costumeData.get()};
            void* selectedHairArgs[] = {hairData.get()};
            invoke(selectCostumes[mode], model, costumeArgs);
            invoke(selectHairs[mode], model, selectedHairArgs);
        }
        invoke(updateHairs, model);
        wardrobeError.clear();
        __android_log_print(ANDROID_LOG_INFO, Tag, "Isolated wardrobe opened: categories=%u", mask);
    } catch (const std::exception& error) {
        current->failed = true;
        wardrobeError = error.what();
        __android_log_print(ANDROID_LOG_WARN, Tag, "Wardrobe initialization: %s; account writes blocked", error.what());
    }
}

void modelHairs(void* model, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto* entry = session(model);
    WardrobeContext active(model, entry ? entry->categories : 0);
    originalModelHairs(model, method);
}
void modelOrnaments(void* model, void* base, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto* entry = session(model);
    WardrobeContext active(model, entry ? entry->categories : 0);
    originalModelOrnaments(model, base, method);
}

void* costumeCollection(const Method* method) {
    void* original = originalCostumeCollection(method);
    return (wardrobeScope & Costumes) ? view(categories[0], original, true) : original;
}

void* requiredCostumeHair(void* original) {
    if (!original) return nullptr;
    auto* entry = session(wardrobeModel);
    if (!entry || !(entry->categories & Costumes)) return original;
    Root data(invoke(selectedCostume, wardrobeModel));
    if (!data.get()) return original;
    Root costume(invoke(userCostume, data.get()));
    if (!costume.get()) return original;
    Root hair(invoke(checked(api.objectClass(costume.get()), "get_DefaultHair",
                             "Solis.Common.Proto.Master.Hair"), costume.get()));
    if (!hair.get()) return original;
    Root id(invoke(checked(api.objectClass(hair.get()), "get_Id", "System.String"), hair.get()));
    void* args[] = {id.get()};
    if (invoke(checked(api.objectClass(original), "FindById", "Solis.Common.Proto.Transaction.UserHair",
                       {"System.String"}), original, args)) return original;
    const auto key = idText(id.get());
    if (key.empty()) return original;
    if (entry->requiredHair && entry->requiredHairId == key) return api.target(entry->requiredHair);
    // With only clothing enabled, add just the chosen outfit's required default
    // hair to a copy of the owned list. Do not expose every hairstyle/accessory.
    const auto* listMethod = api.method(api.objectClass(original), "GetAllList", 1);
    if (!listMethod || typeName(api.paramType(listMethod, 0)).rfind("System.Comparison", 0) != 0)
        throw std::runtime_error("Owned hair list unavailable");
    void* listArgs[] = {nullptr};
    Root rows(invoke(listMethod, original, listArgs));
    if (!rows.get()) throw std::runtime_error("Owned hair rows unavailable");
    auto* rowClass = api.objectClass(rows.get());
    auto* countMethod = checked(rowClass, "get_Count", "System.Int32");
    auto* itemMethod = checked(rowClass, "get_Item", "Solis.Common.Proto.Transaction.UserHair", {"System.Int32"});
    const auto count = scalar<int32_t>(invoke(countMethod, rows.get()));
    if (count < 0 || count > 10000) throw std::runtime_error("Unexpected owned hair count");
    auto* collectionClass = gameClass("Solis.Common.User", "UserHairCollection");
    auto* add = checked(collectionClass, "Add", "System.Void", {"Solis.Common.Proto.Transaction.UserHair"});
    auto* cloneMethod = checked(gameClass("Solis.Common.Proto.Transaction", "UserHair"), "Clone", "Solis.Common.Proto.Transaction.UserHair");
    Root collection(construct(collectionClass));
    for (int32_t i = 0; i < count; ++i) {
        void* indexArgs[] = {&i};
        Root row(invoke(itemMethod, rows.get(), indexArgs));
        Root clone(invoke(cloneMethod, row.get()));
        void* addArgs[] = {clone.get()};
        invoke(add, collection.get(), addArgs);
    }
    Root required(userRow(Hair, hair.get()));
    void* addArgs[] = {required.get()};
    invoke(add, collection.get(), addArgs);
    auto handle = api.retain(collection.get(), false);
    if (!handle) throw std::runtime_error("Cannot retain required hairstyle");
    if (entry->requiredHair) api.release(entry->requiredHair);
    entry->requiredHair = handle;
    entry->requiredHairId = key;
    return collection.get();
}

void* hairCollection(const Method* method) {
    void* original = originalHairCollection(method);
    if (wardrobeScope & Hair) return view(categories[1], original, true);
    if (wardrobeScope & Costumes) {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        try { return requiredCostumeHair(original); }
        catch (const std::exception& error) {
            wardrobeError = error.what();
            __android_log_print(ANDROID_LOG_WARN, Tag, "Required hairstyle: %s", error.what());
        }
    }
    return original;
}

void checkCostume(void* presenter, void* data, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (canForwardWardrobeWrite(session(presenterModel(presenter)) != nullptr, policy.enabled.load()))
        originalCheckCostume(presenter, data, method);
}
void checkHair(void* presenter, void* data, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (canForwardWardrobeWrite(session(presenterModel(presenter)) != nullptr, policy.enabled.load()))
        originalCheckHair(presenter, data, method);
}
Task checkBulk(void* presenter, Cancellation ct, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return canForwardWardrobeWrite(session(presenterModel(presenter)) != nullptr, policy.enabled.load())
        ? originalCheckBulk(presenter, ct, method) : Task{};
}

void applyLocalWear(void* presenter, void* model, WardrobeSession& entry, void* requestedCostume, void* requestedHair) {
    if (entry.failed || entry.account != accountEpoch ||
        !canApplyAppearance(entry.categories, policy.enabled.load(), entry.generation,
                            policy.generation.load(), entry.revision, appearances.revision())) {
        toast("预览设置已变化，请退出并重新打开换装页");
        return;
    }
    Root current(invoke(modelCharacter, model));
    Root costumeData(invoke(selectedCostume, model)), hairData(invoke(selectedHair, model));
    Root selectedCostumeId(invoke(costumeId, costumeData.get()));
    Root selectedHairId(invoke(hairId, hairData.get()));
    const auto c = idText(selectedCostumeId.get()), h = idText(selectedHairId.get());
    if (c.empty() || h.empty() || (requestedCostume && c != idText(requestedCostume)) ||
        (requestedHair && h != idText(requestedHair))) throw std::runtime_error("Appearance selection changed");
    Root costume(invoke(userCostume, costumeData.get())), hair(invoke(userHair, hairData.get()));
    if (!costume.get() || !hair.get() || !loadedAppearance(presenter, costume.get(), hair.get())) {
        toast("请等待所选外观下载并显示完成，再应用穿戴");
        wardrobeError = "Selected appearance has not finished loading";
        __android_log_print(ANDROID_LOG_WARN, Tag, "Local wear deferred: selected actor resources not ready");
        return;
    }
    void* wearableArgs[] = {costume.get()};
    if (!scalar<bool>(invoke(checked(api.objectClass(hair.get()), "IsWearableCostume", "System.Boolean",
                                {"Solis.Common.Proto.Master.Costume"}), hair.get(), wearableArgs)))
        throw std::runtime_error("Incompatible appearance");
    Root id(invoke(characterId, current.get()));
    Root real(ownedCharacter(id.get()));
    if (!real.get() || real.get() == current.get()) throw std::runtime_error("Wardrobe is not isolated");
    const auto* hashMethod = checked(api.objectClass(real.get()), "GetHashCode", "System.Int32");
    const auto beforeHash = scalar<int32_t>(invoke(hashMethod, real.get()));
    auto* manager = gameClass("Solis.Common.User", "UserDataManager");
    Root realCostumes(invoke(checked(manager, "get_Costumes", "Solis.Common.User.UserCostumeCollection", {}, true), nullptr));
    void* ownedArgs[] = {selectedCostumeId.get()};
    const bool wasOwned = invoke(checked(api.objectClass(realCostumes.get()), "FindById",
                                  "Solis.Common.Proto.Transaction.UserCostume", {"System.String"}), realCostumes.get(), ownedArgs) != nullptr;
    const int mode = scalar<int32_t>(invoke(modeGetter, model));
    if (mode != 0 && mode != 1) throw std::runtime_error("Unknown wardrobe mode");
    // Validate the final resource pair using the existing game's ActorCostume
    // resolver before committing this render choice.
    Root clone(detachedCharacter(real.get()));
    setId(setCostumeIds[mode], clone.get(), c);
    setId(setHairIds[mode], clone.get(), h);
    Root actor(originalActor(clone.get(), mode == 0, nullptr));
    if (!actor.get()) throw std::runtime_error("Cannot resolve selected appearance");
    retainCharacter(entry, clone.get());
    replacePrivateCharacter(model, clone.get());
    if (!appearances.put(std::to_string(accountEpoch), idText(id.get()), mode != 0, {c, h, entry.categories}))
        throw std::runtime_error("Invalid appearance identity");
    wardrobeError.clear();
    const bool unchanged = beforeHash == scalar<int32_t>(invoke(hashMethod, real.get()));
    __android_log_print(ANDROID_LOG_INFO, Tag, "Local appearance applied: mode=%d, costumeOwned=%d, accountUnchanged=%d; account save bypassed", mode, wasOwned, unchanged);
    toast("已应用本机外观，账号穿戴保持不变");
}

Task saveLocal(void* presenter, Cancellation ct, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    void* model = presenterModel(presenter);
    auto* entry = session(model);
    if (canForwardWardrobeWrite(entry != nullptr, policy.enabled.load()))
        return originalSave(presenter, ct, method);
    if (!entry) {
        toast("请退出并重新打开换装页后应用本机外观");
        __android_log_print(ANDROID_LOG_WARN, Tag, "Local wardrobe save blocked: editor is not isolated");
        return {};
    }
    try { applyLocalWear(presenter, model, *entry, nullptr, nullptr); }
    catch (const std::exception& error) {
        wardrobeError = error.what();
        __android_log_print(ANDROID_LOG_WARN, Tag, "Local wear rejected: %s", error.what());
        toast("外观预览未能应用，账号穿戴保持不变");
    }
    return {};
}
Task saveLocalIds(void* presenter, void* costume, void* hair, Cancellation ct, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    void* model = presenterModel(presenter);
    auto* entry = session(model);
    if (canForwardWardrobeWrite(entry != nullptr, policy.enabled.load()))
        return originalSaveIds(presenter, costume, hair, ct, method);
    if (!entry) {
        toast("请退出并重新打开换装页后应用本机外观");
        __android_log_print(ANDROID_LOG_WARN, Tag, "Local wardrobe save blocked: editor is not isolated");
        return {};
    }
    try { applyLocalWear(presenter, model, *entry, costume, hair); }
    catch (const std::exception& error) {
        wardrobeError = error.what();
        __android_log_print(ANDROID_LOG_WARN, Tag, "Local wear rejected: %s", error.what());
        toast("外观预览未能应用，账号穿戴保持不变");
    }
    return {};
}

void* actorCostume(void* character, bool favorite, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (wardrobeReady.load() && appearances.size() && character) {
        try {
            Root id(invoke(characterId, character));
            const auto saved = appearances.get(std::to_string(accountEpoch), idText(id.get()), !favorite, policy.enabled.load());
            if (saved && ownedCharacter(id.get()) == character) {
                Root clone(detachedCharacter(character));
                return originalActor(clone.get(), favorite, method);
            }
        } catch (const std::exception& error) { wardrobeError = error.what(); }
    }
    return originalActor(character, favorite, method);
}

void* homeDescriptor(HomeActor actor, void* prop, const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    // An explicit ActorCostume belongs to a scene/activity. Preserve it; only
    // the normal account-favorite path adopts the local wardrobe appearance.
    if (!actor.costume && actor.character && appearances.size()) {
        try {
            Root id(invoke(checked(api.objectClass(actor.character), "get_Id", "System.String"), actor.character));
            if (appearances.get(std::to_string(accountEpoch), idText(id.get()), false, policy.enabled.load())) {
                Root real(ownedCharacter(id.get()));
                if (real.get()) {
                    Root replacement(actorCostume(real.get(), true, nullptr));
                    if (replacement.get()) {
                        actor.costume = replacement.get();
                        return originalHomeDescriptor(actor, prop, method);
                    }
                }
            }
        } catch (const std::exception& error) { wardrobeError = error.what(); }
    }
    return originalHomeDescriptor(actor, prop, method);
}

void initializeUser(void* self, void* response, const Method* method) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        ++accountEpoch;
        appearances.clear();
        clearDecorationOverride();
    }
    originalUserInitialization(self, response, method);
}

bool layout(void* klass, size_t size, std::initializer_list<std::pair<const char*, size_t>> fields) {
    uint32_t alignment = 0;
    if (!klass || !api.isValueType(klass) || api.valueSize(klass, &alignment) != size) return false;
    for (const auto& [name, offset] : fields) {
        void* field = api.field(klass, name);
        // IL2CPP field offsets include the two-pointer boxed object header.
        if (!field || api.fieldOffset(field) != 2 * sizeof(void*) + offset) return false;
    }
    return true;
}

bool installWardrobe() {
    wardrobeError = "Wardrobe model layout unavailable";
    const char* userType = "Solis.Common.Proto.Transaction.UserCharacter";
    const char* costumeType = "Solis.Common.Proto.Transaction.UserCostume";
    const char* hairType = "Solis.Common.Proto.Transaction.UserHair";
    const char* voidType = "System.Void";
    const char* string = "System.String";
    const char* taskType = "Cysharp.Threading.Tasks.UniTask";
    const char* cancellation = "System.Threading.CancellationToken";
    auto* model = gameClass("Solis.OutGame", "CharacterCostumeScreenModel");
    auto* presenter = gameClass("Solis.OutGame", "CharacterCostumeScreenPresenter");
    auto* user = gameClass("Solis.Common.Proto.Transaction", "UserCharacter");
    auto* manager = gameClass("Solis.Common.User", "UserDataManager");
    if (!model || !presenter || !user || !manager) return false;
    presenterModelField = api.field(presenter, "_model");
    if (!presenterModelField || typeName(api.fieldType(presenterModelField)) != "Solis.OutGame.CharacterCostumeScreenModel") return false;
    auto* task = assemblyClass("UniTask.dll", "Cysharp.Threading.Tasks", "UniTask");
    auto* ct = assemblyClass("mscorlib.dll", "System.Threading", "CancellationToken");
    wardrobeError = "Wardrobe async value layout unsupported";
    if (!layout(task, sizeof(Task), {{"source", 0}, {"token", offsetof(Task, token)}}) ||
        !layout(ct, sizeof(Cancellation), {{"_source", 0}})) return false;
    modelCharacter = checked(model, "get_UserCharacter", userType);
    wardrobeError = "Private wardrobe property unavailable";
    privateCharacterField = api.field(model, "_userCharacterProperty");
    auto* privateProperty = privateCharacterField ? api.classFromType(api.fieldType(privateCharacterField)) : nullptr;
    if (!privateProperty || api.isValueType(privateProperty)) return false;
    forcePrivateCharacter = checked(privateProperty, "SetValueAndForceNotify", voidType, {userType});
    if (!forcePrivateCharacter) return false;
    userCharacters = checked(manager, "get_Characters", "Solis.Common.User.UserCharacterCollection", {}, true);
    findCharacter = checked(gameClass("Solis.Common.User", "UserCharacterCollection"), "FindById", userType, {string});
    userClone = checked(user, "Clone", userType);
    characterId = checked(user, "get_CharacterId", string);
    modeGetter = api.method(model, "get_CurrentCostumeModeType", 0);
    wardrobeError = "Wardrobe mode enum unsupported";
    if (!modeGetter) return false;
    auto* modeClass = api.classFromType(api.returnType(modeGetter));
    uint32_t alignment = 0;
    if (!modeClass || !api.isEnum(modeClass) || api.valueSize(modeClass, &alignment) != 4) return false;
    void* favoriteField = api.field(modeClass, "Favorite");
    void* liveField = api.field(modeClass, "Live");
    int32_t favorite = -1, live = -1;
    if (!favoriteField || !liveField) return false;
    api.staticGet(favoriteField, &favorite); api.staticGet(liveField, &live);
    if (favorite != 0 || live != 1) return false;
    selectedCostume = checked(model, "get_SelectCostume", costumeType);
    selectedHair = checked(model, "get_SelectHair", hairType);
    auto* uc = gameClass("Solis.Common.Proto.Transaction", "UserCostume");
    auto* uh = gameClass("Solis.Common.Proto.Transaction", "UserHair");
    costumeId = checked(uc, "get_CostumeId", string);
    hairId = checked(uh, "get_HairId", string);
    userCostume = checked(uc, "get_Costume", "Solis.Common.Proto.Master.Costume");
    userHair = checked(uh, "get_Hair", "Solis.Common.Proto.Master.Hair");
    updateHairs = checked(model, "UpdateUserHairItemModels", voidType);
    wardrobeError = "Wardrobe character accessors unavailable";
    if (!modelCharacter || !userCharacters || !findCharacter || !userClone || !characterId ||
        !selectedCostume || !selectedHair || !costumeId || !hairId || !userCostume || !userHair || !updateHairs || !toastMethod) return false;
    for (int mode = 0; mode < 2; ++mode) {
        wardrobeError = "Wardrobe appearance accessors unavailable";
        const std::string prefix = mode ? "Live" : "Favorite";
        setCostumeIds[mode] = checked(user, ("set_" + prefix + "CostumeId").c_str(), voidType, {string});
        setHairIds[mode] = checked(user, ("set_" + prefix + "HairId").c_str(), voidType, {string});
        selectCostumes[mode] = checked(model, ("set_Select" + prefix + "UserCostume").c_str(), voidType, {costumeType});
        selectHairs[mode] = checked(model, ("set_Select" + prefix + "UserHair").c_str(), voidType, {hairType});
        costumeGetters[mode] = checked(user, mode ? "get_ActualLiveCostume" : "get_FavoriteCostume", "Solis.Common.Proto.Master.Costume");
        hairGetters[mode] = checked(user, "GetHair", "Solis.Common.Proto.Master.Hair", {"System.Boolean"});
        if (!setCostumeIds[mode] || !setHairIds[mode] || !selectCostumes[mode] || !selectHairs[mode] || !costumeGetters[mode] || !hairGetters[mode]) return false;
    }
    auto* home = gameClass("Solis.OutGame", "HomeOfficeActors");
    wardrobeError = "Home actor descriptor layout unsupported";
    const Method* descriptor = home ? api.method(home, "CreateActorDescriptor", 2) : nullptr;
    if (!descriptor || api.paramCount(descriptor) != 2 || typeName(api.paramType(descriptor, 1)) != string ||
        typeName(api.returnType(descriptor)) != "Solis.Common.SolisActorDescriptor") return false;
    uint32_t impl = 0;
    if (!(api.flags(descriptor, &impl) & 0x10u)) return false;
    void* pair = api.classFromType(api.paramType(descriptor, 0));
    if (!layout(pair, sizeof(HomeActor), {{"Item1", 0}, {"Item2", sizeof(void*)}}) ||
        typeName(api.fieldType(api.field(pair, "Item1"))) != "Solis.Common.Proto.Master.Character" ||
        typeName(api.fieldType(api.field(pair, "Item2"))) != "Solis.Common.Data.ActorCostume") return false;
    // Install all write barriers before enabling expanded views. They also
    // reject writes if the toggle was enabled after an ordinary page opened.
    wardrobeError = "Wardrobe hooks unavailable";
    bool ok = hook(checked(presenter, "CheckCostumeRequest", voidType, {costumeType}), checkCostume, originalCheckCostume);
    ok &= hook(checked(presenter, "CheckHairRequest", voidType, {hairType}), checkHair, originalCheckHair);
    ok &= hook(checked(presenter, "CheckBulkAsync", taskType, {cancellation}), checkBulk, originalCheckBulk);
    ok &= hook(checked(presenter, "SetCostumeRequestAsync", taskType, {cancellation}), saveLocal, originalSave);
    ok &= hook(checked(presenter, "SetCostumeRequestAsync", taskType, {string, string, cancellation}), saveLocalIds, originalSaveIds);
    ok &= hook(checked(model, "set_UserCharacter", voidType, {userType}), setModelCharacter, originalUserSetter);
    ok &= hook(checked(model, "SetUserBaseHairItemModels", voidType), modelHairs, originalModelHairs);
    ok &= hook(checked(model, "SetUserOrnamentHairItemModels", voidType, {"Solis.OutGame.CharacterHairVerticalListItemModel"}), modelOrnaments, originalModelOrnaments);
    ok &= hook(checked(manager, "get_Costumes", "Solis.Common.User.UserCostumeCollection", {}, true), costumeCollection, originalCostumeCollection);
    ok &= hook(checked(manager, "get_Hairs", "Solis.Common.User.UserHairCollection", {}, true), hairCollection, originalHairCollection);
    ok &= hook(checked(user, "ActorCostume", "Solis.Common.Data.ActorCostume", {"System.Boolean"}), actorCostume, originalActor);
    ok &= hook(descriptor, homeDescriptor, originalHomeDescriptor);
    if (!ok) return false;
    return hook(checked(model, ".ctor", voidType, {"Solis.OutGame.CharacterCostumeTransitionParam"}), wardrobeCtor, originalWardrobeCtor);
}

#include "preview_decorations.inc"

void installAdapters(SymbolResolver resolve) {
    if (!resolve) throw std::runtime_error("Preview symbol resolver unavailable");
#define LOAD(member, name) api.member = reinterpret_cast<decltype(api.member)>(resolve(name)); if (!api.member) throw std::runtime_error("Missing preview export: " name);
    LOAD(domain, "il2cpp_domain_get")
    LOAD(assemblies, "il2cpp_domain_get_assemblies")
    LOAD(image, "il2cpp_assembly_get_image")
    LOAD(imageName, "il2cpp_image_get_name")
    LOAD(findClass, "il2cpp_class_from_name")
    LOAD(method, "il2cpp_class_get_method_from_name")
    LOAD(returnType, "il2cpp_method_get_return_type")
    LOAD(paramType, "il2cpp_method_get_param")
    LOAD(paramCount, "il2cpp_method_get_param_count")
    LOAD(flags, "il2cpp_method_get_flags")
    LOAD(typeName, "il2cpp_type_get_name")
    LOAD(free, "il2cpp_free")
    LOAD(objectClass, "il2cpp_object_get_class")
    LOAD(classFromType, "il2cpp_class_from_type")
    LOAD(isValueType, "il2cpp_class_is_valuetype")
    LOAD(newObject, "il2cpp_object_new")
    LOAD(invoke, "il2cpp_runtime_invoke")
    LOAD(unbox, "il2cpp_object_unbox")
    LOAD(retain, "il2cpp_gchandle_new")
    LOAD(target, "il2cpp_gchandle_get_target")
    LOAD(release, "il2cpp_gchandle_free")
    LOAD(weak, "il2cpp_gchandle_new_weakref")
    LOAD(field, "il2cpp_class_get_field_from_name")
    LOAD(fieldType, "il2cpp_field_get_type")
    LOAD(fieldOffset, "il2cpp_field_get_offset")
    LOAD(fieldGet, "il2cpp_field_get_value")
    LOAD(fieldSet, "il2cpp_field_set_value")
    LOAD(nestedTypes, "il2cpp_class_get_nested_types")
    LOAD(className, "il2cpp_class_get_name")
    LOAD(staticGet, "il2cpp_field_static_get_value")
    LOAD(valueSize, "il2cpp_class_value_size")
    LOAD(isEnum, "il2cpp_class_is_enum")
    LOAD(stringLength, "il2cpp_string_length")
    LOAD(stringChars, "il2cpp_string_chars")
    LOAD(newString, "il2cpp_string_new")
#undef LOAD
    void* scene = gameClass("Solis.OutGame", "OutGameScenePresenter");
    void* node = assemblyClass("quaunity-ui.Runtime.dll", "Qua.UI", "WindowNode");
    sceneInstance = checked(scene, "get_Instance", "Solis.OutGame.OutGameScenePresenter", {}, true);
    topScreen = checked(scene, "get_TopScreen", "Qua.UI.ScreenPresenterBase");
    screenParent = checked(node, "get_Parent", "Qua.UI.WindowPresenterBase");
    photographyWindowClass = gameClass("Solis.OutGame", "PhotographyWindowPresenter");
    if (!sceneInstance || !topScreen || !screenParent || !photographyWindowClass)
        throw std::runtime_error("Preview screen accessors unavailable");
    // Shared UI feedback must not depend on a successful wardrobe adapter.
    toastMethod = checked(gameClass("Solis.Common", "ToastManager"), "Show", "System.Void", {"System.String"}, true);
    const char* string = "System.String";
    const char* boolean = "System.Boolean";
    const char* hair = "Solis.Common.Proto.Master.Hair";
    const char* costume = "Solis.Common.Proto.Master.Costume";
    void* picker = gameClass("Solis.OutGame", "CostumeChangeSheetPresenter");
    void* costumes = gameClass("Solis.Common.User", "UserCostumeCollection");
    void* hairs = gameClass("Solis.Common.User", "UserHairCollection");
    void* poses = gameClass("Solis.Common.User", "UserPhotoPoseCollection");
    bool costumeReady = hook(checked(picker, "SetCostumeItemModels", "System.Void", {nullptr}), costumeScope, originalCostumeScope);
    costumeReady &= hook(checked(costumes, "GetSortedCharacterCostumes", nullptr, {string}), readCostumes, originalCostumes);
    bool hairReady = hook(checked(picker, "SetBaseHairItemModels", "System.Void", {hair}), hairScope, originalHairScope);
    hairReady &= hook(checked(picker, "SetOrnamentHairItemModels", "System.Void", {hair, hair}), ornamentScope, originalOrnamentScope);
    hairReady &= hook(checked(hairs, "GetSortedCharacterHairs", nullptr, {string, costume, boolean}), readHairs, originalHairs);
    hairReady &= hook(checked(hairs, "GetSortedCharacterOrnamentHairs", nullptr, {string}), readOrnaments, originalOrnaments);
    hairReady &= hook(checked(hairs, "ExistsCharacterOrnamentHair", boolean, {string}), hasOrnaments, originalHasOrnaments);
    // A list-only adapter would expose poses in the picker but lose them when
    // the separate 3D scene constructs PhotographyExpressionActor instances.
    bool poseReady = hook(checked(gameClass("Solis.Photography", "PhotographyExpression3DModel"),
        "CreateSpotData", "System.Void", {"Solis.Common.Proto.Master.PhotoExpression", boolean}),
        createPoseScene, originalCreatePoseScene);
    poseReady &= hook(checked(poses, "GetUserPhotoPoses", nullptr, {string, boolean}), readPoses, originalPoses);
    uint32_t ready = (costumeReady ? Costumes : 0) | (hairReady ? Hair : 0) | (poseReady ? Poses : 0);
    const bool accountReady = hook(checked(gameClass("Solis.Common.User", "UserDataManager"),
        "InitializeUserData", "System.Void", {"Solis.Common.Proto.Api.UserGetResponse"}), initializeUser, originalUserInitialization);
    wardrobeReady = costumeReady && hairReady && accountReady && installWardrobe();
    const bool decorationsReady = accountReady && installDecorations();
    if (decorationsReady) ready |= Decorations;
    else __android_log_print(ANDROID_LOG_WARN, Tag, "%s", categories[3].error.c_str());
    if (wardrobeReady.load()) wardrobeError.clear();
    else __android_log_print(ANDROID_LOG_WARN, Tag, "%s", wardrobeError.c_str());
    policy.ready = ready;
    __android_log_print(ANDROID_LOG_INFO, Tag, "Preview adapters available: %u; all disabled", policy.ready.load());
    __android_log_print(ANDROID_LOG_INFO, Tag, "Local wardrobe adapter available: %d", wardrobeReady.load());
}
}

void install(SymbolResolver resolve) {
    // The Java settings thread can arrive while il2cpp_init is still running.
    // Publish one complete result, never intermediate ready bits or strings.
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (installed.exchange(true)) return;
    try {
        installAdapters(resolve);
    } catch (const std::exception& error) {
        policy.ready = 0;
        wardrobeReady = false;
        installationError = error.what();
        __android_log_print(ANDROID_LOG_WARN, Tag, "Optional previews unavailable: %s", error.what());
    }
}

bool setEnabled(uint32_t category, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!policy.set(category, enabled)) return false;
    if (!enabled && (category == Costumes || category == Hair)) appearances.disable(category);
    if (!enabled && category == Decorations) clearDecorationOverride();
    return true;
}

void clearAppearances() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    appearances.clear();
}

std::string status() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& state : categories) rows.push_back({
        {"id", state.bit}, {"name", state.name}, {"available", bool(policy.ready.load() & state.bit)},
        {"enabled", bool(policy.enabled.load() & state.bit)}, {"rows", state.count},
        {"reads", state.uses}, {"error", state.error}});
    return nlohmann::json{{"categories", rows}, {"installation_error", installationError}, {"wardrobe_available", wardrobeReady.load()},
        {"appearances", appearances.size()}, {"wardrobe_error", wardrobeError}}.dump();
}
}
