#pragma once

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace localify::photo_experiments {
enum Context : unsigned { Live = 1, Scene = 2, Both = Live | Scene };
struct Appearance { std::string costume, hair, ornament; };
struct Rule { std::string source, target; unsigned context = Both; Appearance appearance; };
struct CostumeChoice { std::string name, hair, thumbnail; int order=0; };
struct HairChoice {
    std::string name;
    std::vector<std::string> wearable, excluded;
    std::string faceAsset;
    std::string ornamentBase, thumbnail;
    int order=0;
    bool fits(const std::string& costume) const {
        return (wearable.empty() || std::find(wearable.begin(),wearable.end(),costume)!=wearable.end()) &&
            std::find(excluded.begin(),excluded.end(),costume)==excluded.end();
    }
};
struct Character {
    std::string id, name;
    Appearance appearance;
    bool playable = false;
    bool owned = false;
    std::map<std::string,CostumeChoice> costumes;
    std::map<std::string,HairChoice> hairs;
    std::vector<std::string> modelAssets;
    std::string modelName;
    bool assetAppearance = false;
    bool collaboration = false;
    int order=0;
};

inline Appearance selectedAppearance(const Rule& rule, const Character& target) {
    Appearance selected=target.appearance;
    if(!rule.appearance.costume.empty()) {
        auto costume=target.costumes.find(rule.appearance.costume);
        if(costume==target.costumes.end())throw std::invalid_argument("服装不属于所选角色");
        selected={costume->first,costume->second.hair};
    }
    if(!rule.appearance.hair.empty())selected.hair=rule.appearance.hair;
    if(!target.modelAssets.empty()) {
        if(!rule.appearance.costume.empty()||!rule.appearance.hair.empty()||!rule.appearance.ornament.empty())
            throw std::invalid_argument("独立模型使用固定外观");
        return {};
    }
    if(!rule.appearance.hair.empty() || !rule.appearance.costume.empty()) {
        auto hair=target.hairs.find(selected.hair);
        if(hair==target.hairs.end() || !hair->second.fits(selected.costume))
            throw std::invalid_argument("发型不适用于所选角色或服装");
    }
    if(!rule.appearance.ornament.empty()) {
        auto ornament=target.hairs.find(rule.appearance.ornament);
        if(ornament==target.hairs.end() || ornament->second.ornamentBase!=selected.hair ||
           !ornament->second.fits(selected.costume))
            throw std::invalid_argument("饰品不适用于所选发型或服装");
        selected.hair=ornament->first;
    }
    return selected;
}

inline void validateRules(const std::vector<Rule>& rules, const std::map<std::string, Character>& catalog) {
    if (rules.size() > 30) throw std::invalid_argument("最多添加 30 条替换规则");
    std::map<std::string, unsigned> occupied;
    for (const auto& rule : rules) {
        auto source = catalog.find(rule.source), target = catalog.find(rule.target);
        if (source == catalog.end() || !source->second.playable || target == catalog.end())
            throw std::invalid_argument("角色目录已变化，请重新选择");
        if (!rule.context || (rule.context & ~Both) || (occupied[rule.source] & rule.context))
            throw std::invalid_argument("同一角色在同一范围只能设置一条规则");
        occupied[rule.source] |= rule.context;
        selectedAppearance(rule,target->second);
    }
}

inline const Rule* replacement(const std::vector<Rule>& rules, const std::string& source, unsigned context) {
    for (const auto& rule : rules) {
        if (rule.source != source || !(rule.context & context)) continue;
        return &rule;
    }
    return nullptr;
}

// Keep every submitted slot unique. An unavailable collaboration character can
// use an owned baseline character, but must not displace another original slot.
template<class Generator>
inline std::vector<std::string> baselineCharacters(const std::vector<std::string>& selected,
                                                  const std::vector<std::string>& owned,
                                                  Generator& random) {
    std::set<std::string> available(owned.begin(), owned.end()), reserved, used;
    auto candidates = owned;
    std::shuffle(candidates.begin(), candidates.end(), random);
    for (const auto& id : selected) if (available.count(id)) reserved.insert(id);
    std::vector<std::string> result;
    for (const auto& id : selected) {
        std::string chosen;
        if (available.count(id) && !used.count(id)) chosen = id;
        else for (const auto& candidate : candidates) {
            if (!used.count(candidate) && !reserved.count(candidate)) { chosen = candidate; break; }
        }
        if (chosen.empty()) throw std::runtime_error("已有角色不足，无法为每个拍摄位置选择初始数据");
        used.insert(chosen);
        result.push_back(chosen);
    }
    return result;
}
}
