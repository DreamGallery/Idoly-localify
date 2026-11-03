#pragma once
namespace localify::frames {
using Callback = void(*)();
bool install(void* (*resolve)(const char*));
bool subscribe(const char* name, Callback callback);
bool available();
}
