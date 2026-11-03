#pragma once
#include <string>
namespace localify::graphics {
void install(void* (*resolve)(const char*));
std::string status();
// JNI only queues values. All Unity operations run from the game update callback.
bool configure(const std::string& json);
}
