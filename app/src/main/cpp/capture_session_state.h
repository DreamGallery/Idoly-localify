#pragma once

#include <cstddef>
#include <string>
#include <utility>

// Protected by the caller's dumpMutex. It owns completeness accounting, not
// Unity objects or files; tests can inject a short/disk-full write explicitly.
struct CaptureSessionState {
    static constexpr size_t kLimit = 16 * 1024 * 1024;
    std::string textFile;
    bool active = false;
    bool truncated = false;
    bool writeFailed = false;
    size_t bytes = 0;
    size_t textRecords = 0;
    size_t layoutRecords = 0;
    size_t droppedRecords = 0;

    void begin(std::string file) {
        *this = CaptureSessionState();
        textFile = std::move(file);
        active = true;
    }
    void stop() { active = false; }
    bool complete() const {
        return !textFile.empty() && !active && !truncated && !writeFailed;
    }

    template <class Writer>
    bool append(bool layout, const std::string& encoded, Writer&& write) {
        if (!active) return false; // Frozen after stop, including counters.
        if (truncated || writeFailed) { ++droppedRecords; return false; }
        if (encoded.size() > kLimit - bytes) {
            truncated = true;
            ++droppedRecords;
            return false;
        }
        if (!write(encoded)) {
            writeFailed = true;
            ++droppedRecords;
            return false;
        }
        bytes += encoded.size();
        ++(layout ? layoutRecords : textRecords);
        return true;
    }
};
