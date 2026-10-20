#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "home_activity_name_matcher.h"
#include "numeric_text_template_matcher.h"
#include "user_template_matcher.h"

namespace localify {

enum class FontMode { Fallback, ReplaceTranslated, ReplaceAll, PreserveNonCjk, ReplaceUi };

struct RuntimeOptions {
    bool dumpEnabled = false;
    bool resourceReplace = true;
    bool assetReplace = true;
    bool captureOcto = false;
    bool probeMaster = false;
    bool fontEnabled = false;
    FontMode fontMode = FontMode::Fallback;

    bool operator==(const RuntimeOptions& other) const;
};

struct TextDecision {
    bool matched = false;
    std::string text;
    bool updateFont = false;
    bool retainOriginal = false;
    bool playerTemplate = false;
};

// Owns validated data and pure lookup rules. It knows no Unity objects, JNI,
// filesystem layout, or UI mutation APIs. The runtime exposes it read-only.
struct DictionaryCatalog {
    using TextMap = std::unordered_map<std::string, std::string>;
    TextMap text, legal, i18n;
    std::unordered_map<std::string, TextMap> i18nBySource;
    std::unordered_set<std::string> translatedValues;
    UserTemplateMatcher userTemplates;
    NumericTextTemplateMatcher numericTemplates;
    HomeActivityNameMatcher homeActivityNames;
    RuntimeOptions options;
    size_t variantCount = 0;

    static DictionaryCatalog read(const std::string& path, bool reload);
    // A key with source variants never falls through to a context-free value.
    const std::string* resolveI18n(const std::string& key, const std::string& source) const;
    // The hierarchy is evaluated lazily, only for a scoped candidate.
    TextDecision resolveText(const std::string& source,
                             const std::function<std::string()>& objectPath = {}) const;

private:
    std::string nameTags(const std::string& source) const;
};

enum class DictionaryUpdate { Applied, RestartRequired };

class DictionaryRuntime {
public:
    // Unity hooks may nest. A caller holds this lock for the entire operation
    // that uses catalog references, including nested setters and font routing.
    std::recursive_mutex& frameMutex() { return mutex_; }
    const DictionaryCatalog& catalog() const { return catalog_; }
    // Options are fixed for the running process; hot reload cannot change them.
    const RuntimeOptions& options() const { return options_; }
    uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

    // Parse/validate off-lock, then publish all maps together. Throws on invalid
    // input; invalid/deferred updates leave every live value and generation intact.
    DictionaryUpdate load(const std::string& path, bool reload = false,
                          bool deferApply = false, bool allowDeferred = true);
    // Called at startup after the MasterDB matcher is loaded. Suppression also
    // applies to every subsequent dictionary reload.
    size_t suppressMasterSources(const std::vector<std::string>& sources);

private:
    std::recursive_mutex mutex_;
    DictionaryCatalog catalog_;
    RuntimeOptions options_;
    std::vector<std::string> suppressedSources_;
    std::atomic<uint64_t> generation_{1};
};

} // namespace localify
