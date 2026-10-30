#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include "preview_access.h"

namespace localify::preview_access {

struct Appearance {
    std::string costume, hair;
    uint32_t categories = 0;
};

// The caller holds the preview module mutex. These are render choices, never
// ownership records. Account identity stays in memory and is not logged.
class AppearanceOverrides {
public:
    using Key = std::tuple<std::string, std::string, bool>;
    bool put(const std::string& account, const std::string& character, bool live,
             Appearance appearance) {
        if (account.empty() || character.empty() || appearance.costume.empty() ||
            appearance.hair.empty() || !(appearance.categories & WardrobeCategories) ||
            (appearance.categories & ~WardrobeCategories))
            return false;
        values[{account, character, live}] = std::move(appearance);
        return true;
    }
    std::optional<Appearance> get(const std::string& account, const std::string& character,
                                  bool live, uint32_t enabled) const {
        auto entry = values.find({account, character, live});
        if (entry == values.end() || (entry->second.categories & enabled) != entry->second.categories)
            return std::nullopt;
        return entry->second;
    }
    void disable(uint32_t category) {
        for (auto it = values.begin(); it != values.end();) {
            if (it->second.categories & category) it = values.erase(it);
            else ++it;
        }
        ++epoch;
    }
    void clear() { values.clear(); ++epoch; }
    size_t size() const { return values.size(); }
    uint64_t revision() const { return epoch; }
private:
    std::map<Key, Appearance> values;
    uint64_t epoch = 1;
};

// Closing a toggle or restoring appearances must not turn an already expanded
// wardrobe back into an account-writing page. Its write barrier lasts for the
// model's lifetime; stale selections cannot create a new override either.
inline bool canApplyAppearance(uint32_t capturedCategories, uint32_t enabled,
                               uint64_t capturedGeneration, uint64_t generation,
                               uint64_t capturedRevision, uint64_t revision) {
    return capturedCategories && (capturedCategories & ~WardrobeCategories) == 0 &&
        (capturedCategories & enabled) == capturedCategories &&
        capturedGeneration == generation && capturedRevision == revision;
}
}
