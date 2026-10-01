#include "../app/src/main/cpp/font_label_geometry.h"
#include <cassert>
int main() {
    // The support Label used to share the ReplaceAll-only Rank gate: migrating
    // the font policy must not disable its existing Chinese layout profile.
    assert(homeRankOrSupportLayoutCandidate("Label",true));
    assert(homeRankOrSupportLayoutCandidate("Label",false));
    assert(homeRankOrSupportLayoutCandidate("Rank",true));
    assert(!homeRankOrSupportLayoutCandidate("Rank",false));
    assert(!homeRankOrSupportLayoutCandidate("Serif",true));
    assert(!homeRankOrSupportLayoutCandidate("Title",false));
}
