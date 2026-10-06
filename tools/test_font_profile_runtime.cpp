#include <cassert>
#include "font_profile_config.h"
#include "font_label_geometry.h"
#include "font_outline_geometry.h"
#include <limits>

int main() {
    FontProfileConfig profile;
    auto changed = nlohmann::json::parse(profile.dump());
    changed["profiles"][0]["adjust"]["outline_width"] = 0.6;
    assert(profile.assign(changed.dump()));
    assert(profile.value("ordinary_story_subtitle", "outline_width") == 0.6f);
    const std::string good = profile.dump();
    auto oldPortrait = nlohmann::json::parse(good);
    for (auto& item : oldPortrait["profiles"])
        if (item["id"] == "portrait_love_event_name")
            item["adjust"]["auto_size_when_characters_gt"] = 2;
    assert(profile.assign(oldPortrait.dump()));
    assert(profile.dump() == good); // Retired fitting must not reset other saved settings.
    for (const auto& invalid : nlohmann::json::array({true, -1, 21, 2.5, "2"})) {
        for (auto& item : oldPortrait["profiles"])
            if (item["id"] == "portrait_love_event_name")
                item["adjust"]["auto_size_when_characters_gt"] = invalid;
        assert(!profile.assign(oldPortrait.dump()));
        assert(profile.dump() == good);
    }
    auto oldSubtitle = nlohmann::json::parse(good);
    oldSubtitle["profiles"][0]["adjust"].erase("font_size_unity");
    oldSubtitle["profiles"][0]["adjust"].erase("character_spacing_unity");
    assert(profile.assign(oldSubtitle.dump()));
    assert(profile.dump() == good); // Existing outline survives new layout defaults.
    oldSubtitle["profiles"][0]["adjust"].erase("face_dilate");
    assert(!profile.assign(oldSubtitle.dump())); // Do not backfill unrelated required fields.
    assert(profile.dump() == good);
    auto subtitleLayout = nlohmann::json::parse(good);
    subtitleLayout["profiles"][0]["adjust"]["font_size_unity"] = 66;
    subtitleLayout["profiles"][0]["adjust"]["character_spacing_unity"] = -2.5;
    assert(profile.assign(subtitleLayout.dump()));
    assert(profile.value("ordinary_story_subtitle", "font_size_unity") == 66);
    assert(profile.value("ordinary_story_subtitle", "character_spacing_unity") == -2.5f);
    auto badLayout = subtitleLayout;
    badLayout["profiles"][0]["adjust"]["font_size_unity"] = 0;
    assert(!profile.assign(badLayout.dump()));
    assert(profile.dump() == subtitleLayout.dump());
    badLayout = subtitleLayout;
    badLayout["profiles"][0]["adjust"]["character_spacing_unity"] = true;
    assert(!profile.assign(badLayout.dump()));
    badLayout = subtitleLayout;
    badLayout["profiles"][1]["adjust"]["font_size_unity"] = 40;
    assert(!profile.assign(badLayout.dump())); // Name layout stays intentionally unavailable.
    assert(profile.assign(good));
    auto legacy = nlohmann::json::parse(good);
    legacy["font"] = {{"mode", "replace-all"}, {"unity_font_name", "FOT-UDKakugo_LargePro-DB"}};
    assert(profile.assign(legacy.dump()));
    assert(profile.value("ordinary_story_subtitle", "outline_width") == 0.6f);
    assert(nlohmann::json::parse(profile.dump())["font"]["mode"] == "replace-ui");
    auto previous = nlohmann::json::parse(good);
    previous["font"]["mode"] = "preserve-non-cjk";
    assert(profile.assign(previous.dump()));
    assert(profile.dump() == good);
    auto invalidPrevious = previous;
    invalidPrevious["profiles"][0]["path_suffix"] = {"Other"};
    assert(!profile.assign(invalidPrevious.dump()));
    assert(profile.dump() == good);
    auto forgedLegacy = legacy;
    forgedLegacy["profiles"][0]["guard"]["original_font_size_unity"] = {0, 200};
    assert(!profile.assign(forgedLegacy.dump()));
    assert(profile.dump() == good);
    forgedLegacy = legacy;
    forgedLegacy["font"]["unity_font_name"] = "UnknownFont";
    assert(!profile.assign(forgedLegacy.dump()));
    assert(profile.dump() == good);

    auto sixteenProfiles = nlohmann::json::parse(good);
    for (auto it = sixteenProfiles["profiles"].begin(); it != sixteenProfiles["profiles"].end();) {
        if ((*it)["id"].get<std::string>().find("costume_selection_") == 0)
            it = sixteenProfiles["profiles"].erase(it);
        else ++it;
    }
    assert(sixteenProfiles["profiles"].size() == 16);
    assert(profile.assign(sixteenProfiles.dump()));
    assert(profile.dump() == good);
    auto fifteenProfiles = sixteenProfiles;
    for (auto it = fifteenProfiles["profiles"].begin(); it != fifteenProfiles["profiles"].end(); ++it)
        if ((*it)["id"] == "daily_roulette_description") { fifteenProfiles["profiles"].erase(it); break; }
    assert(profile.assign(fifteenProfiles.dump()));
    assert(profile.dump() == good);
    auto oldProfiles = nlohmann::json::parse(good);
    for (auto it=oldProfiles["profiles"].begin();it!=oldProfiles["profiles"].end();++it)
        if ((*it)["id"] == "daily_roulette_description") { oldProfiles["profiles"].erase(it); break; }
    assert(profile.assign(oldProfiles.dump()));
    assert(profile.dump() == good); // Previously saved adjustments survive the new default.
    auto wrongOld = oldProfiles;
    wrongOld["profiles"][0]["guard"]["original_font_size_unity"] = {0,200};
    assert(!profile.assign(wrongOld.dump()));
    assert(profile.dump() == good);
    wrongOld = oldProfiles;
    wrongOld["profiles"].erase(wrongOld["profiles"].begin());
    assert(!profile.assign(wrongOld.dump())); // No broad missing-profile migration.
    auto invalidDaily = nlohmann::json::parse(good);
    for (auto& item : invalidDaily["profiles"])
        if (item["id"] == "daily_roulette_description") item["adjust"]["outline_outward"] = 1;
    assert(!profile.assign(invalidDaily.dump()));
    float outward=0;
    assert(outwardOutlineFace(0.1f,0.35f,true,outward));
    assert(std::fabs(outward-0.45f)<0.0001f);
    assert(std::fabs((outward-0.35f)-0.1f)<0.0001f); // Original face boundary preserved.
    assert(outwardOutlineFace(0.1f,0.35f,true,outward) && std::fabs(outward-0.45f)<0.0001f);
    assert(outwardOutlineFace(0.1f,0.35f,false,outward) && std::fabs(outward-0.1f)<0.0001f);
    assert(!outwardOutlineFace(0.9f,0.35f,true,outward)); // Cannot represent safely: keep material.
    assert(!outwardOutlineFace(std::numeric_limits<float>::quiet_NaN(),0.35f,true,outward));

    changed["profiles"][0]["adjust"]["outline_width"] = 1.1;
    assert(!profile.assign(changed.dump()));
    assert(profile.dump() == good);
    changed = nlohmann::json::parse(good);
    changed["profiles"][0]["guard"]["original_font_size_unity"] = {0, 200};
    assert(!profile.assign(changed.dump()));
    changed = nlohmann::json::parse(good);
    changed["profiles"][0]["adjust"]["outline_width"] = true;
    assert(!profile.assign(changed.dump()));
    assert(!profile.assign("{}"));
    assert(!profile.assign("not json"));
    assert(profile.dump() == good);
    assert(profile.assign(kFontProfileDefaults));
    assert(profile.value("ordinary_story_subtitle", "outline_width") == 0.35f);
    assert(profile.value("ordinary_story_subtitle", "face_dilate") == 0.35f);
    assert(profile.value("ordinary_story_subtitle", "font_size_unity") == 54.0f);
    assert(profile.value("ordinary_story_subtitle", "character_spacing_unity") == 16.0f);
    assert(profile.value("ordinary_story_name", "outline_width") == 0.5f);
    assert(profile.value("ordinary_story_name", "face_dilate") == 0.5f);
    auto controls = nlohmann::json::parse(profile.dump());
    for (auto& item : controls["profiles"]) {
        if (item["id"] == "shop_new_arrivals") item["adjust"]["content_width_ratio"] = 0.80;
    }
    assert(profile.assign(controls.dump()));
    assert(profile.value("shop_new_arrivals", "content_width_ratio") == 0.80f);
    const auto accepted = profile.dump();
    for (auto& item : controls["profiles"]) {
        if (item["id"] == "shop_new_arrivals") item["path_suffix"] = {"Noti", "Text"};
    }
    assert(!profile.assign(controls.dump()));
    assert(profile.dump() == accepted);
    float x = 0;
    assert(centeredLabelX(-120, 240, 216, .5f, .5f, 0, x) && x == -108);
    assert(centeredLabelX(-120, 240, 216, .5f, .5f, .5f, x) && x == 0);
    assert(centeredLabelX(-120, 240, 216, .5f, .5f, 1, x) && x == 108);
    assert(centeredLabelX(0, 240, 216, 0, 1, 0, x) && x == 12);
    assert(centeredLabelX(0, 480, 432, 0, 1, 0, x) && x == 24);
    assert(centeredLabelX(-120, 240, 216, 0, 0, .5f, x) && x == 120);
    assert(!centeredLabelX(0, 0, 120, 0, 1, .5f, x));
    assert(!centeredLabelX(0, 120, 121, 0, 1, .5f, x));
    assert(!centeredLabelX(0, 120, 100, 1, 0, .5f, x));
    assert(!centeredLabelX(0, std::numeric_limits<float>::quiet_NaN(), 100, 0, 1, .5f, x));

    const std::string costume = "WindowSystem/WindowRoot/CharacterWindow(Clone)/CharacterCostumeScreen(Clone)/ScreenCanvas/ScreenRoot/UIRoot/Fotter/";
    const std::string preview = "WindowSystem/WindowRoot/Preview3dWindow(Clone)/CostumeFittingRoomScreen(Clone)/ScreenCanvas/ScreenRoot/UIRoot/Fotter/";
    assert(std::string(costumeButtonProfile(costume + "HideButton/Label")) == "costume_selection_hide_ui");
    assert(std::string(costumeButtonProfile(costume + "ReactionButton/Label")) == "costume_selection_reaction");
    assert(std::string(costumeButtonProfile(costume + "DetermineButton/Label")) == "costume_selection_determine");
    assert(std::string(costumeButtonProfile(preview + "HideButton/Label")) == "costume_preview_hide_ui");
    assert(!costumeButtonProfile(costume + "CloseButton/Label"));
    assert(!costumeButtonProfile("Other/" + costume + "DetermineButton/Label"));
    assert(!costumeButtonProfile(preview + "DetermineButton/Label"));
    // Captured main costume page: fixed parent buttons 232/254/232 wide,
    // labels pivot at their left edge with centered horizontal anchors.
    assert(centeredLabelX(-116, 232, 208.8f, .5f, .5f, 0, x) && std::fabs(x + 104.4f) < .01f);
    assert(centeredLabelX(-127, 254, 228.6f, .5f, .5f, 0, x) && std::fabs(x + 114.3f) < .01f);
}
