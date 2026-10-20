#include "dictionary.h"

#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace localify {

static_assert(std::is_nothrow_swappable_v<DictionaryCatalog>,
              "Publishing a validated dictionary must not throw");

bool RuntimeOptions::operator==(const RuntimeOptions& other) const {
    return std::tie(dumpEnabled, resourceReplace, assetReplace, captureOcto,
                    probeMaster, fontEnabled, fontMode) ==
           std::tie(other.dumpEnabled, other.resourceReplace, other.assetReplace,
                    other.captureOcto, other.probeMaster, other.fontEnabled, other.fontMode);
}

DictionaryUpdate DictionaryRuntime::load(const std::string& path, bool reload,
                                          bool deferApply, bool allowDeferred) {
    auto next = DictionaryCatalog::read(path, reload);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (reload && (deferApply || !(next.options == options_))) {
        if (!allowDeferred)
            throw std::runtime_error("runtime configuration changes require restarting the game");
        return DictionaryUpdate::RestartRequired;
    }
    for (const auto& source : suppressedSources_) next.text.erase(source);
    if (!reload) options_ = next.options;
    // Old maps are destroyed with next after the frame lock has been released.
    std::swap(catalog_, next);
    if (reload) generation_.fetch_add(1, std::memory_order_release);
    return DictionaryUpdate::Applied;
}

size_t DictionaryRuntime::suppressMasterSources(const std::vector<std::string>& sources) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    suppressedSources_ = sources;
    size_t removed = 0;
    for (const auto& source : sources) removed += catalog_.text.erase(source);
    return removed;
}

} // namespace localify
