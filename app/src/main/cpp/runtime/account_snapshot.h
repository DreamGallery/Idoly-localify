#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace localify::account_snapshot {
void install(void* (*resolve)(const char*));
bool request(bool includePhotos);
std::string status();
std::string document();
std::vector<uint8_t> protobuf();
}
