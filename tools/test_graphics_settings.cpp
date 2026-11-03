#include <cassert>
#include <cmath>
#include "runtime/graphics_settings.cpp"
int __android_log_print(int,const char*,const char*,...){return 0;}
void* shadowhook_hook_func_addr(void*,void*,void**){assert(false);return nullptr;}
using namespace localify::graphics;
int main(){
    Config value;value.enabled=true;value.fps=120;value.scale=1.5f;value.aa=3;
    auto j=serialize(value);auto round=parse(j);assert(round.enabled && round.fps==120 && round.aa==3);
    for(const char* field:{"schema_version","enabled","fps","scale","antialiasing"}){
        auto broken=j;broken.erase(field);bool rejected=false;try{parse(broken);}catch(...){rejected=true;}assert(rejected);
    }
    for(auto pair:{std::pair<const char*,int>{"fps",240},{"antialiasing",16},{"schema_version",2}}){
        auto broken=j;broken[pair.first]=pair.second;bool rejected=false;try{parse(broken);}catch(...){rejected=true;}assert(rejected);
    }
    auto size=targetSize(1344,2992,1.25f);assert(size.width==1680 && size.height==3740);
    size=targetSize(1344,2992,2);assert(size.height<=4096 && uint64_t(size.width)*size.height<=8388608);
    assert(std::abs(double(size.width)/size.height-1344.0/2992)<.002);
    bool rejected=false;try{targetSize(0,2992,2);}catch(...){rejected=true;}assert(rejected);
    // A game request while the override is active becomes the restoration baseline.
    static int delivered=0;
    originalFps=[](int value,const Method*){delivered=value;};
    active=value;fpsHook(30,nullptr);assert(delivered==120 && gameFps==30);
    applying=true;fpsHook(120,nullptr);assert(gameFps==30);applying=false;
    active.enabled=false;fpsHook(60,nullptr);assert(delivered==60 && gameFps==60);
    originalAuto=[](void*,const Method*){return true;};
    active=value;assert(!autoHook(nullptr,nullptr));active.enabled=false;assert(autoHook(nullptr,nullptr));
    // Missing interfaces must disable the optional adapter without taking down localization.
    install([](const char*)->void*{return nullptr;});
    assert(!nlohmann::json::parse(status())["available"].get<bool>());
    assert(!configure(j.dump()));
}
