#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Adventure text replaces {user} before it reaches TMP_Text. Keep the font
// choice for those translated lines without assuming or storing the account's
// name. The surrounding literal must be long enough to avoid broad matches
// such as "{user}！" against unrelated UI text.
class UserTemplateMatcher {
public:
    void add(const std::string& value) {
        constexpr std::size_t tokenLength = 6; // strlen("{user}")
        const auto first = value.find("{user}");
        if (first == std::string::npos) return;
        const auto second = value.find("{user}", first + tokenLength);
        if (second == std::string::npos) {
            const std::string prefix = value.substr(0, first);
            const std::string suffix = value.substr(first + tokenLength);
            if (prefix.size() + suffix.size() < minLiteralBytes) return;
            if (prefix.empty()) {
                std::size_t node = 0;
                for (auto it = suffix.rbegin(); it != suffix.rend(); ++it)
                    node = append(suffixNodes, node, static_cast<unsigned char>(*it));
                suffixNodes[node].terminal = true;
            } else {
                std::size_t node = 0;
                for (unsigned char byte : prefix)
                    node = append(prefixNodes, node, byte);
                prefixNodes[node].suffixes.push_back(suffix);
            }
            ++count_;
            return;
        }
        if (value.find("{user}", second + tokenLength) != std::string::npos) return;
        Repeated repeated{value.substr(0, first),
                          value.substr(first + tokenLength,
                                       second - first - tokenLength),
                          value.substr(second + tokenLength)};
        if (repeated.first.size() + repeated.middle.size() + repeated.last.size()
                < minLiteralBytes) return;
        repeats.push_back(std::move(repeated));
        ++count_;
    }

    bool matches(const std::string& value) const {
        std::size_t node = 0;
        for (std::size_t index = 0; index < value.size(); ++index) {
            const auto found = prefixNodes[node].children.find(
                static_cast<unsigned char>(value[index]));
            if (found == prefixNodes[node].children.end()) break;
            node = found->second;
            for (const auto& suffix : prefixNodes[node].suffixes) {
                const std::size_t literalBytes = index + 1 + suffix.size();
                if (value.size() > literalBytes &&
                    value.size() - literalBytes <= maxUsernameBytes &&
                    value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0)
                    return true;
            }
        }
        node = 0;
        for (std::size_t length = 1; length < value.size(); ++length) {
            const auto found = suffixNodes[node].children.find(
                static_cast<unsigned char>(value[value.size() - length]));
            if (found == suffixNodes[node].children.end()) break;
            node = found->second;
            if (suffixNodes[node].terminal &&
                value.size() - length <= maxUsernameBytes)
                return true;
        }
        for (const auto& pattern : repeats) {
            const std::size_t fixed = pattern.first.size() + pattern.middle.size() +
                                      pattern.last.size();
            if (value.size() <= fixed || (value.size() - fixed) % 2 != 0) continue;
            const std::size_t usernameBytes = (value.size() - fixed) / 2;
            if (!usernameBytes || usernameBytes > maxUsernameBytes ||
                value.compare(0, pattern.first.size(), pattern.first) != 0 ||
                value.compare(value.size() - pattern.last.size(), pattern.last.size(),
                              pattern.last) != 0) continue;
            const std::size_t first = pattern.first.size();
            if (value.compare(first + usernameBytes, pattern.middle.size(),
                              pattern.middle) == 0 &&
                value.compare(first + usernameBytes + pattern.middle.size(),
                              usernameBytes, value, first, usernameBytes) == 0)
                return true;
        }
        return false;
    }

    std::size_t size() const { return count_; }

private:
    static constexpr std::size_t minLiteralBytes = 12;
    static constexpr std::size_t maxUsernameBytes = 128;
    struct Node {
        std::unordered_map<unsigned char, std::size_t> children;
        std::vector<std::string> suffixes;
        bool terminal = false;
    };
    struct Repeated { std::string first, middle, last; };
    static std::size_t append(std::vector<Node>& nodes, std::size_t node,
                              unsigned char byte) {
        const auto found = nodes[node].children.find(byte);
        if (found != nodes[node].children.end()) return found->second;
        const std::size_t next = nodes.size();
        nodes[node].children.emplace(byte, next);
        nodes.emplace_back();
        return next;
    }
    std::vector<Node> prefixNodes{1};
    std::vector<Node> suffixNodes{1};
    std::vector<Repeated> repeats;
    std::size_t count_ = 0;
};
