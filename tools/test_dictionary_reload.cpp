#include <cassert>
#include <fstream>
#include <string>
#include "json.hpp"
#include "runtime/dictionary.h"

localify::DictionaryRuntime runtime;

bool loadDictionary(const std::string& path, bool reload = false,
                    bool* restartRequired = nullptr, bool deferApply = false) {
    if (restartRequired) *restartRequired = false;
    try {
        const auto result = runtime.load(path, reload, deferApply, restartRequired != nullptr);
        if (restartRequired) *restartRequired = result == localify::DictionaryUpdate::RestartRequired;
        return true;
    } catch (const std::exception&) { return false; }
}

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
    assert(runtime.options().dumpEnabled && runtime.catalog().text.at("source") == "old");
    const auto generation = runtime.generation();

    auto next = old;
    next["dump"] = false;
    next["text"]["source"] = "new";
    write(path, next);
    bool restart = false;
    assert(loadDictionary(path, true, &restart) && restart);
    assert(runtime.options().dumpEnabled && runtime.catalog().text.at("source") == "old");
    assert(runtime.generation() == generation);
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
    assert(runtime.options().dumpEnabled && runtime.catalog().text.at("source") == "old");
    assert(runtime.generation() == generation);

    // Applying the downloaded file on next startup changes configuration safely.
    write(path, next);
    assert(loadDictionary(path));
    assert(!runtime.options().dumpEnabled && runtime.catalog().text.at("source") == "new");
    next["text"]["source"] = "newer";
    write(path, next);
    assert(loadDictionary(path, true, &restart) && !restart);
    assert(runtime.catalog().text.at("source") == "newer");
    assert(runtime.generation() == generation + 1);

    // Subsequent updates while waiting for restart also leave live state intact,
    // even if their settings happen to match the running process again.
    next["text"]["source"] = "pending";
    write(path, next);
    assert(loadDictionary(path, true, &restart, true) && restart);
    assert(runtime.catalog().text.at("source") == "newer");
    assert(runtime.generation() == generation + 1);

    // Missing/corrupt input must not be accepted or mutate the running dictionary.
    std::ofstream(path) << "{";
    assert(!loadDictionary(path, true, &restart));
    assert(!loadDictionary(path + ".missing", true, &restart));
    assert(runtime.catalog().text.at("source") == "newer");

    // Exercise lookup priority and source guards through the real portable API.
    nlohmann::json lookups = {
        {"text", {{"同じ原文", "通用译文"}, {"ブルームピース", "开花碎片"},
                  {"鈴村優", "铃村优"}, {"部分を含む文章", "完整句子"}}},
        {"i18n", {{"context", "不应回退"}, {"generic", "通用键译文"}}},
        {"i18n_by_source", {{"context", {{"同じ原文", "上下文译文"}, {"保留", "保留"}}}}},
        {"legal_by_type", {{"1", {{"source", "同じ原文"}, {"translation", "法务译文"}}}}},
        {"translated_values", {"已翻译的剧情"}}
    };
    write(path, lookups);
    assert(loadDictionary(path));
    assert(*runtime.catalog().resolveI18n("context", "同じ原文") == "上下文译文");
    assert(*runtime.catalog().resolveI18n("context", "保留") == "保留");
    assert(!runtime.catalog().resolveI18n("context", "changed source"));
    assert(*runtime.catalog().resolveI18n("generic", "任意原文") == "通用键译文");
    assert(!runtime.catalog().resolveI18n("unknown", "同じ原文"));
    int hierarchyWalks = 0;
    auto unrelated = [&] { ++hierarchyWalks; return std::string("Unrelated/Name"); };
    assert(runtime.catalog().resolveText("同じ原文", unrelated).text == "法务译文");
    assert(hierarchyWalks == 0);
    assert(runtime.catalog().resolveText("12時間前").text == "12小时前");
    assert(!runtime.catalog().resolveText("あ12時間前").matched);
    assert(runtime.catalog().resolveText("#鈴村優\n#未知").text == "#铃村优\n#未知");
    assert(!runtime.catalog().resolveText("未知ブルームピース").matched);
    auto rendered = runtime.catalog().resolveText("已翻译的剧情");
    assert(rendered.matched && rendered.retainOriginal && rendered.updateFont);
    const std::string shopPath = "WindowSystem/WindowRoot/ShopWindow(Clone)/"
        "NormalShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/Name";
    auto shop = [&] { ++hierarchyWalks; return shopPath; };
    hierarchyWalks = 0;
    assert(runtime.catalog().resolveText("ブルームピース×3,000", shop).text == "开花碎片×3,000");
    assert(hierarchyWalks == 1);
    assert(!runtime.catalog().resolveText("ブルームピース×3,000", unrelated).matched);
    assert(!runtime.catalog().resolveText("ブルームピース×3,000").matched);
    // An exact whole-string decision still wins outside the scoped matcher.
    lookups["text"]["ブルームピース×3,000"] = "独立译文";
    write(path, lookups);
    assert(loadDictionary(path, true, &restart) && !restart);
    assert(runtime.catalog().resolveText("ブルームピース×3,000", shop).text == "开花碎片×3,000");
    assert(runtime.catalog().resolveText("ブルームピース×3,000", unrelated).text == "独立译文");

    // Parse errors late in the file cannot publish any earlier parsed maps.
    invalid = lookups;
    invalid["text"]["同じ原文"] = "不能应用";
    invalid["i18n_by_source"]["context"]["同じ原文"] = 123;
    const auto beforeInvalid = runtime.generation();
    write(path, invalid);
    assert(!loadDictionary(path, true, &restart));
    assert(runtime.catalog().text.at("同じ原文") == "通用译文");
    assert(*runtime.catalog().resolveI18n("context", "同じ原文") == "上下文译文");
    assert(runtime.generation() == beforeInvalid);

    // Source-string fallback must stay suppressed after every successful reload.
    assert(runtime.suppressMasterSources({"同じ原文"}) == 1);
    write(path, lookups);
    assert(loadDictionary(path, true, &restart) && !restart);
    assert(runtime.catalog().text.count("同じ原文") == 0);
    assert(*runtime.catalog().resolveI18n("context", "同じ原文") == "上下文译文");

    if (argc == 4) {
        // Optional real released dictionaries: old startup -> deferred update -> restart.
        assert(loadDictionary(argv[2]));
        const auto oldSize = runtime.catalog().text.size();
        const auto oldDump = runtime.options().dumpEnabled;
        assert(loadDictionary(argv[3], true, &restart) && restart);
        assert(runtime.catalog().text.size() == oldSize && runtime.options().dumpEnabled == oldDump);
        assert(loadDictionary(argv[3]));
        assert(loadDictionary(argv[3], true, &restart) && !restart);
    }
}
