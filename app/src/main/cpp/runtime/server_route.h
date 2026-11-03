#pragma once
#include <string>
namespace localify::server_route {
// Configuration is frozen at installation. UI changes take effect next process.
bool configure(const std::string& json);
bool validate(const std::string& json);
void install(void* (*resolve)(const char*));
std::string status();
}
