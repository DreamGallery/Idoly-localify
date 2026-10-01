"""Exercise the actual glyph hook with TMP's string/buffer desynchronization."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LoveEpisodeRuntimeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_numeric_render_buffer_is_authoritative_and_other_labels_are_untouched(self):
        root = Path(__file__).resolve().parents[1]
        native = (root / 'app/src/main/cpp/localify.cpp').read_text()
        hook = native[native.index('void* prepareLoveEpisodeGlyphs('):
                      native.index('\nint32_t hookTmp3dSetArraySizes(')]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <codecvt>
#include <locale>
#include <string>
#include "love_episode_label.h"
namespace mixed_font_router {
struct UnicodeChar { int32_t unicode, stringIndex, length; };
}
struct Il2CppString { int32_t length; char16_t chars[64]; };
struct Il2CppByteArray {
    void* klass = nullptr; void* monitor = nullptr; void* bounds = nullptr;
    size_t length = 64;
    alignas(8) uint8_t data[64 * sizeof(mixed_font_router::UnicodeChar)]{};
};
struct MethodInfoHead { int params; const char* first; const char* second; };
struct Label {
    std::string path = std::string(love_episode_label::path);
    Il2CppByteArray raw, rebuilt;
    void* processing = &raw;
    std::u16string getter = u"第1话进行中";
    bool reenter = false;
    bool rhr = false;
    int setCount = 0, renderCount = 0;
};
void fill(Il2CppByteArray& array, const std::u16string& text) {
    std::memset(array.data, 0, sizeof(array.data));
    auto* chars = reinterpret_cast<mixed_font_router::UnicodeChar*>(array.data);
    for (size_t i = 0; i < text.size(); ++i) chars[i] = {text[i], static_cast<int>(i), 1};
}
int32_t hookTmpUiSetArraySizes(void*, void*, void*);
void setter(void* object, Il2CppString* value, void*) {
    auto& label = *static_cast<Label*>(object);
    ++label.setCount;
    label.getter.assign(value->chars, value->length);
    fill(label.rebuilt, label.getter);
    label.processing = &label.rebuilt;
    if (label.reenter) hookTmpUiSetArraySizes(object, label.processing, nullptr);
}
void* invoke(const MethodInfoHead* method, void* object, void** args, void**) {
    assert(method->params == 2 && *static_cast<bool*>(args[1]));
    setter(object, static_cast<Il2CppString*>(args[0]), nullptr);
    return nullptr;
}
auto runtimeInvoke = invoke;
void getField(void* object, void*, void* out) { *static_cast<void**>(out) = static_cast<Label*>(object)->processing; }
auto fieldGetValue = getField;
void* getFieldInfo(void* klass, const char*) { return klass; }
auto classGetFieldFromName = getFieldInfo;
void* findClass(const char*, const char*, const char*) { static int klass; return &klass; }
Il2CppString* makeString(const char* text) {
    static Il2CppString value;
    const auto str = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().from_bytes(text);
    value.length = static_cast<int32_t>(str.size());
    std::copy(str.begin(), str.end(), value.chars);
    return &value;
}
auto stringNew = makeString;
std::string textObjectPath(void* object) { return static_cast<Label*>(object)->path; }
const MethodInfoHead* methods(void*, void** iterator) {
    // Match the older TMP API: one argument is StringBuilder, not string.
    static MethodInfoHead choices[] = {{1, "System.Text.StringBuilder", nullptr},
        {2, "System.String", "System.Single"}, {2, "System.String", "System.Boolean"}};
    const auto index = reinterpret_cast<uintptr_t>(*iterator);
    if (index == 3) return nullptr;
    *iterator = reinterpret_cast<void*>(index + 1);
    return &choices[index];
}
auto classGetMethods = methods;
const char* getMethodName(const MethodInfoHead*) { return "SetText"; }
auto methodGetName = getMethodName;
uint32_t getParamCount(const MethodInfoHead* m) { return m->params; }
auto methodGetParamCount = getParamCount;
const void* getParam(const MethodInfoHead* method, int index) { return index ? method->second : method->first; }
auto methodGetParam = getParam;
char* getTypeName(const void* type) { return strdup(static_cast<const char*>(type)); }
auto typeGetName = getTypeName;
auto il2cppFree = std::free;
void routeTmpFontBeforeGlyphLookup(void* object, void* array) {
    const auto* chars = reinterpret_cast<mixed_font_router::UnicodeChar*>(static_cast<Il2CppByteArray*>(array)->data);
    static_cast<Label*>(object)->rhr = chars[0].unicode == 0x7b2c;
}
int32_t render(void* object, void* array, void*) {
    auto& label = *static_cast<Label*>(object);
    ++label.renderCount;
    assert(array == label.processing);
    return 7;
}
auto origTmpUiSetArraySizes = render;
void diagnoseHomeTalkGlyph(void*, const char*) {}
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
'''
        checks = r'''
int main() {
    Label label;
    // The string cache looks translated, but a formatted setter writes only 1.
    fill(label.raw, u"1");
    assert(hookTmpUiSetArraySizes(&label, &label.raw, nullptr) == 7);
    assert(label.getter == u"第1话进行中" && label.rhr && label.setCount == 1);
    // A complete buffer must not be reprocessed on every layout pass.
    hookTmpUiSetArraySizes(&label, label.processing, nullptr);
    assert(label.setCount == 1);
    // Fresh game digits override the stale getter and work after returning.
    fill(label.raw, u"28"); label.processing = &label.raw; label.reenter = true;
    hookTmpUiSetArraySizes(&label, label.processing, nullptr);
    assert(label.getter == u"第28话进行中" && label.setCount == 2 && label.rhr);
    // Another EpisodeText or a currency count must keep its own numeric buffer.
    Label other; other.path = "Other/EpisodeText"; fill(other.raw, u"1");
    hookTmpUiSetArraySizes(&other, &other.raw, nullptr);
    assert(other.setCount == 0 && !other.rhr);
    // Unsupported content stays unchanged, including unterminated arrays.
    Label invalid; fill(invalid.raw, u"10000");
    hookTmpUiSetArraySizes(&invalid, &invalid.raw, nullptr);
    assert(invalid.setCount == 0);
    fill(invalid.raw, u"1234"); invalid.raw.length = 4;
    hookTmpUiSetArraySizes(&invalid, &invalid.raw, nullptr);
    assert(invalid.setCount == 0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'fixture.cpp'
            binary = Path(directory) / 'fixture'
            source.write_text(fixture + hook + checks)
            subprocess.run(['c++', '-std=c++17', '-I', str(root / 'app/src/main/cpp'),
                            str(source), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
