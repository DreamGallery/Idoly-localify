#include "dictionary.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "json.hpp"
#include "fan_event_ranking_matcher.h"
#include "premium_gacha_labels.h"
#include "shop_exchange_description_matcher.h"
#include "venus_achiever_name_matcher.h"

namespace localify {

DictionaryCatalog DictionaryCatalog::read(const std::string& path, bool reload) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("dictionary file is unavailable");
    nlohmann::json root = nlohmann::json::parse(input);
    if (!root.is_object()) throw std::runtime_error("dictionary must be an object");
    if (reload && (!root.contains("text") || !root.contains("i18n")))
        throw std::runtime_error("reload requires text and i18n maps");
    DictionaryCatalog next;
    auto loadMap = [](const nlohmann::json& node, auto& destination) {
        if (!node.is_object()) throw std::runtime_error("dictionary map must be an object");
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (!it.value().is_string() || it.value().get<std::string>().empty())
                throw std::runtime_error("dictionary map contains an invalid value");
            destination[it.key()] = it.value().get<std::string>();
        }
    };
    loadMap(root.value("text", nlohmann::json::object()), next.text);
    const auto numericEntries = root.value("numeric_text_templates", nlohmann::json::object());
    if (!numericEntries.is_object() || numericEntries.size() > 128)
        throw std::runtime_error("invalid numeric_text_templates map");
    for (auto it = numericEntries.begin(); it != numericEntries.end(); ++it) {
        if (!it.value().is_string() || !next.numericTemplates.add(it.key(), it.value().get<std::string>()))
            throw std::runtime_error("invalid numeric text template");
    }
    const auto activityNames = root.value("home_activity_names", nlohmann::json::object());
    if (!activityNames.is_object() || activityNames.size() > 64)
        throw std::runtime_error("invalid home_activity_names map");
    for (auto it = activityNames.begin(); it != activityNames.end(); ++it) {
        if (!it.value().is_string() ||
                !next.homeActivityNames.add(it.key(), it.value().get<std::string>()))
            throw std::runtime_error("invalid home activity name");
    }
    const auto legalEntries = root.value("legal_by_type", nlohmann::json::object());
    if (!legalEntries.is_object())
        throw std::runtime_error("legal_by_type must be an object");
    for (auto it = legalEntries.begin(); it != legalEntries.end(); ++it) {
        const std::string type = it.key();
        if (type.size() != 1 || type[0] < '1' || type[0] > '7' ||
            !it.value().is_object() || it.value().size() != 2 ||
            !it.value().contains("source") || !it.value().contains("translation") ||
            !it.value()["source"].is_string() ||
            !it.value()["translation"].is_string())
            throw std::runtime_error("invalid legal_by_type entry");
        const auto source = it.value()["source"].get<std::string>();
        const auto translated = it.value()["translation"].get<std::string>();
        if (source.empty() || translated.empty() ||
            source.size() > 1024 * 1024 || translated.size() > 1024 * 1024 ||
            source.find('\0') != std::string::npos ||
            translated.find('\0') != std::string::npos)
            throw std::runtime_error("legal_by_type text size invalid");
        const auto previous = next.legal.find(source);
        if (previous != next.legal.end() && previous->second != translated)
            throw std::runtime_error("legal_by_type source conflict");
        next.legal.emplace(source, translated);
    }
    loadMap(root.value("i18n", nlohmann::json::object()), next.i18n);
    const auto variants = root.value("i18n_by_source", nlohmann::json::object());
    if (!variants.is_object())
        throw std::runtime_error("i18n_by_source must be an object");
    for (auto it = variants.begin(); it != variants.end(); ++it)
        loadMap(it.value(), next.i18nBySource[it.key()]);
    next.options.dumpEnabled = root.value("dump", false);
    next.options.resourceReplace = root.value("resource_replace", true);
    next.options.assetReplace = root.value("asset_replace", true);
    next.options.captureOcto = root.value("capture_octo", false);
    next.options.probeMaster = root.value("probe_master", false);
    next.options.fontEnabled = root.value("font", false);
    const std::string mode = root.value("font_mode", std::string("fallback"));

    if (mode == "replace-translated") next.options.fontMode = FontMode::ReplaceTranslated;
    else if (mode == "replace-all") next.options.fontMode = FontMode::ReplaceAll;
    else if (mode == "preserve-non-cjk") next.options.fontMode = FontMode::PreserveNonCjk;
    else if (mode == "replace-ui") next.options.fontMode = FontMode::ReplaceUi;
    else if (mode == "fallback") next.options.fontMode = FontMode::Fallback;
    else throw std::runtime_error("font_mode must be fallback, replace-translated, replace-all, preserve-non-cjk, or replace-ui");
    for (const auto& [source, translated] : next.text)
        next.translatedValues.insert(translated);
    for (const auto& [source, translated] : next.legal)
        next.translatedValues.insert(translated);
    for (const auto& [key, translated] : next.i18n)
        next.translatedValues.insert(translated);

    for (const auto& [key, translations] : next.i18nBySource) {
        for (const auto& [source, translated] : translations) {
            if (source != translated) next.translatedValues.insert(translated);
            ++next.variantCount;
        }
    }
    const auto storyValues = root.value("translated_values", nlohmann::json::array());
    if (!storyValues.is_array())
        throw std::runtime_error("translated_values must be an array");
    for (const auto& value : storyValues) {
        if (!value.is_string())
            throw std::runtime_error("translated_values entries must be strings");
        const std::string translated = value.get<std::string>();
        if (!translated.empty()) next.translatedValues.insert(translated);
    }
    for (const auto& translated : next.translatedValues)
        next.userTemplates.add(translated);
    return next;
}

std::string DictionaryCatalog::nameTags(const std::string& source) const {
    if (source.empty() || source[0] != '#') return {};
    std::string result;
    bool changed = false;
    size_t start = 0;
    while (start < source.size()) {
        const size_t end = source.find('\n', start);
        const size_t limit = end == std::string::npos ? source.size() : end;
        if (source[start] != '#') return {};
        const std::string name = source.substr(start + 1, limit - start - 1);
        const auto found = text.find(name);
        if (found != text.end() && !found->second.empty() && found->second != name) {
            result += '#';
            result += found->second;
            changed = true;
        } else {
            result.append(source, start, limit - start);
        }
        if (end == std::string::npos) break;
        result += '\n';
        start = end + 1;
    }
    return changed ? result : std::string();
}

std::string translateElapsed(const std::string& source) {
    struct Suffix { const char* original; const char* translated; };
    for (const auto& suffix : {Suffix{"日前", "天前"}, Suffix{"時間前", "小时前"},
                               Suffix{"分前", "分钟前"}}) {
        const std::string original = suffix.original;
        if (source.size() <= original.size() ||
            source.compare(source.size() - original.size(), original.size(), original)) continue;
        const std::string amount = source.substr(0, source.size() - original.size());
        if (std::all_of(amount.begin(), amount.end(),
                        [](unsigned char value) { return value >= '0' && value <= '9'; }))
            return amount + suffix.translated;
    }
    return {};
}

const std::string* DictionaryCatalog::resolveI18n(const std::string& key,
                                                    const std::string& source) const {
    const auto variants = i18nBySource.find(key);
    if (variants != i18nBySource.end()) {
        const auto found = variants->second.find(source);
        return found == variants->second.end() ? nullptr : &found->second;
    }
    const auto found = i18n.find(key);
    return found == i18n.end() ? nullptr : &found->second;
}

TextDecision DictionaryCatalog::resolveText(
        const std::string& source, const std::function<std::string()>& objectPath) const {
    // Cache the hierarchy walk within this lookup, without keeping Unity handles.
    std::string path;
    bool pathRead = false;
    auto context = [&]() -> const std::string& {
        if (!pathRead) { path = objectPath(); pathRead = true; }
        return path;
    };
    if (objectPath && source == "を") {
        const auto ticket = text.find("チケット");
        const auto purchase = text.find("購入");
        const bool localized = ticket != text.end() && ticket->second != ticket->first &&
            purchase != text.end() && purchase->second != purchase->first;
        if (premium_gacha_labels::hidePurchaseParticle(source, context(), localized))
            return {true, "", false};
    }
    const auto legalValue = legal.find(source);
    if (legalValue != legal.end()) return {true, legalValue->second, true};
    if (objectPath && VenusAchieverNameMatcher::mayMatch(source)) {
        const auto pattern = i18n.find("venus.firstAchiever.name");
        if (pattern != i18n.end()) {
            auto target = VenusAchieverNameMatcher::translate(source, context(), pattern->second);
            if (!target.empty()) return {true, std::move(target), true};
        }
    }
    if (objectPath && fan_event_ranking::mayMatch(source, text)) {
        const auto pattern = i18n.find("fanEvent.ranking.sheet.description");
        const auto group = i18n.find("common.group.prefix");
        if (pattern != i18n.end() && group != i18n.end()) {
            auto target = fan_event_ranking::translate(source, context(), text,
                                                     pattern->second, group->second);
            if (!target.empty()) {
                const bool retain = target == source;
                return {true, std::move(target), false, retain};
            }
        }
    }
    if (objectPath && homeActivityNames.mayMatch(source)) {
        auto target = homeActivityNames.translate(source, context());
        if (!target.empty()) return {true, std::move(target), true};
        if (homeActivityNames.isRendered(source, context())) return {true, {}, true, true};
    }
    if (objectPath && ShopExchangeDescriptionMatcher::mayMatch(source)) {
        auto target = ShopExchangeDescriptionMatcher::translate(source, context(), text);
        if (!target.empty()) return {true, std::move(target), true};
    }
    if (objectPath && ShopScopedQuantityNameMatcher::mayMatch(source)) {
        auto target = ShopScopedQuantityNameMatcher::translate(source, context(), text);
        if (!target.empty()) return {true, std::move(target), true};
    }
    const auto exact = text.find(source);
    if (exact != text.end()) return {true, exact->second, true};
    auto target = numericTemplates.translate(source);
    if (!target.empty()) return {true, std::move(target), true};
    target = translateElapsed(source);
    if (!target.empty()) return {true, std::move(target), true};
    target = nameTags(source);
    if (!target.empty()) return {true, std::move(target), true};
    const bool exactRendered = translatedValues.count(source) || numericTemplates.isRendered(source);
    const bool player = !exactRendered && userTemplates.matches(source);
    if (exactRendered || player) return {true, {}, true, true, player};
    return {};
}

} // namespace localify
