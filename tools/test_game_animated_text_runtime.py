"""Exercise native dialogue ownership through the production TMP entry points."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature + ' {')
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


class GameAnimatedTextRuntimeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_original_text_mesh_and_animation_survive_all_tmp_entry_points(self):
        source = (ROOT / 'app/src/main/cpp/localify.cpp').read_text()
        hooks = ''.join(function(source, signature) for signature in (
            'bool preservesGameAnimatedText(void* object)',
            'void hookTmpSetText(void* self, Il2CppString* value, void* method)',
            'void hookTmpSetTextOne(void* self, Il2CppString* value, void* method)',
            'void hookTmpSetTextTwo(void* self, Il2CppString* value, bool syncInput, void* method)',
            'void hookTmpPreRender(void* self, void* method)',
            'void hookTmpOnEnable(void* self, void* method)',
            'int32_t hookTmpUiSetArraySizes(void* self, void* array, void* method)',
            'int32_t hookTmp3dSetArraySizes(void* self, void* array, void* method)',
            'void refreshCanvasText()',
        ))
        fixture = r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "runtime/game_animated_text.h"
struct Il2CppString { std::string text; };
struct MethodInfoHead { bool dirty; };
struct Label {
    Il2CppString name{"Serif"};
    std::string path = "WindowSystem/WindowRoot/DiceWindow(Clone)/DiceStoryScreen(Clone)/"
        "ScreenCanvas/ScreenRoot/SpeechBubbleRoot/SpeechBubble/Serif";
    Il2CppString* text = nullptr;
    void* buffer = nullptr;
    void* method = nullptr;
    bool sync = false;
    int setterCalls = 0, glyphCalls = 0, enables = 0, renders = 0, dirtyCalls = 0;
};
int hierarchyReads = 0, mutations = 0;
Il2CppString translated{"译文"};
int replacementBuffer = 0;
bool measuringButtonLabel = false;
std::string utf8(Il2CppString* value) { return value ? value->text : ""; }
const MethodInfoHead* methodInfo(const char*, const char*, const char*, const char* name, int) {
    static MethodInfoHead get{false}, dirty{true};
    return std::string(name) == "SetVerticesDirty" ? &dirty : &get;
}
void* invoke(const MethodInfoHead* method, void* object, void**, void**) {
    auto& label = *static_cast<Label*>(object);
    if (method->dirty) ++label.dirtyCalls;
    return &label.name;
}
auto runtimeInvoke = invoke;
std::string textObjectPath(void* object) {
    ++hierarchyReads;
    return static_cast<Label*>(object)->path;
}
bool preservesGameAnimatedText(void*);
int32_t hookTmpUiSetArraySizes(void*, void*, void*);
void assign(void* object, Il2CppString* value, bool sync, void* method) {
    auto& label = *static_cast<Label*>(object);
    ++label.setterCalls;
    label.text = value; label.sync = sync; label.method = method;
    // A setter can synchronously build its mesh. The nested hook must retain
    // the same ownership decision without changing the incoming character array.
    hookTmpUiSetArraySizes(object, value, method);
}
void origTmpSetText(void* object, Il2CppString* value, void* method) { assign(object, value, false, method); }
void origTmpSetTextOne(void* object, Il2CppString* value, void* method) { assign(object, value, false, method); }
void origTmpSetTextTwo(void* object, Il2CppString* value, bool sync, void* method) { assign(object, value, sync, method); }
void preserveButtonMinimumWidth(void*, Il2CppString*) { ++mutations; }
Il2CppString* translateText(void*, Il2CppString*) { ++mutations; return &translated; }
Il2CppString* applyStorySwapTitle(void*, Il2CppString* value) { ++mutations; return value; }
void rememberTranslatedText(void*, Il2CppString*, Il2CppString*) { ++mutations; }
void routeTmpFontAfterSetter(void*) { ++mutations; }
void restoreStoryNameColor(void*) { ++mutations; }
void relayoutBondTalkName(void*) { ++mutations; }
void origTmpPreRender(void* object, void* method) {
    auto& label = *static_cast<Label*>(object); ++label.renders; label.method = method;
}
void localizeTmpPreRender(void* object, void* method) { ++mutations; origTmpPreRender(object, method); }
void origTmpOnEnable(void* object, void* method) {
    auto& label = *static_cast<Label*>(object); ++label.enables; label.method = method;
}
void localizeTmpOnEnable(void*) { ++mutations; }
void* prepareLoveEpisodeGlyphs(void*, void*) { ++mutations; return &replacementBuffer; }
void* prepareFormattedUiGlyphs(void*, void*) { ++mutations; return &replacementBuffer; }
void routeTmpFontBeforeGlyphLookup(void*, void*) { ++mutations; }
struct StoryPunctuationScope { explicit StoryPunctuationScope(void*) { ++mutations; } };
int32_t origTmpUiSetArraySizes(void* object, void* array, void* method) {
    auto& label = *static_cast<Label*>(object);
    ++label.glyphCalls; label.buffer = array; label.method = method;
    return 17;
}
auto origTmp3dSetArraySizes = origTmpUiSetArraySizes;
struct Runtime {
    uint64_t current = 1;
    std::recursive_mutex mutex;
    uint64_t generation() { return current; }
    std::recursive_mutex& frameMutex() { return mutex; }
} translations;
struct Tracked { uint64_t generation; };
std::unordered_map<void*, Tracked> trackedTexts;
std::unordered_map<void*, int> fontBaselines;
std::unordered_map<void*, uint32_t> registeredTmpUi;
std::unordered_map<uint32_t, void*> handles;
void* getTarget(uint32_t handle) {
    const auto found = handles.find(handle);
    return found == handles.end() ? nullptr : found->second;
}
auto gcHandleGetTarget = getTarget;
void freeHandle(uint32_t handle) { handles.erase(handle); }
auto gcHandleFree = freeHandle;
std::vector<void*> canvasDirtyQueue;
size_t canvasDirtyCursor = 0;
uint64_t canvasTextGeneration = 1, canvasFontGeneration = 1;
uint64_t canvasCaptureGeneration = 0, canvasStorySwapGeneration = 1;
std::atomic<uint64_t> fontProfileGeneration{1}, layoutCaptureGeneration{0};
std::atomic<uint64_t> storySwapPlaybackGeneration{1};
std::atomic<bool> textCaptureEnabled{false};
void collectConstrainedTitleFonts() {}
void refreshStorySwapCast() {}
void refreshProfileMaterials() {}
bool isTmpUi(void* object) { return object != nullptr; }
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
'''
        checks = r'''
int main() {
    Label dialogue;
    int method = 1, originalArray = 2;
    Il2CppString first{"はい"}, second{"次の台詞\n二行目"};
    // Even a short source that happens to match a UI dictionary stays verbatim.
    hookTmpSetText(&dialogue, &first, &method);
    assert(dialogue.text == &first && dialogue.buffer == &first);
    hookTmpSetTextOne(&dialogue, &second, &method);
    assert(dialogue.text == &second && dialogue.buffer == &second);
    for (bool sync : {false, true}) {
        hookTmpSetTextTwo(&dialogue, &first, sync, &method);
        assert(dialogue.text == &first && dialogue.sync == sync);
    }
    hookTmpSetTextTwo(&dialogue, nullptr, true, &method);
    assert(!dialogue.text && !dialogue.buffer && dialogue.setterCalls == 5);
    assert(hookTmpUiSetArraySizes(&dialogue, &originalArray, &method) == 17);
    assert(dialogue.buffer == &originalArray && dialogue.method == &method);
    assert(hookTmp3dSetArraySizes(&dialogue, &originalArray, &method) == 17);
    for (int i = 0; i < 2; ++i) {
        hookTmpOnEnable(&dialogue, &method);
        hookTmpPreRender(&dialogue, &method);
    }
    assert(dialogue.enables == 2 && dialogue.renders == 2 && dialogue.glyphCalls == 7);
    assert(dialogue.method == &method && mutations == 0);

    // API dialogue content is not stable or replayable. Ownership must be
    // independent of its length, line endings, markup and dictionary matches.
    for (const auto& content : std::vector<std::string>{
        "", " ", "はい", "一行目\n二行目\n三行目\n四行目", "一行目\r\n二行目",
        "<color=#ff0000>台詞</color>", "『台詞』……", std::string(4096, 'x'),
    }) {
        Il2CppString input{content};
        hookTmpSetTextTwo(&dialogue, &input, true, &method);
        assert(dialogue.text == &input && dialogue.buffer == &input);
        hookTmpPreRender(&dialogue, &method);
        assert(mutations == 0);
    }

    // Do not exclude another Serif, the board, choices, or a nested reward popup.
    for (const auto& path : {
        "HomeTalk/SpeechBubble/Serif",
        "WindowRoot/DiceWindow(Clone)/DiceStoryScreen(Clone)/ReceiveAllResultOverlay(Clone)/Serif",
        "WindowRoot/DiceWindow(Clone)/DiceStoryScreen(Clone)/DiceStoryBranchOverlay(Clone)/Choice1/Text",
        "WindowRoot/DiceWindow(Clone)/DiceMainScreen(Clone)/Serif",
        "OtherDiceWindow(Clone)/DiceStoryScreen(Clone)/ScreenCanvas/ScreenRoot/SpeechBubbleRoot/SpeechBubble/Serif",
    }) {
        Label other; other.path = path;
        assert(!preservesGameAnimatedText(&other));
        hookTmpSetTextTwo(&other, &first, true, &method);
        assert(other.text == &translated && other.buffer == &replacementBuffer);
        hookTmpOnEnable(&other, &method);
        hookTmpPreRender(&other, &method);
        assert(other.enables == 1 && other.renders == 1);
    }
    assert(mutations > 0);
    // Ordinary labels do not pay for hierarchy discovery on each hook call.
    Label name; name.name.text = "Name";
    const int reads = hierarchyReads;
    assert(!preservesGameAnimatedText(&name) && hierarchyReads == reads);
    assert(!preservesGameAnimatedText(nullptr));
    // No cached decision can outlive reparenting or native address reuse.
    dialogue.path = "Other/Serif";
    assert(!preservesGameAnimatedText(&dialogue));
    dialogue.path = "DiceWindow(Clone)/DiceStoryScreen(Clone)/ScreenCanvas/ScreenRoot/"
        "SpeechBubbleRoot/SpeechBubble/Serif";
    assert(preservesGameAnimatedText(&dialogue));

    // A prefab may have been registered before it was parented into Dice.
    // Dictionary/font/capture/story updates must not dirty its animated mesh.
    Label ui; ui.path = "Other/Serif";
    registeredTmpUi = {{&dialogue, 1}, {&ui, 2}};
    handles = {{1, &dialogue}, {2, &ui}};
    trackedTexts = {{&dialogue, {1}}, {&ui, {1}}};
    fontBaselines = {{&dialogue, 1}, {&ui, 1}};
    ++translations.current;
    refreshCanvasText();
    assert(ui.dirtyCalls == 1 && dialogue.dirtyCalls == 0);
    ++fontProfileGeneration;
    refreshCanvasText();
    assert(ui.dirtyCalls == 2 && dialogue.dirtyCalls == 0);
    textCaptureEnabled = true;
    ++layoutCaptureGeneration;
    refreshCanvasText();
    assert(ui.dirtyCalls == 3 && dialogue.dirtyCalls == 0);
    ++storySwapPlaybackGeneration;
    refreshCanvasText();
    assert(ui.dirtyCalls == 4 && dialogue.dirtyCalls == 0);
    // Recheck ownership at delivery too, even for a stale queued object.
    canvasDirtyQueue = {&ui, &dialogue}; canvasDirtyCursor = 0;
    refreshCanvasText();
    assert(ui.dirtyCalls == 5 && dialogue.dirtyCalls == 0);
    assert(canvasDirtyQueue.empty());
}
'''
        scratch = ROOT / '.agent-local/test-runtime'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            cpp = Path(directory) / 'fixture.cpp'
            binary = Path(directory) / 'fixture'
            cpp.write_text(fixture + hooks + checks)
            result = subprocess.run(
                ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I',
                 str(ROOT / 'app/src/main/cpp'), str(cpp), '-o', str(binary)],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
