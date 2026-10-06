// The test runner extracts the production loader into this include. Only its
// Android-independent state is supplied here; parsing is never reimplemented.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "json.hpp"
#include "master_blob_matcher.h"
#include "user_template_matcher.h"
#include "numeric_text_template_matcher.h"
#include "home_activity_name_matcher.h"

#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
std::unordered_map<std::string, std::string> textDict, legalTextDict, i18nDict;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> i18nBySource;
std::unordered_set<std::string> translatedValues;
UserTemplateMatcher translatedUserTemplates;
NumericTextTemplateMatcher numericTextTemplates;
HomeActivityNameMatcher homeActivityNames;
MasterBlobMatcher masterBlobPatches;
std::recursive_mutex dictionaryMutex;
std::atomic<uint64_t> translationGeneration{1};
bool dumpEnabled = false, resourceReplace = true, assetReplace = true;
bool captureOcto = false, probeMaster = false, fontEnabled = false;
enum class FontMode { Fallback, ReplaceTranslated, ReplaceAll, PreserveNonCjk, ReplaceUi };
FontMode fontMode = FontMode::Fallback;
#include "dictionary_loader_under_test.inc"

void write(const std::string& path, const nlohmann::json& value) {
    std::ofstream stream(path);
    stream << value.dump();
}

int main(int argc, char** argv) {
    assert(argc == 2 || argc == 4);
    const std::string path = std::string(argv[1]) + "/dictionary.json";
    nlohmann::json old = {{"dump", true}, {"font", true}, {"font_mode", "replace-ui"},
        {"text", {{"source", "old"}}}, {"i18n", nlohmann::json::object()}};
    write(path, old);
    assert(loadDictionary(path));
    assert(dumpEnabled && textDict.at("source") == "old");
    const auto generation = translationGeneration.load();

    auto next = old;
    next["dump"] = false;
    next["text"]["source"] = "new";
    write(path, next);
    bool restart = false;
    assert(loadDictionary(path, true, &restart) && restart);
    assert(dumpEnabled && textDict.at("source") == "old");
    assert(translationGeneration.load() == generation);
    assert(!loadDictionary(path, true)); // Legacy immediate reload remains strict.

    auto invalid = next;
    invalid["text"]["source"] = 42;
    write(path, invalid);
    assert(!loadDictionary(path, true, &restart) && !restart);
    invalid = next;
    invalid["translated_values"] = {42};
    write(path, invalid);
    assert(!loadDictionary(path, true, &restart) && !restart);
    invalid = next;
    invalid["font_mode"] = "unknown";
    write(path, invalid);
    assert(!loadDictionary(path, true, &restart) && !restart);
    assert(dumpEnabled && textDict.at("source") == "old");
    assert(translationGeneration.load() == generation);

    // Applying the downloaded file on next startup changes configuration safely.
    write(path, next);
    assert(loadDictionary(path));
    assert(!dumpEnabled && textDict.at("source") == "new");
    next["text"]["source"] = "newer";
    write(path, next);
    assert(loadDictionary(path, true, &restart) && !restart);
    assert(textDict.at("source") == "newer");
    assert(translationGeneration.load() == generation + 1);

    // Subsequent updates while waiting for restart also leave live state intact,
    // even if their settings happen to match the running process again.
    next["text"]["source"] = "pending";
    write(path, next);
    assert(loadDictionary(path, true, &restart, true) && restart);
    assert(textDict.at("source") == "newer");
    assert(translationGeneration.load() == generation + 1);

    // Missing/corrupt input must not be accepted or mutate the running dictionary.
    std::ofstream(path) << "{";
    assert(!loadDictionary(path, true, &restart));
    assert(!loadDictionary(path + ".missing", true, &restart));
    assert(textDict.at("source") == "newer");

    if (argc == 4) {
        // Optional real released dictionaries: old startup -> deferred update -> restart.
        assert(loadDictionary(argv[2]));
        const auto oldSize = textDict.size();
        const auto oldDump = dumpEnabled;
        assert(loadDictionary(argv[3], true, &restart) && restart);
        assert(textDict.size() == oldSize && dumpEnabled == oldDump);
        assert(loadDictionary(argv[3]));
        assert(loadDictionary(argv[3], true, &restart) && !restart);
    }
}
