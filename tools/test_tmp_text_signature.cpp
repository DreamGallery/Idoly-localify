#include "../app/src/main/cpp/tmp_text_signature.h"
#include <cassert>

int main() {
    using tmp_text_signature::stringSetter;
    // 6.1.0's one-parameter overload is StringBuilder, not string. Selecting it
    // by arity interprets String fields as a StringBuilder and crashes in native code.
    assert(!stringSetter("System.Void", false, {"System.Text.StringBuilder"}));
    assert(!stringSetter("System.Void", false, {"System.Char[]"}));
    assert(!stringSetter("System.Void", false, {"System.String", "System.Single"}));
    assert(stringSetter("System.Void", false, {"System.String", "System.Boolean"}));
    // Support another TMP version with a real one-parameter string setter.
    assert(stringSetter("System.Void", false, {"System.String"}));
    assert(!stringSetter("System.Void", true, {"System.String"}));
    assert(!stringSetter("System.Boolean", false, {"System.String"}));
    assert(!stringSetter("System.Void", false, {}));
}
