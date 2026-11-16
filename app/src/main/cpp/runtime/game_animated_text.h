#pragma once
#include <string_view>

namespace game_animated_text {
// Dice's API dialogue is driven by a DOTweenTMPAnimator. Its presenter hides
// the text, fades individual character meshes, then waits for that sequence.
// Keep ownership of this label with the game, including its mesh and font.
template <typename PathProvider>
bool preservesOriginal(std::string_view objectName, PathProvider pathProvider) {
    if (objectName != "Serif") return false;
    constexpr std::string_view suffix =
        "DiceWindow(Clone)/DiceStoryScreen(Clone)/ScreenCanvas/ScreenRoot/"
        "SpeechBubbleRoot/SpeechBubble/Serif";
    const auto path = pathProvider();
    const std::string_view view(path);
    if (view.size() < suffix.size()) return false;
    const auto start = view.size() - suffix.size();
    return (start == 0 || view[start - 1] == '/') && view.substr(start) == suffix;
}
}
