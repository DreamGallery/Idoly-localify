"""Run the actual TMP OnEnable hook against a small Unity lifecycle fixture."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class TmpReenableTrackingTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_reenable_retains_source_but_new_assignment_and_reused_address_do_not(self):
        root = Path(__file__).resolve().parents[1]
        native = (root / 'app/src/main/cpp/localify.cpp').read_text()
        # Compile the production hook unchanged; only Unity/JNI boundaries are mocked.
        hook = native[native.index('bool hasTrackedRenderedText('):
                      native.index('\nbool tryAssetLoad(')]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
struct Il2CppString { std::string text; };
struct MethodInfoHead { int kind; };
struct Label { Il2CppString text; };
struct TrackedText {
    uint32_t weakHandle; std::string source, rendered, i18nKey; uint64_t generation;
};
std::recursive_mutex dictionaryMutex;
std::unordered_map<void*, TrackedText> trackedTexts;
std::unordered_map<uint32_t, void*> handles;
std::unordered_map<std::string, std::string> dictionary;
int refreshed = 0, translatedCount = 0, remembered = 0;
int stylingOnly = 0;
Il2CppString* resolvingI18n = nullptr;
void* target(uint32_t h) { return handles[h]; }
auto gcHandleGetTarget = target;
std::string utf8(Il2CppString* s) { return s ? s->text : ""; }
const MethodInfoHead* methodInfo(const char*, const char*, const char*, const char* name, int) {
    static MethodInfoHead get{0}, set{1}; return std::string(name) == "get_text" ? &get : &set;
}
void* invoke(const MethodInfoHead* m, void* object, void** args, void**) {
    auto* label = static_cast<Label*>(object);
    if (m->kind == 1) label->text = *static_cast<Il2CppString*>(args[0]);
    return &label->text;
}
auto runtimeInvoke = invoke;
void origTmpOnEnable(void*, void*) {}
void registerTmpUi(void*) {}
Il2CppString* translateText(void*, Il2CppString* text) {
    if (resolvingI18n == text) ++stylingOnly; else ++translatedCount;
    return text;
}
void rememberTranslatedText(void* object, Il2CppString* in, Il2CppString* out) {
    ++remembered;
    trackedTexts[object] = {1, in->text, out->text, "", 1};
}
void refreshTranslatedText(void* object) {
    ++refreshed;
    auto& state = trackedTexts.at(object);
    auto it = dictionary.find(state.i18nKey.empty() ? state.source : state.i18nKey);
    if (it != dictionary.end()) {
        static_cast<Label*>(object)->text.text = state.rendered = it->second;
        ++state.generation;
    }
}
#define LOGE(...) ((void)0)
#define LOGI(...) ((void)0)
'''
        checks = r'''
int main() {
    Label label{{"old translation"}}, unrelated{{"other"}};
    handles[1] = &label;
    trackedTexts[&label] = {1, "original", "old translation", "", 1};
    // A -> B -> A: re-enable must not record the Chinese value as a new source.
    hookTmpOnEnable(&label, nullptr);
    assert(remembered == 0 && translatedCount == 0 && refreshed == 1);
    assert(stylingOnly == 1 && resolvingI18n == nullptr);
    assert(trackedTexts.at(&label).source == "original");
    dictionary["original"] = "revised translation";
    hookTmpOnEnable(&label, nullptr);
    assert(label.text.text == "revised translation");
    assert(trackedTexts.at(&label).source == "original");
    // Key-scoped sources retain their key, even when another generic source conflicts.
    trackedTexts[&label] = {1, "original", label.text.text, "i18n.key", 2};
    dictionary["i18n.key"] = "key-specific revision";
    hookTmpOnEnable(&label, nullptr);
    assert(label.text.text == "key-specific revision");
    assert(trackedTexts.at(&label).i18nKey == "i18n.key");
    // A direct m_text change bypassing a setter is genuinely new input.
    label.text.text = "new prefab source";
    hookTmpOnEnable(&label, nullptr);
    assert(remembered == 1 && translatedCount == 1);
    assert(trackedTexts.at(&label).source == "new prefab source");
    // Recycled native address: stale weak identity cannot preserve old source.
    handles[1] = &unrelated;
    hookTmpOnEnable(&label, nullptr);
    assert(remembered == 2 && translatedCount == 2);
    handles[1] = &label;
    label.text.text = "setter assignment";
    rememberTranslatedText(&label, &label.text, &label.text);
    assert(trackedTexts.at(&label).source == "setter assignment");
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'fixture.cpp'
            binary = Path(directory) / 'fixture'
            source.write_text(fixture + hook + checks)
            subprocess.run(['c++', '-std=c++17', str(source), '-o', str(binary)],
                           check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
