#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>

#include "master_blob_matcher.h"

void write32(std::ofstream& output, uint32_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
                          static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
    output.write(bytes, sizeof(bytes));
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string path = argv[1];
    {
        std::ofstream output(path, std::ios::binary);
        output.write("IDOLYMB2", 8);
        write32(output, 1);
        write32(output, 1);
        write32(output, 3);
        write32(output, 2);
        output.write("abc", 3);
        output.write("xy", 2);
        write32(output, 3);
        output.write("old", 3);
    }
    MasterBlobMatcher matcher;
    assert(matcher.load(path) == 1);
    assert(matcher.suppressedSources().size() == 1);
    assert(matcher.suppressedSources()[0] == "old");
    const auto* found = matcher.match(reinterpret_cast<const uint8_t*>("abc"), 3);
    assert(found && std::string(found->begin(), found->end()) == "xy");
    assert(!matcher.match(reinterpret_cast<const uint8_t*>("abd"), 3));
    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output.put('!');
    }
    bool rejected = false;
    try { matcher.load(path); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    assert(matcher.size() == 1);
    assert(matcher.match(reinterpret_cast<const uint8_t*>("abc"), 3));
}
