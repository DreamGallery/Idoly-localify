#include "../app/src/main/cpp/love_font_policy.h"
#include "../app/src/main/cpp/story_punctuation.h"
#include <cassert>
#include <string_view>

int main() {
    assert(love_font_policy::usesRoundedReplacement("FOT-UDMarugo_SmallPr6-B SDF"));
    assert(love_font_policy::usesRoundedReplacement("FOT-UDMarugo_SmallPr6-B Dynamic"));
    assert(!love_font_policy::usesRoundedReplacement("Solis-MK5 SDF"));
    assert(!love_font_policy::usesRoundedReplacement("FOT-UDKakugo_LargePro-DB SDF"));
    assert(!love_font_policy::usesRoundedReplacement("Localify-LoveRoundedSC"));
    for (uint32_t c : {0x300c, 0x300d, 0x300e, 0x300f, 0xff01, 0xff1f, 0x2026})
        assert(story_punctuation::isPunctuation(c));
    for (uint32_t c : {0x20, 0x3000, 0x30fc, 0x30, 0x41, 0x6211})
        assert(!story_punctuation::isPunctuation(c));
    assert(std::string_view(story_punctuation::profile("Screen/Message Container/Message Label")) == "ordinary_story_subtitle");
    assert(std::string_view(story_punctuation::profile("Screen/Love UI/Message Area/Message Container/Message Root/Message Text")) == "portrait_love_event_dialogue");
    assert(!story_punctuation::profile("Screen/Message Container/Name Label"));
    assert(!story_punctuation::profile("Menu/Message Text"));
}
