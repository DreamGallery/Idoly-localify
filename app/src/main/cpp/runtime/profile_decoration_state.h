#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace localify::preview_access {
// An empty ID is a deliberate local choice of no decoration, not the absence
// of an override. The account is an in-memory loading epoch, never a user ID.
class DecorationOverride {
public:
    bool set(uint64_t account, const std::string& id) {
        if (!account || id.size() > 256) return false;
        for (unsigned char c : id) if (c < 0x21 || c > 0x7e) return false;
        value = Value{account, id};
        return true;
    }
    std::optional<std::string> get(uint64_t account, bool enabled) const {
        if (!enabled || !value || value->account != account) return std::nullopt;
        return value->id;
    }
    void clear() { value.reset(); ++epoch; }
    uint64_t revision() const { return epoch; }
private:
    struct Value { uint64_t account; std::string id; };
    std::optional<Value> value;
    uint64_t epoch = 1;
};

inline bool currentDecorationSession(bool enabled, uint64_t account, uint64_t currentAccount,
                                     uint64_t generation, uint64_t currentGeneration,
                                     uint64_t revision, uint64_t currentRevision) {
    return enabled && account == currentAccount && generation == currentGeneration &&
        revision == currentRevision;
}
}
