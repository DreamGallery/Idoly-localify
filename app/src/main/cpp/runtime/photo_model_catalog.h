#pragma once

#include "photo_experiment_policy.h"

namespace localify::photo_experiments {
// Story-only actors have no Character/Costume/Hair MasterDB rows. These are
// complete visual sets from the game's ADV actor declarations, not invented
// inventory IDs. The asset: namespace can only be a local replacement target.
inline void addStoryModels(std::map<std::string,Character>& catalog) {
    for(const auto& actor:std::vector<std::pair<std::string,std::string>>{
            {"lim","小南来夢"},{"lmn","大須賀れもん"}}) {
        Character value;
        value.id="asset:"+actor.first;value.name=actor.second;
        value.modelName=actor.first;value.assetAppearance=true;
        const auto prefix="mdl_chr_"+actor.first;
        const auto hair=prefix+"-base-00_hair";
        value.hairs.emplace(hair,HairChoice{"默认发型",{},{},prefix+"-base-00_face"});
        for(const auto& outfit:std::vector<std::pair<std::string,std::string>>{
                {"idol","演出服"},{"casl","私服"}})
            value.costumes.emplace(prefix+"-"+outfit.first+"-00_body",CostumeChoice{outfit.second,hair});
        value.appearance={prefix+"-idol-00_body",hair};
        catalog.emplace(value.id,std::move(value));
    }
}
}
