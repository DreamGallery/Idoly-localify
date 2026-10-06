#pragma once
#include <cmath>
#include <string>

enum class StoryFontRole { None = -1, Subtitle = 0, SpeakerName = 1 };

inline StoryFontRole storyFontRole(const std::string& name, const std::string& path,
                                  float size, float width) {
    if (!std::isfinite(size) || !std::isfinite(width)) return StoryFontRole::None;
    const std::string suffix = "/Message Container/" + name;
    if (path.size() < suffix.size() || path.compare(path.size() - suffix.size(), suffix.size(), suffix))
        return StoryFontRole::None;
    if (name == "Message Label" && size >= 50 && size <= 58 && width >= .30f && width <= .40f)
        return StoryFontRole::Subtitle;
    if (name == "Name Label" && size >= 30 && size <= 34 && width >= .45f && width <= .55f)
        return StoryFontRole::SpeakerName;
    return StoryFontRole::None;
}

// TMP's usable distance range is (GradientScale - 1) * face.scale / pointSize.
// Allocate that range in the atlas itself, before any glyph is rasterized.
// Limit ordinary-story outlines to 16 atlas pixels at the 90pt sampling size.
inline int storyAtlasPadding(float sourcePoint, float sourceFaceScale, float sourceGradient,
                             float targetPoint = 90, float targetFaceScale = 1) {
    if (!std::isfinite(sourcePoint) || !std::isfinite(sourceFaceScale) ||
        !std::isfinite(sourceGradient) || !std::isfinite(targetPoint) ||
        !std::isfinite(targetFaceScale) || sourcePoint <= 0 || sourceFaceScale <= 0 ||
        sourceGradient <= 1 || targetPoint <= 0 || targetFaceScale <= 0) return 0;
    const float padding = (sourceGradient - 1) * targetPoint / sourcePoint *
        sourceFaceScale / targetFaceScale;
    if (!std::isfinite(padding) || padding < 1 || padding > 64) return 0;
    const float limit = 16.0f * (targetPoint / 90.0f);
    return static_cast<int>(std::floor(std::fmin(padding, limit)));
}
