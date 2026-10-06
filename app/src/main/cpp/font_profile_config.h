#pragma once

#include <cmath>
#include <string>
#include <unordered_map>
#include "json.hpp"
#include "font_profile_defaults.h"

// A value snapshot. The caller publishes it under its runtime configuration
// lock; this class never calls Unity or mutates a partially validated snapshot.
class FontProfileConfig {
public:
    using Json = nlohmann::json;
    FontProfileConfig() : data_(Json::parse(kFontProfileDefaults)) { index(); }

    bool assign(const std::string& encoded) {
        if (encoded.size() > 65536) return false;
        try {
            Json candidate = Json::parse(encoded);
            const Json defaults = Json::parse(kFontProfileDefaults);
            const Json bounds = Json::parse(kFontProfileBounds);
            if (!candidate.is_object() || candidate.size() != defaults.size()) return false;
            // Migrate only the exact previously shipped font metadata. The
            // complete schema/selectors/guards/adjustments are validated below.
            const Json legacyFont = {{"mode", "replace-all"},
                {"unity_font_name", "FOT-UDKakugo_LargePro-DB"}};
            const Json previousFont = {{"mode", "preserve-non-cjk"},
                {"unity_font_name", "Localify-ResourceHanRoundedSC-ExtraBold"}};
            if (candidate.contains("font") &&
                (candidate["font"] == legacyFont || candidate["font"] == previousFont) &&
                defaults["font"].value("mode", std::string()) == "replace-ui")
                candidate["font"] = defaults["font"];
            for (const auto& entry : defaults.items()) {
                if (entry.key() != "profiles" &&
                    (!candidate.contains(entry.key()) || candidate.at(entry.key()) != entry.value()))
                    return false;
            }
            // Only these shipped extensions may be absent in older complete
            // settings. All original selectors, guards and adjustments remain
            // strictly validated below; missing required profiles still fail.
            if (candidate.contains("profiles") && candidate["profiles"].is_array()) {
                // The portrait pill sizes itself to its name. Retire the old
                // character-count auto-fit toggle, retaining all user values.
                for (auto& item : candidate["profiles"])
                    if (item.is_object() && item.value("id", std::string()) == "portrait_love_event_name" &&
                        item.contains("adjust") && item["adjust"].is_object() &&
                        item["adjust"].contains("auto_size_when_characters_gt")) {
                        const auto& old = item["adjust"]["auto_size_when_characters_gt"];
                        if (!old.is_number_integer() || !number(old, 0, 20)) return false;
                        item["adjust"].erase("auto_size_when_characters_gt");
                    }
                // Older subtitle settings predate the two layout controls.
                // Fill only these new fields; retain every saved adjustment.
                for (auto& item : candidate["profiles"])
                    if (item.is_object() && item.value("id", std::string()) == "ordinary_story_subtitle" &&
                        item.contains("adjust") && item["adjust"].is_object())
                        for (const auto& baseline : defaults["profiles"])
                            if (baseline["id"] == "ordinary_story_subtitle")
                                for (const char* field : {"font_size_unity", "character_spacing_unity"})
                                    if (!item["adjust"].contains(field))
                                        item["adjust"][field] = baseline["adjust"][field];
                for (const std::string optional : {"daily_roulette_description",
                     "costume_selection_hide_ui", "costume_selection_reaction", "costume_selection_determine"}) {
                    bool found = false;
                    for (const auto& item : candidate["profiles"])
                        if (item.is_object() && item.value("id", std::string()) == optional) found = true;
                    if (!found) for (const auto& item : defaults["profiles"])
                        if (item["id"] == optional) candidate["profiles"].push_back(item);
                }
            }
            if (!candidate.contains("profiles") || !candidate["profiles"].is_array() ||
                candidate["profiles"].size() != defaults["profiles"].size()) return false;
            Json ordered = Json::array();
            for (const auto& baseline : defaults["profiles"]) {
                const Json* selected = nullptr;
                for (const auto& profile : candidate["profiles"]) {
                    if (profile.is_object() && profile.value("id", std::string()) == baseline["id"]) {
                        if (selected) return false;
                        selected = &profile;
                    }
                }
                if (!selected || selected->size() != baseline.size()) return false;
                for (const auto& field : baseline.items()) {
                    if (field.key() != "adjust" &&
                        (!selected->contains(field.key()) || selected->at(field.key()) != field.value()))
                        return false;
                }
                if (!selected->contains("adjust")) return false;
                const auto& adjust = selected->at("adjust");
                if (!adjust.is_object() || adjust.size() != baseline["adjust"].size()) return false;
                for (const auto& field : baseline["adjust"].items()) {
                    if (!adjust.contains(field.key())) return false;
                    const auto& value = adjust.at(field.key());
                    if (bounds.contains(field.key())) {
                        if (!number(value, bounds[field.key()][0], bounds[field.key()][1])) return false;
                    } else if (field.value().is_boolean()) {
                        if (!value.is_boolean()) return false;
                    } else if (field.value().is_array()) {
                        if (!value.is_array() || value.size() != field.value().size()) return false;
                        const bool rect = field.key() == "rect_size_unity";
                        for (const auto& component : value)
                            if (!number(component, rect ? 1 : 0, rect ? 1000 : 1)) return false;
                    } else if (value != field.value()) return false;
                }
                ordered.push_back(*selected);
            }
            candidate["profiles"] = std::move(ordered);
            data_ = std::move(candidate);
            index();
            return true;
        } catch (const std::exception&) { return false; }
    }

    float value(const char* profile, const char* field) const {
        return adjust(profile).at(field).get<float>();
    }
    bool flag(const char* profile, const char* field) const {
        return adjust(profile).at(field).get<bool>();
    }
    const Json& adjust(const char* profile) const {
        return definition(profile).at("adjust");
    }
    const Json& definition(const char* profile) const { return data_["profiles"].at(indices_.at(profile)); }
    std::string dump() const { return data_.dump(); }

private:
    static bool number(const Json& value, double low, double high) {
        if (!value.is_number()) return false;
        const double x = value.get<double>();
        return std::isfinite(x) && x >= low && x <= high;
    }
    void index() {
        indices_.clear();
        for (size_t i = 0; i < data_["profiles"].size(); ++i)
            indices_.emplace(data_["profiles"][i]["id"].get<std::string>(), i);
    }
    Json data_;
    std::unordered_map<std::string, size_t> indices_;
};
