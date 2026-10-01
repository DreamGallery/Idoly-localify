#pragma once

#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include "json.hpp"

// Independent directed aliases, bounded to one script family. Invalid state never
// changes the resource selected by the game.
inline bool storySwapCardName(const std::string& name) {
    constexpr const char* prefix = "adv_card_";
    if (name.compare(0, 9, prefix) != 0 || name.size() <= 13 ||
        name.compare(name.size() - 4, 4, ".txt") != 0) return false;
    for (size_t i = 9; i < name.size() - 4; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

inline bool storySwapBaseName(const std::string& name) {
    return storySwapCardName(name) &&
        (name.size() < 10 || name.compare(name.size() - 10, 10, "_short.txt") != 0);
}

inline bool storySwapKindName(const std::string& name, const std::string& kind) {
    if (kind == "card") return storySwapBaseName(name);
    if (kind != "event" && kind != "love") return false;
    const std::string prefix = "adv_" + kind + "_";
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    // Reuse the strict ASCII basename validation without admitting other families.
    return storySwapBaseName("adv_card_" + name.substr(prefix.size()));
}

inline std::string storySwapKind(const std::string& name) {
    for (const auto* kind : {"card", "event", "love"})
        if (storySwapKindName(name, kind) || (std::string(kind) == "card" && storySwapCardName(name)))
            return kind;
    return {};
}

// Event/love replacements stay complete ADV scripts. Love-specific choice and
// branch commands must remain untouched; title parsing is deliberately card-only.
inline bool storySwapScriptCompatible(const std::string& script, const std::string& kind) {
    if (kind == "card") return true; // Preserve the existing card behavior.
    if (kind != "event" && kind != "love") return false;
    if (script.empty() || script.front() != '[' || script.find('\0') != std::string::npos)
        return false;
    const auto end = script.find_last_not_of(" \r\n\t");
    if (end == std::string::npos || end < 9 || script.substr(end - 9, 10) != "[timeline]") return false;
    // Love Condition branches stay intact. The game's original Love.Start /
    // Love.Restart branch selection is preserved; this only aliases the TXT.
    if (kind == "event" && (script.find("[choicegroup") != std::string::npos ||
                           script.find("[branchgroup") != std::string::npos)) return false;
    return true;
}

inline std::vector<int> storySwapChoiceShape(const std::string& script) {
    std::vector<int> shape;
    std::istringstream input(script);
    std::string line;
    while (std::getline(input, line)) {
        if (line.compare(0, 13, "[choicegroup ") != 0) continue;
        int count = 0;
        size_t pos = 0;
        while ((pos = line.find("choices=[choice ", pos)) != std::string::npos) {
            ++count;
            pos += 16;
        }
        shape.push_back(count);
    }
    return shape;
}

inline bool storySwapPairCompatible(const std::string& entry, const std::string& source,
                                   const std::string& kind) {
    if (!storySwapScriptCompatible(entry, kind) || !storySwapScriptCompatible(source, kind))
        return false;
    // Love's API provides choice metadata for the original entry. A different
    // group/option count can crash SetupLoveAdvEngineChoiceGroup before playback.
    return kind != "love" || storySwapChoiceShape(entry) == storySwapChoiceShape(source);
}

struct StorySwapSelection {
    std::string entry, source, entryTitle, sourceTitle;
    int entryEpisode = 0, sourceEpisode = 0;
    std::string resolve(const std::string& requested) const {
        if (entry.empty()) return requested;
        if (requested == entry) return source;
        if (storySwapCardName(entry) && requested == entry.substr(0, entry.size() - 4) + "_short.txt")
            return source.substr(0, source.size() - 4) + "_short.txt";
        return requested;
    }
};

inline StorySwapSelection storySwapRead(const std::string& path, const std::string& kind = "card") {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0 || input.tellg() > 4096) return {};
        input.seekg(0);
        const auto state = nlohmann::json::parse(input);
        if (!state.is_object() || !state.at("entry").is_string() ||
            !state.at("source").is_string()) return {};
        StorySwapSelection value;
        value.entry = state.at("entry").get<std::string>();
        value.source = state.at("source").get<std::string>();
        if (!storySwapKindName(value.entry, kind) || !storySwapKindName(value.source, kind) ||
            value.entry == value.source) return {};
        if (state.at("schema_version") == 1 && state.size() == 3) return value;
        if (state.at("schema_version") != 2 || state.size() != 7 ||
            !state.at("entry_title").is_string() || !state.at("source_title").is_string() ||
            !state.at("entry_episode").is_number_integer() ||
            !state.at("source_episode").is_number_integer()) return {};
        value.entryTitle = state.at("entry_title").get<std::string>();
        value.sourceTitle = state.at("source_title").get<std::string>();
        value.entryEpisode = state.at("entry_episode").get<int>();
        value.sourceEpisode = state.at("source_episode").get<int>();
        for (const auto* title : {&value.entryTitle, &value.sourceTitle})
            if (title->empty() || title->size() > 1024 || title->find('\0') != std::string::npos)
                return {};
        if (value.entryEpisode < 1 || value.entryEpisode > 20 ||
            value.sourceEpisode < 1 || value.sourceEpisode > 20) return {};
        return value;
    } catch (...) { return {}; }
}

inline std::string storySwapSource(const std::string& path, const std::string& requested) {
    return storySwapCardName(requested) ? storySwapRead(path).resolve(requested) : requested;
}

inline std::string storySwapTitleLines(std::string value, bool flat = false) {
    for (size_t pos = 0; (pos = value.find("\\n", pos)) != std::string::npos;)
        value.replace(pos, 2, "\n");
    std::string result;
    for (char c : value) if (c != '\r' && (!flat || c != '\n')) result += c;
    return result;
}

inline std::string storySwapTitleKey(const std::string& value) {
    std::string result;
    for (char c : storySwapTitleLines(value, true)) if (c != ' ' && c != '\t') result += c;
    return result;
}

struct StorySwapTitle {
    std::string character, episode, title;
};

// Compiled card scripts have one bounded [title] line. Accept the observed
// missing space after 第N话, but never guess metadata from arbitrary dialogue.
inline StorySwapTitle storySwapScriptTitle(const std::string& script) {
    constexpr const char* prefix = "[title title=";
    const auto pos = script.find(prefix);
    if (pos == std::string::npos || (pos && script[pos - 1] != '\n')) return {};
    const auto end = script.find(']', pos);
    if (end == std::string::npos || end - pos > 1024 ||
        script.find('\n', pos) < end || script.find(prefix, end) != std::string::npos) return {};
    const auto value = script.substr(pos + 13, end - pos - 13);
    const auto episodeStart = value.find(" 第");
    if (episodeStart == std::string::npos || episodeStart == 0) return {};
    const auto numberStart = episodeStart + std::string(" 第").size();
    const auto numberEnd = value.find("话", numberStart);
    if (numberEnd == std::string::npos || numberEnd == numberStart || numberEnd - numberStart > 2) return {};
    int episode = 0;
    for (size_t i = numberStart; i < numberEnd; ++i) {
        if (value[i] < '0' || value[i] > '9') return {};
        episode = episode * 10 + value[i] - '0';
    }
    if (episode < 1 || episode > 20) return {};
    size_t titleStart = numberEnd + std::string("话").size();
    while (titleStart < value.size() && value[titleStart] == ' ') ++titleStart;
    if (titleStart == value.size()) return {};
    return {value.substr(0, episodeStart), "第" + std::to_string(episode) + "话", value.substr(titleStart)};
}

struct StorySwapPlaybackTitles {
    StorySwapTitle entry, source;
    std::string entryCatalogTitle, sourceCatalogTitle;
    bool catalogOnly = false;
    bool ready() const { return !entry.title.empty() && !source.title.empty(); }
};

inline StorySwapPlaybackTitles storySwapPlaybackTitles(
        const StorySwapSelection& selection, const std::string& requested,
        const std::string& loaded, const std::string& entryScript, const std::string& sourceScript) {
    if (requested == loaded || selection.resolve(requested) != loaded) return {};
    if (!storySwapCardName(requested)) {
        // Event scripts commonly contain only [title]. Their reviewed metadata
        // supplies playback labels; no character or introduction is inferred.
        if (selection.entryTitle.empty() || selection.sourceTitle.empty()) return {};
        return {{"", "第" + std::to_string(selection.entryEpisode) + "话", selection.entryTitle},
                {"", "第" + std::to_string(selection.sourceEpisode) + "话", selection.sourceTitle},
                selection.entryTitle, selection.sourceTitle, true};
    }
    StorySwapPlaybackTitles result{storySwapScriptTitle(entryScript),
                                  storySwapScriptTitle(sourceScript), {}, {}};
    if (!result.ready()) return {};
    if (selection.entryEpisode && (result.entry.episode != "第" + std::to_string(selection.entryEpisode) + "话" ||
        result.source.episode != "第" + std::to_string(selection.sourceEpisode) + "话")) return {};
    result.entryCatalogTitle = selection.entryTitle;
    result.sourceCatalogTitle = selection.sourceTitle;
    return result;
}

inline int storySwapTitleRole(const std::string& path) {
    const std::string root = "WindowSystem/WindowRoot/StoryWindow(Clone)/StoryPlayerScreen(Clone)/";
    if (path == root + "ScreenCanvas/ScreenRoot/Header/Title") return 1;
    if (path == root + "ScreenCanvas/ScreenRoot/Header/SmallTitle" ||
        path == root + "StoryFullScreenLoading/Root/Layout/SmallTitle") return 2;
    if (path == root + "StoryFullScreenLoading/Root/Layout/EpisodeText") return 3;
    if (path == root + "StoryFullScreenLoading/Root/Layout/Title") return 4;
    return 0;
}

inline std::string storySwapTitleReplacement(const StorySwapPlaybackTitles& titles,
                                             const std::string& path, const std::string& current) {
    if (!titles.ready()) return {};
    const int role = storySwapTitleRole(path);
    if (!role) return {};
    const std::string key = storySwapTitleKey(current);
    if (role == 2) return !titles.catalogOnly && key == storySwapTitleKey(titles.entry.character) ? titles.source.character : "";
    if (role == 3) return key == titles.entry.episode ? titles.source.episode : "";
    if (titles.catalogOnly && key == storySwapTitleKey(titles.entryCatalogTitle))
        return storySwapTitleLines(titles.sourceCatalogTitle, role == 1);
    const std::string prefix = role == 1 ? titles.entry.episode : "";
    if (key != storySwapTitleKey(prefix + titles.entry.title) &&
        (titles.entryCatalogTitle.empty() || key != storySwapTitleKey(prefix + titles.entryCatalogTitle))) return {};
    const auto title = titles.sourceCatalogTitle.empty() ? titles.source.title : titles.sourceCatalogTitle;
    return role == 1 ? titles.source.episode + " " + storySwapTitleLines(title, true) : storySwapTitleLines(title);
}
