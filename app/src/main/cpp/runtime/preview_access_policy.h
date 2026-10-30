#pragma once

#include <atomic>
#include <cstdint>
#include "preview_access.h"

namespace localify::preview_access {
class Policy {
public:
    // Unsupported categories and partially installed adapters remain disabled.
    bool set(uint32_t category, bool value) {
        if (!category || (category & (category - 1)) || !(ready.load() & category)) return false;
        if (value) enabled.fetch_or(category);
        else enabled.fetch_and(~category);
        generation.fetch_add(1);
        return true;
    }
    bool allows(uint32_t category, uint32_t scope, bool scoped) const {
        return (ready.load() & enabled.load() & category) && (!scoped || (scope & category));
    }
    std::atomic<uint32_t> ready{0}, enabled{0};
    std::atomic<uint64_t> generation{1};
};

class Scope {
public:
    Scope(uint32_t& value, uint32_t category) : target(value), previous(value) { target |= category; }
    ~Scope() { target = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    uint32_t& target;
    uint32_t previous;
};

// An ordinary editor opened before enabling previews has no detached model.
// While wardrobe previews are enabled it must ask for a reopen, not submit an
// account request. An already isolated page keeps its barrier after disabling.
inline bool canForwardWardrobeWrite(bool isolatedPage, uint32_t enabled) {
    return !isolatedPage && !(enabled & WardrobeCategories);
}

inline bool canForwardDecorationWrite(bool isolatedPage, uint32_t enabled) {
    return !isolatedPage && !(enabled & Decorations);
}

// Both seconds and milliseconds occur in game metadata. Reject unknown units
// instead of exposing future content after an unexpected schema change.
inline bool released(int64_t timestamp, int64_t nowSeconds) {
    if (timestamp == 0) return true;
    if (timestamp < 1000000000LL) return false;
    if (timestamp < 100000000000LL) return timestamp <= nowSeconds;
    if (timestamp >= 1000000000000LL && timestamp < 100000000000000LL)
        return timestamp / 1000 <= nowSeconds;
    return false;
}
}
