#include "../app/src/main/cpp/button_minimum_layout.h"
#include <cassert>
#include <limits>

int main() {
    using namespace button_minimum_layout;
    assert(plainLabel("レイアウト"));
    assert(!plainLabel("所持数 {0}"));
    assert(!plainLabel("<sprite=1>購入"));
    assert(!plainLabel("長い説明\n続き"));
    assert(!plainLabel(""));
    // Do not change a sibling, composite control, fixed frame or left-aligned row.
    assert(eligible(true, true, true, 4));
    assert(!eligible(false, true, true, 4));
    assert(!eligible(true, false, true, 4));
    assert(!eligible(true, true, false, 4));
    assert(!eligible(true, true, true, 3));
    // Preserve a stronger minimum supplied by the game; reject empty or
    // multi-line measurements rather than widening unrelated descriptions.
    assert(minimumWidth(160.01f, 40, 40, 200).value() == 200);
    assert(minimumWidth(200.01f, 40, 40, -1).value() == 200.01f);
    assert(!minimumWidth(200, 100, 40, -1));
    assert(!minimumWidth(0, 40, 40, -1));
    assert(!minimumWidth(200, 40, 40, std::numeric_limits<float>::quiet_NaN()));
    assert(!minimumWidth(std::numeric_limits<float>::infinity(), 40, 40, -1));
}
