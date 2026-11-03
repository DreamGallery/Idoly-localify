#include "frame_dispatch.h"
#include "managed_api.h"
#include "inline_hook.h"
#include <shadowhook.h>
#include <android/log.h>
#include <array>
#include <mutex>
#include <string>

namespace localify::frames {
namespace {
struct Entry {std::string name; Callback callback=nullptr; bool failed=false;};
std::mutex mutex;
std::array<Entry,8> callbacks;
bool ready=false;
void (*original)(const managed::Method*)=nullptr;
void dispatch(const managed::Method* method) {
    std::array<Entry,8> copy;
    {std::lock_guard<std::mutex> lock(mutex);copy=callbacks;}
    for(const auto& entry:copy)if(entry.callback && !entry.failed){
        try {entry.callback();}
        catch(...){
            std::lock_guard<std::mutex> lock(mutex);
            for(auto& value:callbacks)if(value.callback==entry.callback)value.failed=true;
            // No account values or arbitrary managed exception messages in logs.
            __android_log_print(ANDROID_LOG_WARN,"IdolyFrames","Disabled failed callback: %s",entry.name.c_str());
        }
    }
    original(method);
}
}
bool install(void* (*resolve)(const char*)) {
    std::lock_guard<std::mutex> lock(mutex);if(ready)return true;
    try {
        managed::Api api;api.load(resolve);
        auto* method=api.find(api.klass("UnityEngine","Canvas","UnityEngine.UIModule.dll"),
            "SendWillRenderCanvases","System.Void",{},true);
        ready=method && installInlineHook(method->pointer,reinterpret_cast<void*>(dispatch),original,shadowhook_hook_func_addr);
    }catch(...){ready=false;}
    return ready;
}
bool subscribe(const char* name,Callback callback) {
    if(!callback || !name)return false;
    std::lock_guard<std::mutex> lock(mutex);
    for(const auto& item:callbacks)if(item.callback==callback)return true;
    for(auto& item:callbacks)if(!item.callback){item={name,callback,false};return true;}
    return false;
}
bool available(){std::lock_guard<std::mutex> lock(mutex);return ready;}
}
