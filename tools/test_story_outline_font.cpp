#include "../app/src/main/cpp/story_outline_font.h"
#include <cassert>
#include <limits>

int main() {
    // Ordinary story roles must not select portrait stories or generic labels.
    const std::string root = "ADVEnginePresenter/ADVEngine/UI/Canvas/Root/UI Layer/Message Area/Message Container/";
    assert(storyFontRole("Message Label", root + "Message Label", 54, .35f) == StoryFontRole::Subtitle);
    assert(storyFontRole("Name Label", root + "Name Label", 32, .5f) == StoryFontRole::SpeakerName);
    assert(storyFontRole("Message Label", "SomeWindow/Message Label", 54, .35f) == StoryFontRole::None);
    assert(storyFontRole("Name Label", root + "Name Label", 50, .5f) == StoryFontRole::None);
    assert(storyFontRole("Message Label", root + "Message Label", 54, .1f) == StoryFontRole::None);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(storyFontRole("Message Label", root + "Message Label", nan, .35f) == StoryFontRole::None);

    // Verified game 6.0.2 Solis MK5: 34pt, padding 7, face scale 1.
    assert(storyAtlasPadding(34, 1, 8) == 16);
    assert(storyAtlasPadding(90, 1, 10) == 9);
    assert(storyAtlasPadding(34, .5f, 8, 90, .5f) == 16);
    assert(storyAtlasPadding(34, 1, 8, 180) == 32);
    assert(storyAtlasPadding(34, 1, 8, 90, 2) == 9);
    assert(storyAtlasPadding(0, 1, 8) == 0);
    assert(storyAtlasPadding(34, 0, 8) == 0);
    assert(storyAtlasPadding(34, 1, 1) == 0);
    assert(storyAtlasPadding(nan, 1, 8) == 0);
    assert(storyAtlasPadding(1, 1, 1000) == 0);

    // The chosen cap keeps both visible outlines at 85-90% of the original
    // normalized range; the previous generic atlas had less than half.
    // This checks the shader's visible range, including its automatic ratio.
    for (float width : {.35f, .5f}) {
        const float t = std::fmax(1, .75f / 4 + 2 * width);
        const float original = width * 7 / (t * 34);
        const float replacement = width * storyAtlasPadding(34, 1, 8) / (t * 90);
        const float previous = width * 9 / (t * 90);
        assert(replacement > original * .85f);
        assert(replacement < original * .9f);
        assert(previous < original * .5f);
    }
}
