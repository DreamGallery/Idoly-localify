#include "../app/src/main/cpp/runtime/preview_access.h"
#include "../app/src/main/cpp/runtime/preview_access_policy.h"
#include "../app/src/main/cpp/runtime/appearance_overrides.h"
#include "../app/src/main/cpp/runtime/profile_decoration_state.h"
#include <cassert>
#include <stdexcept>

using namespace localify::preview_access;
int main() {
    Policy policy;
    assert(!policy.set(Costumes, true));
    policy.ready = Costumes | Hair | Poses;
    assert(!policy.allows(Poses, 0, false));
    assert(!policy.set(0, true));
    assert(!policy.set(Costumes | Hair, true));
    assert(!policy.set(8, true));
    assert(policy.set(Costumes, true));
    uint32_t scope = 0;
    assert(!policy.allows(Costumes, scope, true));
    {
        Scope clothing(scope, Costumes);
        assert(policy.allows(Costumes, scope, true));
        assert(!policy.allows(Hair, scope, true));
        try { Scope nested(scope, Hair); throw std::runtime_error("unwind"); }
        catch (const std::runtime_error&) {}
        assert(scope == Costumes);
    }
    assert(scope == 0);
    auto generation = policy.generation.load();
    assert(policy.set(Costumes, false));
    assert(policy.generation > generation);
    assert(!policy.allows(Costumes, Costumes, true));
    assert(policy.set(Poses, true));
    assert(policy.allows(Poses, 0, false));
    assert(!policy.allows(Hair, Hair, true));

    // Late enable and failed isolation cannot reach either account-save
    // overload or the checked-state writers; unrelated categories stay local
    // to their own features. Closing a toggle cannot release an old barrier.
    assert(canForwardWardrobeWrite(false, 0));
    assert(canForwardWardrobeWrite(false, Poses | Decorations));
    for (uint32_t enabled : {uint32_t(Costumes), uint32_t(Hair), uint32_t(Costumes | Hair),
                             uint32_t(Costumes | Hair | Poses | Decorations)}) {
        assert(!canForwardWardrobeWrite(false, enabled));
        assert(!canForwardWardrobeWrite(true, enabled));
    }
    assert(!canForwardWardrobeWrite(true, 0));
    assert(!canForwardWardrobeWrite(true, Poses | Decorations));
    assert(canForwardDecorationWrite(false, 0));
    assert(canForwardDecorationWrite(false, Costumes | Hair | Poses));
    assert(!canForwardDecorationWrite(false, Decorations));
    assert(!canForwardDecorationWrite(false, Costumes | Decorations));
    assert(!canForwardDecorationWrite(true, 0));
    assert(!canForwardDecorationWrite(true, Costumes | Hair | Poses));
    assert(!canForwardDecorationWrite(true, Decorations));
    constexpr int64_t now = 1793370000;
    assert(released(0, now));
    assert(released(now, now));
    assert(released(now * 1000, now));
    assert(!released(now + 1, now));
    assert(!released((now + 1) * 1000, now));
    assert(!released(-1, now));
    assert(!released(1, now));
    assert(!released(123456789000LL, now));
    assert(!released(638974000000000000LL, now));

    AppearanceOverrides appearances;
    assert(!appearances.put("", "idol", false, {"dress", "hair", Costumes}));
    assert(!appearances.put("account", "idol", false, {"", "hair", Costumes}));
    assert(!appearances.put("account", "idol", false, {"dress", "hair", Poses}));
    assert(appearances.put("account", "idol", false, {"daily", "hair", Costumes | Hair}));
    assert(appearances.put("account", "idol", true, {"live", "default", Costumes}));
    assert(!appearances.get("another-account", "idol", false, 7));
    assert(!appearances.get("account", "another-idol", false, 7));
    assert(appearances.get("account", "idol", false, 7)->costume == "daily");
    assert(appearances.get("account", "idol", true, 7)->costume == "live");
    assert(!appearances.get("account", "idol", false, Costumes));
    auto revision = appearances.revision();
    assert(canApplyAppearance(3, 7, 10, 10, revision, revision));
    assert(!canApplyAppearance(3, 1, 10, 10, revision, revision));
    assert(!canApplyAppearance(3, 7, 10, 11, revision, revision));
    appearances.disable(Hair);
    assert(!canApplyAppearance(3, 7, 10, 10, revision, appearances.revision()));
    assert(!appearances.get("account", "idol", false, 7));
    assert(appearances.get("account", "idol", true, 7)->costume == "live");
    appearances.clear();
    assert(appearances.size() == 0);
    assert(!appearances.get("account", "idol", true, 7));

    policy.ready.fetch_or(Decorations);
    assert(policy.set(Decorations, true));
    assert(policy.allows(Decorations, Decorations, true));
    assert(!policy.allows(Decorations, Costumes, true));
    assert(policy.set(Decorations, false));
    assert(!policy.allows(Decorations, Decorations, true));

    DecorationOverride decoration;
    assert(!decoration.get(1, true));
    assert(decoration.set(1, ""));
    assert(decoration.get(1, true) && decoration.get(1, true)->empty());
    assert(decoration.set(1, "deco-frame"));
    assert(!decoration.set(0, "deco-frame"));
    assert(!decoration.set(1, "invalid\nvalue"));
    assert(!decoration.get(2, true));
    assert(!decoration.get(1, false));
    const auto decorationRevision = decoration.revision();
    assert(currentDecorationSession(true, 1, 1, 2, 2, decorationRevision, decorationRevision));
    assert(!currentDecorationSession(false, 1, 1, 2, 2, decorationRevision, decorationRevision));
    assert(!currentDecorationSession(true, 1, 2, 2, 2, decorationRevision, decorationRevision));
    assert(!currentDecorationSession(true, 1, 1, 2, 3, decorationRevision, decorationRevision));
    decoration.clear();
    assert(!decoration.get(1, true));
    assert(!currentDecorationSession(true, 1, 1, 2, 2, decorationRevision, decoration.revision()));
}
