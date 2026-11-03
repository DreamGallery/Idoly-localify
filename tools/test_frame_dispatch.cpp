#include <cassert>
#include <stdexcept>
#include "runtime/frame_dispatch.cpp"
int __android_log_print(int,const char*,const char*,...){return 0;}
void* shadowhook_hook_func_addr(void*,void*,void**){assert(false);return nullptr;}
using namespace localify::frames;
int main(){
    static int healthy=0,broken=0,game=0;
    original=[](const localify::managed::Method*){++game;};
    auto good=[](){++healthy;};
    auto bad=[](){++broken;throw std::runtime_error("synthetic failure");};
    assert(subscribe("healthy",good));assert(subscribe("duplicate",good));
    assert(subscribe("broken",bad));assert(!subscribe("null",nullptr));
    dispatch(nullptr);dispatch(nullptr);
    assert(healthy==2 && broken==1 && game==2);
    assert(!install([](const char*)->void*{return nullptr;}));
    assert(!available());
}
