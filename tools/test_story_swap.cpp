#include <cassert>
#include <fstream>
#include <string>
#include "../app/src/main/cpp/story_swap.h"

int main(int argc, char** argv) {
    assert(argc == 2 || argc == 3);
    if (argc == 3) {
        std::ifstream corpus(argv[2]);
        const auto rows = nlohmann::json::parse(corpus);
        for (const auto& row : rows) {
            const auto title = storySwapScriptTitle(row.at("script").get<std::string>());
            assert(!title.character.empty() && !title.title.empty());
            assert(title.episode == "第" + std::to_string(row.at("episode").get<int>()) + "话");
        }
    }
    const std::string path = argv[1];
    const std::string entry = "adv_card_ai_13_01.txt";
    const std::string source = "adv_card_ai_14_02.txt";
    auto write = [&](const std::string& value) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << value;
    };
    assert(storySwapSource(path, entry) == entry);
    write(R"({"schema_version":1,"entry":"adv_card_ai_13_01.txt","source":"adv_card_ai_14_02.txt"})");
    assert(storySwapSource(path, entry) == source);
    assert(storySwapSource(path, "adv_card_ai_13_01_short.txt") ==
           "adv_card_ai_14_02_short.txt");
    assert(storySwapSource(path, source) == source);
    assert(storySwapSource(path, "adv_main_01_01.txt") == "adv_main_01_01.txt");
    write(R"({"schema_version":1,"entry":"adv_card_ai_13_01_short.txt","source":"adv_card_ai_14_02.txt"})");
    assert(storySwapSource(path, "adv_card_ai_13_01_short.txt") ==
           "adv_card_ai_13_01_short.txt");
    write(R"({"schema_version":1,"entry":"adv_card_ai_13_01.txt","source":"adv_card_ai_14_02_short.txt"})");
    assert(storySwapSource(path, entry) == entry);
    write(R"({"schema_version":1,"entry":"adv_card_ai_13_01.txt","source":"adv_card_ai_13_01.txt"})");
    assert(storySwapSource(path, entry) == entry);
    write(R"({"schema_version":1,"entry":"adv_card_ai_13_01.txt","source":"../../dict.json"})");
    assert(storySwapSource(path, entry) == entry);
    write("broken");
    assert(storySwapSource(path, entry) == entry);

    // Each category reads only its own state, never crosses ADV dialects.
    write(R"({"schema_version":2,"entry":"adv_event_2107_01_01.txt","source":"adv_event_2108_01_01.txt","entry_title":"入口","source_title":"源","entry_episode":1,"source_episode":1})");
    assert(storySwapRead(path).entry.empty());
    const auto event = storySwapRead(path, "event");
    assert(event.resolve("adv_event_2107_01_01.txt") == "adv_event_2108_01_01.txt");
    assert(event.resolve("adv_event_2107_01_01_short.txt") == "adv_event_2107_01_01_short.txt");
    assert(storySwapRead(path, "love").entry.empty());
    write(R"({"schema_version":2,"entry":"adv_love_2305_01_01.txt","source":"adv_love_2603_08_03.txt","entry_title":"入口","source_title":"源","entry_episode":1,"source_episode":8})");
    const auto love = storySwapRead(path, "love");
    assert(love.resolve("adv_love_2305_01_01.txt") == "adv_love_2603_08_03.txt");
    assert(storySwapRead(path, "event").entry.empty());
    assert(storySwapKind("adv_love_2305_01_01.txt") == "love");
    assert(storySwapKind("love-story-23-0514-001").empty());
    assert(storySwapKind("adv_event_../x.txt").empty());
    assert(storySwapKind("adv_love_2305_01_01_short.txt").empty());
    write(R"({"schema_version":1,"entry":"adv_love_2305_01_01.txt","source":"adv_event_2107_01_01.txt"})");
    assert(storySwapRead(path, "love").entry.empty());
    const std::string branchScript = "[title]\n[choicegroup choices=[choice text=选项]]\n[branchgroup type=Choice groupLength=1]\n[branch groupLength=1]\n[message text=你好]\n[timeline]\r\n";
    assert(storySwapScriptCompatible(branchScript, "love"));
    assert(!storySwapScriptCompatible(branchScript, "event"));
    assert(storySwapScriptCompatible("[title]\n[message text=你好]\n[timeline]", "event"));
    assert(!storySwapScriptCompatible("broken", "love"));
    assert(!storySwapScriptCompatible("[title]\n[message text=truncated]", "event"));
    const auto loveTitles = storySwapPlaybackTitles(love, love.entry, love.source, branchScript, branchScript);
    assert(loveTitles.ready() && loveTitles.catalogOnly);
    const std::string conditionalScript = "[title]\n[branchgroup type=Condition groupLength=2]\n[branch groupLength=1]\n[message text=条件成立]\n[branch]\n[timeline]";
    assert(storySwapScriptCompatible(conditionalScript, "love"));
    assert(!storySwapScriptCompatible(conditionalScript, "event"));
    assert(storySwapPlaybackTitles(love, love.entry, love.source, branchScript, conditionalScript).ready());
    const std::string choices = "[choicegroup choices=[choice text=甲] choices=[choice text=乙]]\n";
    const std::string oneChoiceGroup = "[title]\n" + choices + "[timeline]";
    const std::string twoChoiceGroups = "[title]\n" + choices + choices + "[timeline]";
    assert(!storySwapPairCompatible(twoChoiceGroups, oneChoiceGroup, "love"));
    assert(!storySwapPairCompatible(oneChoiceGroup, twoChoiceGroups, "love"));
    assert(storySwapPairCompatible(oneChoiceGroup, "[title]\n" + choices +
        "[branchgroup type=Condition groupLength=2]\n[branch]\n[branch]\n[timeline]", "love"));
    assert(!storySwapPairCompatible(oneChoiceGroup,
        "[title]\n[choicegroup choices=[choice text=甲]]\n[timeline]", "love"));

    const std::string root = "WindowSystem/WindowRoot/StoryWindow(Clone)/StoryPlayerScreen(Clone)/";
    const std::string header = root + "ScreenCanvas/ScreenRoot/Header/Title";
    assert(storySwapTitleReplacement(loveTitles, header, "入口") == "源");
    assert(storySwapTitleReplacement(loveTitles, header, "第1话 入口") == "第8话 源");
    assert(storySwapTitleReplacement(loveTitles, header, "无关标题").empty());
    const std::string small = root + "ScreenCanvas/ScreenRoot/Header/SmallTitle";
    const std::string loading = root + "StoryFullScreenLoading/Root/Layout/";
    const std::string entryScript = "[title title=长濑琴乃 第1话 脚本入口标题]\n";
    const std::string sourceScript = "[title title=白石千纱 第2话 嗡嗡嗡]\n";
    write(R"({"schema_version":2,"entry":"adv_card_ai_13_01.txt","source":"adv_card_ai_14_02.txt","entry_title":"这次是\\n水上摩托\\n啊","source_title":"嗡嗡\\n嗡——","entry_episode":1,"source_episode":2})");
    const auto selection = storySwapRead(path);
    assert(selection.resolve(entry) == source);
    const auto titles = storySwapPlaybackTitles(selection, entry, source, entryScript, sourceScript);
    assert(titles.ready());
    assert(storySwapTitleReplacement(titles, header, "第1话 这次是水上摩托啊") == "第2话 嗡嗡嗡——");
    assert(storySwapTitleReplacement(titles, header, "第1话 脚本入口标题") == "第2话 嗡嗡嗡——");
    assert(storySwapTitleReplacement(titles, small, "长濑琴乃") == "白石千纱");
    assert(storySwapTitleReplacement(titles, loading + "SmallTitle", "长濑琴乃") == "白石千纱");
    assert(storySwapTitleReplacement(titles, loading + "EpisodeText", "第1话") == "第2话");
    assert(storySwapTitleReplacement(titles, loading + "Title", "这次是\n水上摩托\n啊") == "嗡嗡\n嗡——");
    assert(storySwapTitleReplacement(titles, header, "第2话 这次是水上摩托啊").empty());
    assert(storySwapTitleReplacement(titles, header, "前缀第1话 这次是水上摩托啊").empty());
    assert(storySwapTitleReplacement(titles, "Other/" + header, "第1话 这次是水上摩托啊").empty());
    assert(storySwapTitleReplacement(titles, root + "Profile/SmallTitle", "长濑琴乃").empty());
    assert(storySwapTitleReplacement(titles, small, "其他角色").empty());
    // Failed/fallback loads and unrelated requests cannot activate metadata.
    assert(!storySwapPlaybackTitles(selection, entry, entry, entryScript, sourceScript).ready());
    assert(!storySwapPlaybackTitles(selection, "adv_main_01.txt", source, entryScript, sourceScript).ready());
    assert(!storySwapPlaybackTitles(selection, entry, source, entryScript, "broken").ready());
    assert(!storySwapPlaybackTitles(selection, entry, source, sourceScript, sourceScript).ready());
    assert(storySwapScriptTitle("[title title=天动瑠依 第2话『十七岁的女孩』模式]\n").title == "『十七岁的女孩』模式");
    assert(storySwapScriptTitle("dialogue [title title=长濑琴乃 第1话 标题]\n").title.empty());
    assert(storySwapScriptTitle(entryScript + entryScript).title.empty());
    assert(storySwapScriptTitle("[title title=长濑琴乃 第0话 标题]\n").title.empty());
    assert(storySwapScriptTitle("[title title=长濑琴乃 第1话 标题\n正文]").title.empty());
    // Legacy aliases remain readable and use their exact script metadata.
    write(R"({"schema_version":1,"entry":"adv_card_ai_13_01.txt","source":"adv_card_ai_14_02.txt"})");
    const auto legacy = storySwapPlaybackTitles(storySwapRead(path), entry, source, entryScript, sourceScript);
    assert(storySwapTitleReplacement(legacy, header, "第1话 脚本入口标题") == "第2话 嗡嗡嗡");
    write(R"({"schema_version":2,"entry":"adv_card_ai_13_01.txt","source":"adv_card_ai_14_02.txt","entry_title":"one","source_title":"two","entry_episode":0,"source_episode":2})");
    assert(storySwapSource(path, entry) == entry);
}
