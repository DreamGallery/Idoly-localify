#pragma once
#include <cmath>
// TMP SDF inner face boundary is proportional to (faceDilate-outlineWidth)/2.
// Use the unadjusted material baseline on every application, never the last result.
inline bool outwardOutlineFace(float baselineFace, float outlineWidth, bool outward, float& face) {
    if (!std::isfinite(baselineFace) || !std::isfinite(outlineWidth) ||
        baselineFace < -1 || baselineFace > 1 || outlineWidth < 0 || outlineWidth > 1) return false;
    face = baselineFace + (outward ? outlineWidth : 0);
    return face >= -1 && face <= 1;
}
