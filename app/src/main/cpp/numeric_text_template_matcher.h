#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A reviewed exact UI sentence may contain one ASCII decimal number. This
// matcher neither accepts player names nor interprets arbitrary placeholders.
class NumericTextTemplateMatcher {
public:
    static constexpr std::string_view token = "{number}";

    bool add(const std::string& source, const std::string& translated) {
        Pattern value;
        if (!split(source, value.sourcePrefix, value.sourceSuffix) ||
            !split(translated, value.targetPrefix, value.targetSuffix)) return false;
        if (value.sourcePrefix.size() + value.sourceSuffix.size() < 12 ||
            source.size() > 256 || translated.size() > 256) return false;
        for (const auto& previous : patterns)
            if (previous.sourcePrefix == value.sourcePrefix &&
                previous.sourceSuffix == value.sourceSuffix) return false;
        patterns.push_back(std::move(value));
        return true;
    }

    std::string translate(std::string_view original) const {
        const Pattern* matched = nullptr;
        std::string_view number;
        for (const auto& pattern : patterns) {
            std::string_view candidate;
            if (!matches(original, pattern.sourcePrefix, pattern.sourceSuffix,
                         candidate)) continue;
            if (matched) return {}; // Ambiguous templates do not translate.
            matched = &pattern;
            number = candidate;
        }
        if (!matched) return {};
        std::string output = matched->targetPrefix;
        output.append(number);
        output.append(matched->targetSuffix);
        return output;
    }

    bool isRendered(std::string_view value) const {
        for (const auto& pattern : patterns) {
            std::string_view number;
            if (matches(value, pattern.targetPrefix, pattern.targetSuffix, number))
                return true;
        }
        return false;
    }

    std::size_t size() const { return patterns.size(); }

private:
    struct Pattern {
        std::string sourcePrefix, sourceSuffix, targetPrefix, targetSuffix;
    };

    static bool split(const std::string& text, std::string& prefix, std::string& suffix) {
        const auto first = text.find(token);
        if (first == std::string::npos || text.find(token, first + token.size()) !=
            std::string::npos || text.find('\0') != std::string::npos) return false;
        prefix = text.substr(0, first);
        suffix = text.substr(first + token.size());
        // No other token syntax is allowed in this narrowly scoped matcher.
        return prefix.find_first_of("{}") == std::string::npos &&
               suffix.find_first_of("{}") == std::string::npos;
    }

    static bool matches(std::string_view value, const std::string& prefix,
                        const std::string& suffix, std::string_view& number) {
        if (value.size() < prefix.size() + suffix.size() + 1 ||
            value.substr(0, prefix.size()) != prefix ||
            value.substr(value.size() - suffix.size()) != suffix) return false;
        number = value.substr(prefix.size(), value.size() - prefix.size() - suffix.size());
        if (number.empty() || number.size() > 6) return false;
        for (char digit : number)
            if (digit < '0' || digit > '9') return false;
        return true;
    }

    std::vector<Pattern> patterns;
};
