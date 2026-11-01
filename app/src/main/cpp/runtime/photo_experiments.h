#pragma once

#include <string>

namespace localify::photo_experiments {
using SymbolResolver = void* (*)(const char*);
void install(SymbolResolver resolve);
// Invoked on the game's thread after its authoritative account initialization.
void accountChanged();
// Settings access native value snapshots only, never Unity/managed objects.
std::string status();
bool configure(const std::string& json);
// Empty = queued; ! = unavailable; otherwise bounded PNG in base64.
std::string thumbnail(const std::string& asset);
void cancelThumbnails();
// Called only by Unity's willRenderCanvases hook, never from JNI.
void processThumbnails();
bool posesEnabled();
// Called only from the scoped photography pose-list adapter.
std::string poseMotionSource(const std::string& character);
}
