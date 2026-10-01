#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class MasterBlobMatcher {
public:
    struct Entry {
        std::vector<uint8_t> original;
        std::vector<uint8_t> translated;
    };

    size_t load(const std::string& path) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Master Blob patch file is unavailable");
        const auto size = input.tellg();
        if (size < 12 || size > 256 * 1024 * 1024)
            throw std::runtime_error("Master Blob patch file has invalid size");
        input.seekg(0);
        char magic[8];
        read(input, magic, sizeof(magic));
        const bool version2 = std::memcmp(magic, "IDOLYMB2", sizeof(magic)) == 0;
        if (!version2 && std::memcmp(magic, "IDOLYMB1", sizeof(magic)) != 0)
            throw std::runtime_error("Master Blob patch file has invalid format");
        const uint32_t count = read32(input);
        const uint32_t suppressedCount = version2 ? read32(input) : 0;
        if (count > 300000) throw std::runtime_error("Too many Master Blob patches");
        if (suppressedCount > 4096)
            throw std::runtime_error("Too many suppressed MasterDB sources");
        MasterBlobMatcher next;
        for (uint32_t index = 0; index < count; ++index) {
            const uint32_t originalSize = read32(input);
            const uint32_t translatedSize = read32(input);
            if (!originalSize || !translatedSize || originalSize > 8 * 1024 * 1024 ||
                translatedSize > 8 * 1024 * 1024)
                throw std::runtime_error("Master Blob patch entry has invalid size");
            Entry entry;
            entry.original.resize(originalSize);
            entry.translated.resize(translatedSize);
            read(input, reinterpret_cast<char*>(entry.original.data()), originalSize);
            read(input, reinterpret_cast<char*>(entry.translated.data()), translatedSize);
            auto& bucket = next.buckets_[hash(entry.original.data(), entry.original.size())];
            for (const auto& prior : bucket) {
                if (prior.original == entry.original)
                    throw std::runtime_error("Duplicate Master Blob patch");
            }
            bucket.push_back(std::move(entry));
        }
        for (uint32_t index = 0; index < suppressedCount; ++index) {
            const uint32_t length = read32(input);
            if (!length || length > 4096)
                throw std::runtime_error("Invalid suppressed MasterDB source length");
            std::string source(length, '\0');
            read(input, &source[0], length);
            next.suppressedSources_.push_back(std::move(source));
        }
        if (input.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("Trailing bytes in Master Blob patch file");
        buckets_.swap(next.buckets_);
        suppressedSources_.swap(next.suppressedSources_);
        count_ = count;
        return count_;
    }

    const std::vector<uint8_t>* match(const uint8_t* original, size_t size) const {
        if (!original || !size) return nullptr;
        const auto found = buckets_.find(hash(original, size));
        if (found == buckets_.end()) return nullptr;
        for (const auto& entry : found->second) {
            if (entry.original.size() == size &&
                std::memcmp(entry.original.data(), original, size) == 0)
                return &entry.translated;
        }
        return nullptr;
    }

    size_t size() const { return count_; }
    const std::vector<std::string>& suppressedSources() const { return suppressedSources_; }

private:
    static void read(std::ifstream& input, char* buffer, size_t length) {
        if (!input.read(buffer, static_cast<std::streamsize>(length)))
            throw std::runtime_error("Truncated Master Blob patch file");
    }

    static uint32_t read32(std::ifstream& input) {
        uint8_t bytes[4];
        read(input, reinterpret_cast<char*>(bytes), sizeof(bytes));
        return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
            (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    }

    static uint64_t hash(const uint8_t* data, size_t size) {
        uint64_t value = UINT64_C(14695981039346656037);
        for (size_t i = 0; i < size; ++i) {
            value ^= data[i];
            value *= UINT64_C(1099511628211);
        }
        return value;
    }

    std::unordered_map<uint64_t, std::vector<Entry>> buckets_;
    std::vector<std::string> suppressedSources_;
    size_t count_ = 0;
};
