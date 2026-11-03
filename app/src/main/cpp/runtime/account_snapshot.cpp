#include "account_snapshot.h"
#include "managed_api.h"
#include "frame_dispatch.h"
#include "json.hpp"
#include <cctype>
#include <chrono>
#include <mutex>
#include <memory>
#include <stdexcept>

namespace localify::account_snapshot {
namespace {
using managed::Method;
using managed::Root;
using Json=nlohmann::json;
managed::Api api;
std::mutex mutex;
bool ready=false,pending=false,includePhotos=false;
std::string error,serialized;
std::vector<uint8_t> bytes;
uint64_t generation=0;
const Method *instance=nullptr,*constructor=nullptr,*toBytes=nullptr,*formatterDefault=nullptr,*format=nullptr;
void *responseClass=nullptr;
uintptr_t (*arrayLength)(void*)=nullptr;
struct Field {std::string name,type;void* source;const Method *getter,*setter;bool repeated;};
std::vector<Field> fields;
struct Photo {std::string id,file,url;int firebaseType=0;std::unique_ptr<Root> task;std::chrono::steady_clock::time_point started;};
std::vector<Photo> photos;
size_t finishedPhotos=0;
int userImageType=-1,generatedImageType=-1;
constexpr size_t MaxBytes=32*1024*1024;
constexpr int MaxRows=200000;

const Method* allList(void* klass) {
    for(auto* type=klass;type;type=api.parent(type)){
        void* iterator=nullptr;
        while(auto* m=api.methods(type,&iterator)){
            if(std::strcmp(api.methodName(m),"GetAllList") || api.paramCount(m)!=1)continue;
            const auto param=api.type(api.paramType(m,0));
            if(param.rfind("System.Comparison",0)!=0)continue;
            return api.find(klass,"GetAllList","@ref",{param});
        }
    }
    throw std::runtime_error("Account collection reader unavailable");
}
void capture() {
    std::lock_guard<std::mutex> lock(mutex);
    if(!ready || !pending)return;
    if(!serialized.empty())return;
    pending=false;error.clear();serialized.clear();bytes.clear();photos.clear();finishedPhotos=0;
    try {
        Root manager(api,api.call(instance,nullptr));
        if(!manager.get())throw std::runtime_error("请先登录账号并进入首页");
        Root response(api,api.newObject(responseClass));api.call(constructor,response.get());
        Json coverage=Json::object();int rows=0;
        for(const auto& f:fields){
            void* raw=nullptr;api.fieldGet(manager.get(),f.source,&raw);Root source(api,raw);
            if(!f.repeated){
                if(source.get()){
                    if(api.type(api.classType(api.objectClass(source.get())))!=f.type)
                        throw std::runtime_error("Account field type mismatch: "+f.name);
                    void* args[]{source.get()};api.call(f.setter,response.get(),args);
                }
                coverage[f.name]=source.get()?1:0;continue;
            }
            if(!source.get())throw std::runtime_error("Account collection not initialized: "+f.name);
            Root destination(api,api.call(f.getter,response.get()));
            auto* item=api.find(api.objectClass(destination.get()),"get_Item","@ref",{"System.Int32"});
            if(!item)throw std::runtime_error("Snapshot repeated item unavailable");
            const auto rowType=api.type(api.returnType(item));
            auto* add=api.find(api.objectClass(destination.get()),"Add","System.Void",{rowType});
            void* nullArg[]{nullptr};Root list(api,api.call(allList(api.objectClass(source.get())),source.get(),nullArg));
            int count=api.value<int>(api.get(list.get(),"Count","System.Int32"));
            if(count<0 || count>MaxRows-rows)throw std::runtime_error("Account snapshot has too many rows");
            rows+=count;coverage[f.name]=count;
            auto* get=api.find(api.objectClass(list.get()),"get_Item",rowType,{"System.Int32"});
            if(!add || !get)throw std::runtime_error("Account row signature mismatch: "+f.name);
            for(int i=0;i<count;++i){
                void* index[]{&i};Root row(api,api.call(get,list.get(),index));
                if(!row.get())throw std::runtime_error("Account row is null");
                void* args[]{row.get()};api.call(add,destination.get(),args);
            }
        }
        Root user(api,api.get(response.get(),"User"));
        Root pub(api,api.get(response.get(),"UserPublic"));
        if(!user.get() || !pub.get())throw std::runtime_error("账号数据尚未完整同步，请先进入首页");
        if(includePhotos){
            Root list(api,api.get(response.get(),"Photos"));
            int count=api.value<int>(api.get(list.get(),"Count","System.Int32"));
            auto* get=api.find(api.objectClass(list.get()),"get_Item","Solis.Common.Proto.Transaction.UserPhoto",{"System.Int32"});
            auto* filename=api.find(api.klass("Solis.Common.Data","PhotoDataExtensions"),"GetFileName","System.String",{"Solis.Common.Data.IPhotoData","System.Boolean"},true);
            for(int i=0;i<count;++i){
                void* index[]{&i};Root photo(api,api.call(get,list.get(),index));
                int type=api.value<int>(api.get(photo.get(),"ImageType","Solis.Common.Proto.PhotoImageType"));
                if(type!=userImageType && type!=generatedImageType)continue;
                bool thumbnail=false;void* args[]{photo.get(),&thumbnail};
                Root name(api,api.call(filename,nullptr,args));
                Photo value;value.id=api.text(photo.get(),"PhotoId");value.file=api.string(name.get());
                value.firebaseType=api.value<int>(api.get(photo.get(),"FirebaseType","Solis.Common.Utility.SolisFirebaseType"));
                photos.push_back(std::move(value));
            }
            if(photos.size()>5000)throw std::runtime_error("照片数量超过当前备份上限");
        }
        int size=api.value<int>(api.call(api.find(responseClass,"CalculateSize","System.Int32"),response.get()));
        if(size<=0 || size>int(MaxBytes))throw std::runtime_error("Account snapshot size exceeds limit");
        void* args[]{response.get()};Root binary(api,api.call(toBytes,nullptr,args));
        if(!binary.get() || arrayLength(binary.get())!=size)throw std::runtime_error("Snapshot serialization length mismatch");
        // IL2CPP single-dimensional byte array header: object, bounds, length.
        auto* data=reinterpret_cast<const uint8_t*>(binary.get())+4*sizeof(void*);
        bytes.assign(data,data+size);
        Root formatter(api,api.call(formatterDefault,nullptr));Root text(api,api.call(format,formatter.get(),args));
        auto value=Json::parse(api.string(text.get(),int(MaxBytes)));
        if(!value.contains("user") || !value.contains("userPublic"))throw std::runtime_error("Snapshot identity missing");
        ++generation;
        serialized=Json{{"schema_version",1},{"generation",generation},
            {"captured_at_unix",std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            {"message_type","Solis.Common.Proto.Api.UserGetResponse"},{"coverage",coverage},{"data",value}}.dump();
        pending=!photos.empty();
    }catch(const std::exception& ex){bytes.clear();serialized.clear();error=ex.what();}
}
void photoTick() {
    std::lock_guard<std::mutex> lock(mutex);
    if(!pending || serialized.empty())return;
    try{
        int running=0;for(const auto& photo:photos)if(photo.task)++running;
        for(auto& photo:photos){
            if(!photo.url.empty())continue;
            if(!photo.task){
                if(running>=4)continue;
                void* typeArg[]{&photo.firebaseType};
                Root bucket(api,api.call(api.find(api.klass("Solis.Common.Utility","FirebaseUtility"),
                    "GetFireBaseURLBase","System.String",{"Solis.Common.Utility.SolisFirebaseType"},true),nullptr,typeArg));
                auto* storageClass=api.klass("Firebase.Storage","FirebaseStorage","Firebase.Storage.dll");
                void* bucketArg[]{bucket.get()};
                Root storage(api,api.call(api.find(storageClass,"GetInstance","Firebase.Storage.FirebaseStorage",{"System.String"},true),nullptr,bucketArg));
                Root reference(api,api.call(api.find(storageClass,"GetReferenceFromUrl","Firebase.Storage.StorageReference",{"System.String"}),storage.get(),bucketArg));
                Root file(api,api.newString(photo.file.c_str()));
                // Null uses the same current-account storage identity as the game's photo viewer.
                // ServerUserId and Firebase storage identities are not interchangeable.
                void* pathArgs[]{file.get(),nullptr};
                Root path(api,api.call(api.find(api.klass("Solis.Common.Photo","PhotoImageFirebase"),"GetLocationPath","System.String",{"System.String","System.String"},true),nullptr,pathArgs));
                void* pathArg[]{path.get()};
                Root child(api,api.call(api.find(api.objectClass(reference.get()),"Child","Firebase.Storage.StorageReference",{"System.String"}),reference.get(),pathArg));
                photo.task=std::make_unique<Root>(api,api.call(api.find(api.objectClass(child.get()),"GetDownloadUrlAsync","@ref"),child.get()));
                photo.started=std::chrono::steady_clock::now();++running;
            }
            auto* task=photo.task->get();
            if(!api.value<bool>(api.get(task,"IsCompleted","System.Boolean"))){
                if(std::chrono::steady_clock::now()-photo.started>std::chrono::seconds(45))throw std::runtime_error("照片下载地址获取超时，请重试");
                continue;
            }
            if(api.value<bool>(api.get(task,"IsFaulted","System.Boolean")) || api.value<bool>(api.get(task,"IsCanceled","System.Boolean"))){
                std::string code;
                try {
                    Root outer(api,api.get(task,"Exception","System.AggregateException"));
                    Root inner(api,api.get(outer.get(),"InnerException","System.Exception"));
                    auto* getter=api.find(api.objectClass(inner.get()),"get_ErrorCode","System.Int32");
                    if(getter)code="（"+std::to_string(api.value<int>(api.call(getter,inner.get())))+"）";
                }catch(...){}
                throw std::runtime_error("部分照片无法读取"+code+"，请重试或不包含照片原图");
            }
            Root url(api,api.get(task,"Result","System.Uri"));photo.url=api.text(url.get(),"AbsoluteUri");
            photo.task.reset();++finishedPhotos;--running;
        }
        if(finishedPhotos==photos.size())pending=false;
    }catch(const std::exception& ex){for(auto& photo:photos)photo.task.reset();pending=false;error=ex.what();serialized.clear();bytes.clear();}
}
void tick(){capture();photoTick();}
}
void install(void* (*resolve)(const char*)) {
    std::lock_guard<std::mutex> lock(mutex);
    try {
        api.load(resolve);
        auto* manager=api.klass("Solis.Common.User","UserDataManager");
        responseClass=api.klass("Solis.Common.Proto.Api","UserGetResponse");
        instance=api.find(manager,"get_Instance","@ref",{},true);
        constructor=api.find(responseClass,".ctor","System.Void");
        auto* extension=api.klass("Google.Protobuf","MessageExtensions","Google.Protobuf.dll");
        auto* formatter=api.klass("Google.Protobuf","JsonFormatter","Google.Protobuf.dll");
        toBytes=api.find(extension,"ToByteArray","System.Byte[]",{"Google.Protobuf.IMessage"},true);
        formatterDefault=api.find(formatter,"get_Default","Google.Protobuf.JsonFormatter",{},true);
        format=api.find(formatter,"Format","System.String",{"Google.Protobuf.IMessage"});
        arrayLength=reinterpret_cast<decltype(arrayLength)>(resolve("il2cpp_array_length"));
        auto readEnum=reinterpret_cast<void(*)(void*,void*)>(resolve("il2cpp_field_static_get_value"));
        auto* imageType=api.klass("Solis.Common.Proto","PhotoImageType");
        auto* userType=imageType?api.field(imageType,"User"):nullptr;
        auto* generatedType=imageType?api.field(imageType,"Generated"):nullptr;
        if(!readEnum || !userType || !generatedType)throw std::runtime_error("Photo type interfaces unavailable");
        readEnum(userType,&userImageType);readEnum(generatedType,&generatedImageType);
        if(!manager || !responseClass || !instance || !constructor || !toBytes || !formatterDefault || !format || !arrayLength)
            throw std::runtime_error("Account snapshot interfaces unavailable: "+managed::missingSignature);
        void* iterator=nullptr;
        while(auto* m=api.methods(responseClass,&iterator)){
            const std::string name=api.methodName(m),type=api.type(api.returnType(m));
            if(name.rfind("get_",0)!=0 || api.paramCount(m)!=0)continue;
            uint32_t implementation=0;if(api.flags(m,&implementation)&0x10u)continue;
            bool repeated=type.rfind("Google.Protobuf.Collections.RepeatedField",0)==0;
            if(!repeated && type.rfind("Solis.Common.Proto.Transaction.",0)!=0)
                throw std::runtime_error("Unsupported account snapshot property: "+name);
            const auto property=name.substr(4);auto storage=property;storage[0]=char(std::tolower(storage[0]));
            auto* field=api.field(manager,("<_"+storage+">k__BackingField").c_str());
            auto* setter=repeated?nullptr:api.find(responseClass,("set_"+property).c_str(),"System.Void",{type});
            if(!field || (!repeated && (!setter || api.type(api.fieldType(field))!=type)))
                throw std::runtime_error("Account snapshot field unavailable: "+property);
            fields.push_back({property,type,field,m,setter,repeated});
        }
        if(fields.size()<40 || !frames::available() || !frames::subscribe("account snapshot",tick))
            throw std::runtime_error("Account snapshot lifecycle unavailable");
        ready=true;
    }catch(const std::exception& ex){ready=false;fields.clear();error=ex.what();}
}
bool request(bool images){std::lock_guard<std::mutex> lock(mutex);if(!ready || pending)return false;serialized.clear();bytes.clear();photos.clear();includePhotos=images;pending=true;error.clear();return true;}
std::string status(){std::lock_guard<std::mutex> lock(mutex);return Json{{"available",ready},{"pending",pending},{"complete",!pending && !serialized.empty()},{"generation",generation},{"field_count",fields.size()},{"bytes",bytes.size()},{"photo_count",photos.size()},{"photos_resolved",finishedPhotos},{"error",error}}.dump();}
std::string document(){std::lock_guard<std::mutex> lock(mutex);if(pending || serialized.empty())return {};auto result=Json::parse(serialized);result["photo_downloads"]=Json::array();for(const auto& photo:photos)result["photo_downloads"].push_back({{"photo_id",photo.id},{"file_name",photo.file},{"url",photo.url}});return result.dump(-1,' ',true);}
std::vector<uint8_t> protobuf(){std::lock_guard<std::mutex> lock(mutex);return bytes;}
}
