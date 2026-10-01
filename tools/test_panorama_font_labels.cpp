#include "../app/src/main/cpp/panorama_font_labels.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

struct UnicodeChar { int32_t unicode, stringIndex, length; };
int main() {
    using namespace panorama_font_labels;
    const std::string root(labelRoot);
    const UnicodeChar degrees[] = {{'3',0,1},{'6',1,1},{'0',2,1},{0xb0,3,1},{0,4,0}};
    const UnicodeChar live[] = {{'L',0,1},{'I',1,1},{'V',2,1},{'E',3,1},{0,4,0}};
    assert(shouldUseRhr(root + "360", degrees, 5));
    assert(shouldUseRhr(root + "LIVE", live, 5));
    assert(!shouldUseRhr(root + "360", live, 5));
    assert(!shouldUseRhr(root + "LIVE", degrees, 5));
    assert(!shouldUseRhr(root + "LIVE/Child", live, 5));
    assert(!shouldUseRhr("Other/" + root + "LIVE", live, 5));
    assert(!shouldUseRhr("LIVE", live, 5));
    assert(!shouldUseRhr(root + "LIVE", static_cast<const UnicodeChar*>(nullptr), 5));
    assert(!shouldUseRhr(root + "LIVE", live, 0));
    assert(!shouldUseRhr(root + "LIVE", live, 4));
    assert(!shouldUseRhr(root + "LIVE", live, 65537));
    const UnicodeChar trailing[] = {{'L',0,1},{'I',1,1},{'V',2,1},{'E',3,1},{'!',4,1},{0,5,0}};
    assert(!shouldUseRhr(root + "LIVE", trailing, 6));
    const UnicodeChar unterminated[] = {{'L',0,1},{'I',1,1},{'V',2,1},{'E',3,1},{' ',4,1}};
    assert(!shouldUseRhr(root + "LIVE", unterminated, 5));
    auto check = [](double parent, double left, double right) {
        auto l = centeredLayout(parent, left, right);
        assert(l.valid && l.scale > 0 && l.scale <= 1);
        const double start = l.leftCenterX - l.leftWidth / 2;
        const double end = l.rightCenterX + l.rightWidth / 2;
        const double tolerance = parent * 1e-12;
        assert(start >= -parent/2 - tolerance && end <= parent/2 + tolerance);
        assert(std::abs(start + end) <= tolerance);
        assert(std::abs((l.rightCenterX - l.rightWidth/2) -
                        (l.leftCenterX + l.leftWidth/2) - l.gap) <= tolerance);
        return l;
    };
    assert(check(300, 100, 80).scale == 1);
    assert(check(100, 100, 80).scale < 1);
    check(1, 1000000, 1);
    check(200, 1, 1000000);
    check(0.001, 0.002, 0.003);
    for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
        assert(!centeredLayout(bad, 100, 80).valid);
        assert(!centeredLayout(100, bad, 80).valid);
        assert(!centeredLayout(100, 80, bad).valid);
    }
    assert(!centeredLayout(100, 1e308, 1e308).valid);
}
