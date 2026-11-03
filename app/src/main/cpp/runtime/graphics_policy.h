#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "json.hpp"

namespace localify::graphics {
struct Config {
    bool enabled = false;
    int fps = 0, aa = 0;
    float scale = 0;
};
inline Config parse(const nlohmann::json& j) {
    if (!j.is_object() || j.size() != 5 || j.at("schema_version") != 1 ||
        !j.at("enabled").is_boolean() || !j.at("fps").is_number_integer() ||
        !j.at("antialiasing").is_number_integer() || !j.at("scale").is_number() ||
        j.at("fps") < 0 || j.at("fps") > 120 || j.at("antialiasing") < 0 || j.at("antialiasing") > 3)
        throw std::invalid_argument("Invalid graphics settings");
    Config c{j.at("enabled").get<bool>(),j.at("fps").get<int>(),j.at("antialiasing").get<int>(),j.at("scale").get<float>()};
    if ((c.fps != 0 && c.fps != 30 && c.fps != 60 && c.fps != 90 && c.fps != 120) ||
        (c.aa < 0 || c.aa > 3) ||
        !std::isfinite(c.scale) || (c.scale != 0 && (c.scale < .75f || c.scale > 2.f)))
        throw std::invalid_argument("Graphics setting out of range");
    return c;
}
inline nlohmann::json serialize(const Config& c) {
    return {{"schema_version",1},{"enabled",c.enabled},{"fps",c.fps},{"antialiasing",c.aa},{"scale",c.scale}};
}
struct Size { int width, height; };
inline Size targetSize(int width,int height,float scale) {
    if (width < 16 || height < 16 || !std::isfinite(scale) || scale < .75f || scale > 2.f)
        throw std::invalid_argument("Invalid render dimensions");
    double factor = std::min({double(scale),4096.0 / std::max(width,height),
        std::sqrt(8388608.0 / (double(width) * height))});
    // Render targets remain even and preserve aspect; the upper limits bound GPU allocation.
    return {std::max(16,int(width*factor)/2*2),std::max(16,int(height*factor)/2*2)};
}
}
