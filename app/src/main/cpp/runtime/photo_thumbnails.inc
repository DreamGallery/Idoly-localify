// Optional settings thumbnails. JNI only queues allowlisted asset names and
// reads PNG snapshots. All game loading, GPU work and cleanup stay on Unity's
// render thread; no inventory, shared view or resource files are modified.
bool thumbnailReady=false;
std::atomic<bool> thumbnailsPending{false};
bool thumbnailReset=false;
std::deque<std::string> thumbnailQueue,thumbnailOrder;
std::map<std::string,std::string> thumbnailCache;
size_t thumbnailBytes=0;
struct ThumbnailLoad {
    std::string asset;
    Root object{api},view{api},texture{api};
    std::chrono::steady_clock::time_point started;
};
std::unique_ptr<ThumbnailLoad> thumbnailLoad;
void (*originalThumbnailApply)(void*,void*,void*,const Method*)=nullptr;
void* thumbnailViewClass=nullptr;
const Method *thumbnailDestroy=nullptr,*thumbnailSet=nullptr;
std::string defaultHairThumbnail,defaultOrnamentThumbnail;

void resolveCatalogThumbnails(std::map<std::string,Character>& choices) {
    thumbnailAssets.clear();
    const auto* exists=api.find(api.klass("Octo","OctoManager","Octo.dll"),
        "IsAssetBundleExists","System.Boolean",{"System.String"},true);
    std::map<std::string,bool> availability;
    auto available=[&](const std::string& name){
        if(!exists || name.empty() || name.size()>=192)return false;
        auto found=availability.find(name);
        if(found!=availability.end())return found->second;
        bool present=false;
        try {
            Root key(api,api.newString(name.c_str()));void* args[]={key.get()};
            present=api.value<bool>(api.call(exists,nullptr,args));
        }catch(const std::exception&){}
        availability[name]=present;return present;
    };
    auto registered=[&](const std::string& name,const std::string& fallback=std::string{}){
        const auto& chosen=available(name)?name:fallback;
        if(!chosen.empty())thumbnailAssets.insert(chosen);
        return chosen;
    };
    // These are the two assets selected by the game's HairThumbnailView
    // GetDefaultThumbAssetName. Keep ornaments separate from ordinary hair.
    defaultHairThumbnail=registered("img_hair_thumb_default");
    defaultOrnamentThumbnail=registered("img_ornament_hair_thumb_default");
    for(auto& [id,character]:choices) {
        for(auto& [key,costume]:character.costumes)costume.thumbnail=registered(costume.thumbnail);
        for(auto& [key,hair]:character.hairs)hair.thumbnail=registered(hair.thumbnail,
            hair.ornamentBase.empty()?defaultHairThumbnail:defaultOrnamentThumbnail);
    }
}

void thumbnailApply(void* self,void* texture,void* component,const Method* method) {
    originalThumbnailApply(self,texture,component,method);
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(thumbnailLoad && self==thumbnailLoad->view.get() && texture)
        thumbnailLoad->texture=Root(api,texture);
}

void destroyThumbnailObject(void* object) {
    if(!object || !thumbnailDestroy)return;
    void* args[]={object};api.call(thumbnailDestroy,nullptr,args);
}

void finishThumbnailLoad() {
    if(!thumbnailLoad)return;
    // Remove ownership before Destroy, whose callbacks may run synchronously.
    auto old=std::move(thumbnailLoad);
    try {destroyThumbnailObject(old->object.get());}catch(const std::exception&){}
}

void rememberThumbnail(const std::string& asset,std::string value) {
    constexpr size_t MaxEntries=96;
    constexpr size_t MaxBytes=4*1024*1024;
    if(!thumbnailCache.count(asset))thumbnailOrder.push_back(asset);
    else thumbnailBytes-=thumbnailCache.at(asset).size();
    thumbnailBytes+=value.size();
    thumbnailCache[asset]=std::move(value);
    while(thumbnailOrder.size()>MaxEntries || thumbnailBytes>MaxBytes) {
        thumbnailBytes-=thumbnailCache.at(thumbnailOrder.front()).size();
        thumbnailCache.erase(thumbnailOrder.front());thumbnailOrder.pop_front();
    }
}

void resetThumbnails() {
    thumbnailQueue.clear();thumbnailCache.clear();thumbnailOrder.clear();thumbnailBytes=0;
    thumbnailReset=true;thumbnailsPending=true;
}

bool installThumbnails() {
    thumbnailViewClass=api.klass("Solis.Common.Octo","TextureResourceView");
    thumbnailSet=api.find(thumbnailViewClass,"Set","System.Void",{"System.String"});
    thumbnailDestroy=api.find(api.klass("UnityEngine","Object","UnityEngine.CoreModule.dll"),
        "Destroy","System.Void",{"UnityEngine.Object"},true);
    if(!thumbnailSet || !thumbnailDestroy)return false;
    return hook(api.find(thumbnailViewClass,"ApplyAsset","System.Void",{"UnityEngine.Texture2D","UnityEngine.UI.RawImage"}),
        thumbnailApply,originalThumbnailApply);
}

void startThumbnailLoad(const std::string& asset) {
    auto load=std::make_unique<ThumbnailLoad>();load->asset=asset;
    load->started=std::chrono::steady_clock::now();
    const auto go=api.klass("UnityEngine","GameObject","UnityEngine.CoreModule.dll");
    load->object=Root(api,api.newObject(go));
    Root name(api,api.newString("LocalifyThumbnail"));void* nameArgs[]={name.get()};
    api.call(api.find(go,".ctor","System.Void",{"System.String"}),load->object.get(),nameArgs);
    // Keep the helper outside every Canvas. Its RawImage exists only to use
    // the same Octo loader as the game's own clothing/hair thumbnails.
    thumbnailLoad=std::move(load);
    const auto* add=api.find(go,"AddComponent","UnityEngine.Component",{"System.Type"});
    auto component=[&](void* klass){
        if(!klass)throw std::runtime_error("Thumbnail component unavailable");
        Root type(api,api.typeObject(api.classType(klass)));void* args[]={type.get()};
        return api.call(add,thumbnailLoad->object.get(),args);
    };
    Root image(api,component(api.klass("UnityEngine.UI","RawImage","UnityEngine.UI.dll")));
    thumbnailLoad->view=Root(api,component(thumbnailViewClass));
    auto* imageField=api.field(thumbnailViewClass,"_image");
    if(!imageField || !api.matches(api.fieldType(imageField),"@ref"))
        throw std::runtime_error("Thumbnail image field unavailable");
    api.fieldSetObject(thumbnailLoad->view.get(),imageField,image.get());
    bool fade=false;void* fadeArgs[]={&fade};
    api.call(api.find(thumbnailViewClass,"set_UseFade","System.Void",{"System.Boolean"}),thumbnailLoad->view.get(),fadeArgs);
    Root key(api,api.newString(asset.c_str()));void* args[]={key.get()};
    api.call(thumbnailSet,thumbnailLoad->view.get(),args);
}

std::string thumbnailPng(void* texture) {
    const auto rt=api.klass("UnityEngine","RenderTexture","UnityEngine.CoreModule.dll");
    const auto tex=api.klass("UnityEngine","Texture2D","UnityEngine.CoreModule.dll");
    const auto graphics=api.klass("UnityEngine","Graphics","UnityEngine.CoreModule.dll");
    const auto* activeSet=api.find(rt,"set_active","System.Void",{"UnityEngine.RenderTexture"},true);
    const auto* release=api.find(rt,"ReleaseTemporary","System.Void",{"UnityEngine.RenderTexture"},true);
    Root previous(api,api.call(api.find(rt,"get_active","UnityEngine.RenderTexture",{},true),nullptr));
    Root buffer(api),readable(api);
    auto cleanup=[&]{
        try{void* args[]={previous.get()};api.call(activeSet,nullptr,args);}catch(const std::exception&){}
        try{if(buffer.get()){void* args[]={buffer.get()};api.call(release,nullptr,args);}}catch(const std::exception&){}
        try{destroyThumbnailObject(readable.get());}catch(const std::exception&){}
    };
    try {
        int width=api.value<int>(api.get(texture,"width","System.Int32"));
        int height=api.value<int>(api.get(texture,"height","System.Int32"));
        if(width<1||height<1||width>8192||height>8192)throw std::runtime_error("Invalid thumbnail size");
        const float ratio=std::min(1.f,160.f/std::max(width,height));
        width=std::max(1,int(width*ratio));height=std::max(1,int(height*ratio));
        int depth=0;void* sizeArgs[]={&width,&height,&depth};
        buffer=Root(api,api.call(api.find(rt,"GetTemporary","UnityEngine.RenderTexture",
            {"System.Int32","System.Int32","System.Int32"},true),nullptr,sizeArgs));
        void* blitArgs[]={texture,buffer.get()};
        api.call(api.find(graphics,"Blit","System.Void",{"UnityEngine.Texture","UnityEngine.RenderTexture"},true),nullptr,blitArgs);
        void* targetArgs[]={buffer.get()};api.call(activeSet,nullptr,targetArgs);
        readable=Root(api,api.newObject(tex));int format=4;bool mipmaps=false;
        void* ctorArgs[]={&width,&height,&format,&mipmaps};
        api.call(api.find(tex,".ctor","System.Void",{"System.Int32","System.Int32","UnityEngine.TextureFormat","System.Boolean"}),readable.get(),ctorArgs);
        struct Rect {float x,y,w,h;} rect{0,0,float(width),float(height)};
        int x=0,y=0;void* readArgs[]={&rect,&x,&y,&mipmaps};
        api.call(api.find(tex,"ReadPixels","System.Void",{"UnityEngine.Rect","System.Int32","System.Int32","System.Boolean"}),readable.get(),readArgs);
        void* encodeArgs[]={readable.get()};
        Root bytes(api,api.call(api.find(api.klass("UnityEngine","ImageConversion","UnityEngine.ImageConversionModule.dll"),
            "EncodeToPNG","System.Byte[]",{"UnityEngine.Texture2D"},true),nullptr,encodeArgs));
        void* stringArgs[]={bytes.get()};
        Root base64(api,api.call(api.find(api.klass("System","Convert","mscorlib.dll"),"ToBase64String","System.String",{"System.Byte[]"},true),nullptr,stringArgs));
        auto value=api.string(base64.get(),262144);
        cleanup();return value.empty()?"!":value;
    }catch(...){cleanup();throw;}
}

void tickThumbnails() {
    if(thumbnailReset){finishThumbnailLoad();thumbnailReset=false;}
    if(!thumbnailReady){thumbnailQueue.clear();thumbnailsPending=false;return;}
    if(thumbnailLoad) {
        try {
            if(thumbnailLoad->texture.get()) {
                rememberThumbnail(thumbnailLoad->asset,thumbnailPng(thumbnailLoad->texture.get()));
                finishThumbnailLoad();
            }else if(std::chrono::steady_clock::now()-thumbnailLoad->started>std::chrono::seconds(12)) {
                rememberThumbnail(thumbnailLoad->asset,"!");finishThumbnailLoad();
            }
        }catch(const std::exception& e){
            __android_log_print(ANDROID_LOG_WARN,Tag,"Thumbnail unavailable: %s",e.what());
            rememberThumbnail(thumbnailLoad->asset,"!");finishThumbnailLoad();
        }
    }
    if(!thumbnailLoad && !thumbnailQueue.empty()) {
        auto asset=std::move(thumbnailQueue.front());thumbnailQueue.pop_front();
        try{startThumbnailLoad(asset);}catch(const std::exception& e){
            __android_log_print(ANDROID_LOG_WARN,Tag,"Thumbnail unavailable: %s",e.what());
            rememberThumbnail(asset,"!");finishThumbnailLoad();
        }
    }
    thumbnailsPending=thumbnailLoad || !thumbnailQueue.empty();
}
