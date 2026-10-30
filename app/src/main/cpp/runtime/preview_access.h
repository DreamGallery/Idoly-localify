#pragma once

#include <cstdint>
#include <string>

// Optional, process-local views of released resources. Nothing in this module
// is serialized into the user's inventory, preferences or downloadable text.
namespace localify::preview_access {
enum Category : uint32_t { Costumes = 1, Hair = 2, Poses = 4, Decorations = 8 };
inline constexpr uint32_t WardrobeCategories = Costumes | Hair;
using SymbolResolver = void* (*)(const char*);
// Called after IL2CPP initialization. JNI settings only read/mutate native
// state under the module lock; they never invoke Unity or retain managed views.
void install(SymbolResolver resolve);
bool setEnabled(uint32_t category, bool enabled);
void clearAppearances();
std::string status();
}
