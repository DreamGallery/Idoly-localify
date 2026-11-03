#include "graphics_settings.h"
#include "graphics_policy.h"
#include "managed_api.h"
#include "inline_hook.h"
#include <shadowhook.h>
#include <android/log.h>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>

namespace localify::graphics {
namespace {
using managed::Method;
using managed::Root;
managed::Api api;
std::recursive_mutex mutex;
Config requested, active;
bool ready=false, pending=false, applying=false;
bool baselineCaptured=false, fpsObserved=false, intervalObserved=false;
std::string error;
int gameFps=30, gameInterval=1, effectiveFps=0, screenWidth=0,screenHeight=0;
int renderWidth=0,renderHeight=0, aaValue=0;
bool postValue=false;
double measuredFps=0;
uint64_t frames=0, generation=0;
auto sampleTime=std::chrono::steady_clock::now();
const Method *widthMethod=nullptr,*heightMethod=nullptr,*fpsGet=nullptr,*intervalGet=nullptr;
const Method *fpsSet=nullptr,*intervalSet=nullptr,*resizeMethod=nullptr,*rebuildMethod=nullptr;
const Method *autoGet=nullptr,*limitedResolution=nullptr;
const Method *aaGet=nullptr,*aaSet=nullptr,*aaQualityGet=nullptr,*aaQualitySet=nullptr,*postGet=nullptr,*postSet=nullptr;
void *aspectField=nullptr;
struct Camera { uint32_t handle=0,data=0;int mode=0,quality=0;bool post=false; };
std::map<void*,Camera> cameras;
struct Vec2 {float x,y;};
static_assert(sizeof(Size)==8 && sizeof(Vec2)==8);
void (*originalUpdate)(void*,const Method*)=nullptr;
void (*originalFps)(int,const Method*)=nullptr;
void (*originalInterval)(int,const Method*)=nullptr;
void (*originalRender)(void*,int,int,bool,const Method*)=nullptr;
bool (*originalAuto)(void*,const Method*)=nullptr;

template<class F> bool hook(const Method* method,F replacement,F& original) {
    return method && installInlineHook(method->pointer,reinterpret_cast<void*>(replacement),original,shadowhook_hook_func_addr);
}
void failure(const std::exception& ex) {
    if(error!=ex.what())__android_log_print(ANDROID_LOG_WARN,"IdolyGraphics","%s",ex.what());
    error=ex.what();
}
void setScalar(const Method* method,int value) {void* args[]{&value};api.call(method,nullptr,args);}
int scalar(const Method* method){return api.value<int>(api.call(method,nullptr));}

void fpsHook(int value,const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!applying){gameFps=value;fpsObserved=true;}
    originalFps(active.enabled && active.fps ? active.fps : value,method);
}
void intervalHook(int value,const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!applying){gameInterval=value;intervalObserved=true;}
    originalInterval(active.enabled && active.fps ? 1 : value,method);
}
bool autoHook(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return active.enabled && active.scale ? false : originalAuto(self,method);
}

void pruneCameras() {
    for(auto it=cameras.begin();it!=cameras.end();) {
        if(api.target(it->second.handle)!=it->first){
            if(it->second.data)api.release(it->second.data);
            api.release(it->second.handle);it=cameras.erase(it);
        }
        else ++it;
    }
}
void cameraAntialiasing(void* self) {
    auto found=cameras.find(self);if(found==cameras.end())return;
    auto& state=found->second;
    Root data(api,api.get(self,"additionalCameraData","UnityEngine.Rendering.Universal.UniversalAdditionalCameraData"));
    if(!data.get())return;
    if(state.data && api.target(state.data)!=data.get()){api.release(state.data);state.data=0;}
    if(!state.data && active.enabled && active.aa){
        state.mode=api.value<int>(api.call(aaGet,data.get()));
        state.quality=api.value<int>(api.call(aaQualityGet,data.get()));
        state.post=api.value<bool>(api.call(postGet,data.get()));
        state.data=api.weak(data.get(),false);
    }
    if(state.data){
        int mode=active.enabled && active.aa ? active.aa-1 : state.mode;
        int quality=active.enabled && active.aa==3 ? 2 : state.quality;
        bool post=active.enabled && active.aa>1 ? true : state.post;
        void* m[]{&mode};void* q[]{&quality};void* p[]{&post};
        if(api.value<int>(api.call(aaGet,data.get()))!=mode)api.call(aaSet,data.get(),m);
        if(api.value<int>(api.call(aaQualityGet,data.get()))!=quality)api.call(aaQualitySet,data.get(),q);
        if(api.value<bool>(api.call(postGet,data.get()))!=post)api.call(postSet,data.get(),p);
        if(!active.enabled || !active.aa){api.release(state.data);state.data=0;}
    }
    aaValue=api.value<int>(api.call(aaGet,data.get()));
    postValue=api.value<bool>(api.call(postGet,data.get()));
}
void renderHook(void* self,int width,int height,bool alpha,const Method* method) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    try {
        // Explicit-size targets (including photo output) retain their requested dimensions.
        if(self && (width<1 || height<1)) {
            pruneCameras();
            if(!cameras.count(self) && cameras.size()<16){auto h=api.weak(self,false);if(h)cameras.emplace(self,Camera{h});}
            if(active.enabled && active.scale && cameras.count(self)) {
                int w=scalar(widthMethod),h=scalar(heightMethod),maximum=-1;float base=1;bool rotate=true;
                Vec2 aspect{};api.fieldGet(self,aspectField,&aspect);
                void* args[]{&w,&h,&aspect,&maximum,&base,&rotate};
                const auto full=api.value<Size>(api.call(limitedResolution,nullptr,args));
                const auto size=targetSize(full.width,full.height,active.scale);
                width=size.width;height=size.height;
            }
        }
    } catch(const std::exception& ex){failure(ex);}
    originalRender(self,width,height,alpha,method);
    try {
        Root texture(api,api.get(self,"targetTexture","VL.VLRenderTexture"));
        if(texture.get()){
            cameraAntialiasing(self);
            renderWidth=api.value<int>(api.get(texture.get(),"width","System.Int32"));
            renderHeight=api.value<int>(api.get(texture.get(),"height","System.Int32"));
        }
    }catch(const std::exception& ex){failure(ex);}
}
void applyPending() {
    if(!pending)return;
    const auto before=active;
    active=requested;pending=false;applying=true;
    try {
        setScalar(fpsSet,active.enabled && active.fps ? active.fps : gameFps);
        setScalar(intervalSet,active.enabled && active.fps ? 1 : gameInterval);
        if((before.enabled && before.scale) || (active.enabled && active.scale)){
            float one=1;void* args[]{&one,&one};api.call(resizeMethod,nullptr,args);
        }
        pruneCameras();
        // Copy retained targets; callbacks may prune their weak registry.
        std::vector<Root> live;
        for(const auto& p:cameras)live.emplace_back(api,api.target(p.second.handle));
        for(auto& camera:live)if(camera.get())api.call(rebuildMethod,camera.get());
        error.clear();++generation;
    }catch(const std::exception& ex){
        failure(ex);active=before;requested=before;
        try {
            setScalar(fpsSet,active.enabled && active.fps ? active.fps : gameFps);
            setScalar(intervalSet,active.enabled && active.fps ? 1 : gameInterval);
            std::vector<Root> live;
            for(const auto& p:cameras)live.emplace_back(api,api.target(p.second.handle));
            for(auto& camera:live)if(camera.get())api.call(rebuildMethod,camera.get());
        }catch(...){error="Graphics rollback incomplete; restart the game";}
    }
    applying=false;
}
void updateHook(void* self,const Method* method) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(ready){
            if(!baselineCaptured){
                try {
                    if(!fpsObserved)gameFps=scalar(fpsGet);
                    if(!intervalObserved)gameInterval=scalar(intervalGet);
                    baselineCaptured=true;
                }catch(const std::exception& ex){failure(ex);}
            }
            if(baselineCaptured)applyPending();
        }
    }
    originalUpdate(self,method);
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(!ready)return;
    ++frames;
    const auto now=std::chrono::steady_clock::now();
    const auto elapsed=std::chrono::duration<double>(now-sampleTime).count();
    if(elapsed>=1){
        measuredFps=frames/elapsed;frames=0;sampleTime=now;
        try {
            pruneCameras();
            for(const auto& p:cameras)cameraAntialiasing(p.first);
            effectiveFps=scalar(fpsGet);screenWidth=scalar(widthMethod);screenHeight=scalar(heightMethod);
        }
        catch(const std::exception& ex){failure(ex);}
    }
}

const Method* resolutionMethod(void* utility) {
    void* iterator=nullptr;
    while(utility){
        const auto* m=api.methods(utility,&iterator);if(!m)break;
        if(std::strcmp(api.methodName(m),"GetLimitedResolution") || api.paramCount(m)!=6)continue;
        void* tuple=api.classFromType(api.returnType(m));uint32_t alignment=0;
        if(!tuple || !api.isValueType(tuple) || api.valueSize(tuple,&alignment)!=sizeof(Size))continue;
        auto* first=api.field(tuple,"Item1");auto* second=api.field(tuple,"Item2");
        if(!first || !second || api.type(api.fieldType(first))!="System.Int32" || api.type(api.fieldType(second))!="System.Int32")continue;
        return api.find(utility,"GetLimitedResolution",api.type(api.returnType(m)),
            {"System.Int32","System.Int32","UnityEngine.Vector2","System.Int32","System.Single","System.Boolean"},true);
    }
    return nullptr;
}
}
bool configure(const std::string& text) {
    try {
        if(text.size()>1024)return false;
        const auto candidate=parse(nlohmann::json::parse(text));
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(!ready && !error.empty())return false;
        requested=candidate;pending=true;return true;
    }catch(...){return false;}
}
std::string status() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto value=serialize(requested);
    value["available"]=ready;value["pending"]=pending;value["error"]=error;
    value["active"]=serialize(active);value["generation"]=generation;
    value["target_fps"]=effectiveFps;value["measured_fps"]=std::round(measuredFps*10)/10;
    value["screen_width"]=screenWidth;value["screen_height"]=screenHeight;
    value["render_width"]=renderWidth;value["render_height"]=renderHeight;value["actual_antialiasing"]=aaValue;value["post_processing"]=postValue;
    return value.dump();
}
void install(void* (*resolve)(const char*)) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if(ready)return;
    try {
        api.load(resolve);
        auto core=[&](const char* type){return api.klass("UnityEngine",type,"UnityEngine.CoreModule.dll");};
        void* quality=api.klass("Solis.Common","SolisQualityManager","solis-submodule.Runtime.dll");
        void* camera=api.klass("VL.Rendering","VLSRPCameraController","vl-unity.Runtime.dll");
        void* utility=api.klass("VL","VLUtility","vl-unity.Runtime.dll");
        void* cameraData=api.klass("UnityEngine.Rendering.Universal","UniversalAdditionalCameraData","Unity.RenderPipelines.Universal.Runtime.dll");
        void* demand=api.klass("UnityEngine.Rendering","OnDemandRendering","UnityEngine.CoreModule.dll");
        widthMethod=api.find(core("Screen"),"get_width","System.Int32",{},true);
        heightMethod=api.find(core("Screen"),"get_height","System.Int32",{},true);
        fpsGet=api.find(core("Application"),"get_targetFrameRate","System.Int32",{},true);
        fpsSet=api.find(core("Application"),"set_targetFrameRate","System.Void",{"System.Int32"},true);
        intervalGet=api.find(demand,"get_renderFrameInterval","System.Int32",{},true);
        intervalSet=api.find(demand,"set_renderFrameInterval","System.Void",{"System.Int32"},true);
        resizeMethod=api.find(core("ScalableBufferManager"),"ResizeBuffers","System.Void",{"System.Single","System.Single"},true);
        rebuildMethod=api.find(camera,"UpdateRenderTarget","System.Void");
        aspectField=camera?api.field(camera,"aspectRatio"):nullptr;
        limitedResolution=resolutionMethod(utility);
        const auto* update=api.find(quality,"Update","System.Void");
        autoGet=api.find(quality,"get_IsAutoDynamicResolution","System.Boolean");
        aaGet=api.find(cameraData,"get_antialiasing","UnityEngine.Rendering.Universal.AntialiasingMode");
        aaSet=api.find(cameraData,"set_antialiasing","System.Void",{"UnityEngine.Rendering.Universal.AntialiasingMode"});
        aaQualityGet=api.find(cameraData,"get_antialiasingQuality","UnityEngine.Rendering.Universal.AntialiasingQuality");
        aaQualitySet=api.find(cameraData,"set_antialiasingQuality","System.Void",{"UnityEngine.Rendering.Universal.AntialiasingQuality"});
        postGet=api.find(cameraData,"get_renderPostProcessing","System.Boolean");
        postSet=api.find(cameraData,"set_renderPostProcessing","System.Void",{"System.Boolean"});
        auto enumValues=[&](const Method* getter,std::initializer_list<const char*> names){
            if(!getter)return false;
            void* klass=api.classFromType(api.returnType(getter));uint32_t alignment=0;
            auto read=reinterpret_cast<void(*)(void*,void*)>(resolve("il2cpp_field_static_get_value"));
            if(!klass || !api.isEnum(klass) || api.valueSize(klass,&alignment)!=sizeof(int) || !read)return false;
            int expected=0;
            for(auto name:names){void* field=api.field(klass,name);int value=-1;if(!field)return false;read(field,&value);if(value!=expected++)return false;}
            return true;
        };
        bool aaTypes=enumValues(aaGet,{"None","FastApproximateAntialiasing","SubpixelMorphologicalAntiAliasing"}) &&
            enumValues(aaQualityGet,{"Low","Medium","High"});
        const auto* render=api.find(camera,"UpdateRenderTarget","System.Void",
            {"System.Int32","System.Int32","System.Boolean"});
        std::string missing;
        for(const auto& item:std::initializer_list<std::pair<const char*,bool>>{
            {"screen width",widthMethod!=nullptr},{"screen height",heightMethod!=nullptr},
            {"frame rate getter",fpsGet!=nullptr},{"frame rate setter",fpsSet!=nullptr},
            {"render interval getter",intervalGet!=nullptr},{"render interval setter",intervalSet!=nullptr},
            {"dynamic buffer resize",resizeMethod!=nullptr},{"camera rebuild",rebuildMethod!=nullptr},
            {"camera aspect",aspectField && api.type(api.fieldType(aspectField))=="UnityEngine.Vector2"},
            {"resolution calculator",limitedResolution!=nullptr},{"quality update",update!=nullptr},
            {"dynamic resolution switch",autoGet!=nullptr},{"anti-aliasing",aaGet && aaSet},
            {"AA quality",aaQualityGet && aaQualitySet},{"AA enum layout",aaTypes},
            {"post processing",postGet && postSet},{"render target",render!=nullptr}})
            if(!item.second)missing+=(missing.empty()?"":", ")+std::string(item.first);
        if(!missing.empty())throw std::runtime_error("Graphics interfaces unavailable: "+missing);
        // No Unity method is invoked here; installation may run on the loader thread.
        if(!hook(fpsSet,fpsHook,originalFps) || !hook(intervalSet,intervalHook,originalInterval) ||
            !hook(render,renderHook,originalRender) ||
            !hook(autoGet,autoHook,originalAuto) || !hook(update,updateHook,originalUpdate))
            throw std::runtime_error("Graphics hook installation failed");
        ready=true;
        __android_log_print(ANDROID_LOG_INFO,"IdolyGraphics","Graphics adapters ready");
    }catch(const std::exception& ex){requested={};active={};failure(ex);}
}
}
