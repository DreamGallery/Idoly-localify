#include "photo_experiments.h"
#include "photo_experiment_policy.h"
#include "photo_model_catalog.h"
#include "photo_managed.h"
#include "inline_hook.h"
#include "json.hpp"
#include <android/log.h>
#include <shadowhook.h>
#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <set>
#include <chrono>
#include <deque>

namespace localify::photo_experiments {
namespace {
using managed::Method;
using managed::Root;
using Json = nlohmann::json;
managed::Api api;
constexpr const char* Tag = "IdolyPhoto";
constexpr const char* Master = "Solis.Common.Proto.Master.";
constexpr const char* Proto = "Solis.Common.Proto.Api.";
std::recursive_mutex mutex;
std::atomic<bool> initialized{false};
bool normalize = false, limited = false, collaborators = false;
bool requestReady = false, modelReady = false, contentReady = false, characterReady = false;
bool apiReady = false;
std::string error;
uint64_t account = 0, normalizedRequests = 0, renderedModels = 0;
std::map<std::string, Character> catalog;
std::vector<Rule> rules;
struct InitialCharacter { Appearance appearance; std::string pose, facial; int looking = 0; };
std::map<std::string, InitialCharacter> initial;
std::atomic<bool> collabPoses{false};
std::set<std::string> thumbnailAssets;

void fail(const std::string& message) {
    if (error != message) __android_log_print(ANDROID_LOG_WARN, Tag, "%s", message.c_str());
    error = message;
}
template<class F> bool hook(const Method* method, F replacement, F& original) {
    if(!method){fail("Photography hook signature unavailable: "+managed::missingSignature);return false;}
    return method && installInlineHook(method->pointer, reinterpret_cast<void*>(replacement), original, shadowhook_hook_func_addr);
}
void* clone(void* object) {
    return api.call(api.find(api.objectClass(object), "Clone", "@ref"), object);
}
void* create(void* klass) {
    Root value(api, klass ? api.newObject(klass) : nullptr);
    if (!value.get()) throw std::runtime_error("Cannot construct photography value");
    api.call(api.find(klass, ".ctor", "System.Void"), value.get());
    return value.get();
}
void setText(void* object, const char* property, const std::string& value) {
    Root text(api, api.newString(value.c_str()));
    void* args[] = {text.get()};
    api.call(api.find(api.objectClass(object), (std::string("set_")+property).c_str(), "System.Void", {"System.String"}), object, args);
}
void setNumber(void* object, const char* property, int value, bool enumeration = false) {
    void* args[] = {&value};
    api.call(api.find(api.objectClass(object), (std::string("set_")+property).c_str(), "System.Void",
        {enumeration ? "@enum" : "System.Int32"}), object, args);
}
int count(void* list, int max = 20000) {
    auto* method=api.find(api.objectClass(list),"get_Count","System.Int32");
    if(!method)method=api.find(api.objectClass(list),"get_Length","System.Int32");
    int n = api.value<int>(api.call(method,list));
    if (n < 0 || n > max) throw std::runtime_error("Unexpected photography collection size");
    return n;
}
void* item(void* list, int i) {
    void* args[] = {&i};
    auto* method=api.find(api.objectClass(list), "get_Item", "@ref", {"System.Int32"});
    if(!method)method=api.find(api.objectClass(list), "GetValue", "System.Object", {"System.Int32"});
    return api.call(method, list, args);
}
void clear(void* list) { api.call(api.find(api.objectClass(list), "Clear", "System.Void"), list); }
void append(void* list, void* value, const std::string& type) {
    void* args[] = {value};
    api.call(api.find(api.objectClass(list), "Add", "System.Void", {type}), list, args);
}
std::vector<std::string> strings(void* list) {
    std::vector<std::string> out;
    for (int i=0, n=count(list); i<n; ++i) out.push_back(api.string(item(list,i)));
    return out;
}
void replaceStrings(void* list, const std::vector<std::string>& values) {
    clear(list);
    for (const auto& value : values) {
        Root text(api, api.newString(value.c_str())); append(list, text.get(), "System.String");
    }
}
void* masterTable(const std::string& name) {
    return api.call(api.find(api.klass("Solis.Common.Master","MasterManager"), ("get_"+name+"Master").c_str(),
        "Solis.Common.Master."+name+"Master", {}, true), nullptr);
}
void* masterRow(const std::string& name, const std::string& id) {
    Root table(api,masterTable(name)); Root key(api,api.newString(id.c_str()));
    void* args[]={key.get()};
    return api.call(api.find(api.objectClass(table.get()),"FindByKey",Master+name,{"System.String"}),table.get(),args);
}
void* allMaster(const std::string& name) {
    Root table(api,masterTable(name)); int32_t sort=0;void* args[]={&sort};
    return api.call(api.find(api.objectClass(table.get()),"GetAllWithSortByKey","@ref",{"@enum"}),table.get(),args);
}
void* userCollection(const char* name) {
    return api.call(api.find(api.klass("Solis.Common.User","UserDataManager"),(std::string("get_")+name).c_str(),"@ref",{},true),nullptr);
}
void* userRow(const char* name,const std::string& type,const std::string& id) {
    Root list(api,userCollection(name)); Root key(api,api.newString(id.c_str()));void* args[]={key.get()};
    return api.call(api.find(api.objectClass(list.get()),"FindById","Solis.Common.Proto.Transaction."+type,{"System.String"}),list.get(),args);
}
void* reference(void* object,const char* name) {
    if(!object)return nullptr;
    void* field=api.field(api.objectClass(object),name);
    if(!field || !api.matches(api.fieldType(field),"@ref")) return nullptr;
    void* value=nullptr;api.fieldGet(object,field,&value);return value;
}
void* referenceArray(const std::vector<void*>& objects,void* elementClass) {
    Root array(api,api.newArray(elementClass,objects.size()));
    const auto* setter=api.find(api.objectClass(array.get()),"SetValue","System.Void",{"System.Object","System.Int32"});
    for(int i=0;i<int(objects.size());++i){void* args[]={objects[i],&i};api.call(setter,array.get(),args);}
    return array.get();
}
template<class F> void each(void* enumerable,F callback) {
    Root enumerator(api,api.call(api.find(api.objectClass(enumerable),"GetEnumerator","@enumerator"),enumerable));
    void* klass=api.objectClass(enumerator.get());
    void* instance=api.isValueType(klass)?api.unbox(enumerator.get()):enumerator.get();
    const auto* next=api.find(klass,"MoveNext","System.Boolean");
    auto* current=api.find(klass,"get_Current","@ref");
    if(!current)current=api.find(klass,"System.Collections.IEnumerator.get_Current","System.Object");
    for(int n=0;api.value<bool>(api.call(next,instance));++n){
        if(n>=20000)throw std::runtime_error("Photography enumeration limit exceeded");
        callback(api.call(current,instance));
    }
}

void resolveCatalogThumbnails(std::map<std::string,Character>& choices);

void refreshCatalog() {
    std::map<std::string,Character> next;
    std::map<std::string,InitialCharacter> nextInitial;
    Root characters(api,allMaster("Character")), costumes(api,allMaster("Costume")), hairs(api,allMaster("Hair"));
    std::map<std::string,std::map<std::string,CostumeChoice>> costumeChoices;
    std::map<std::string,std::map<std::string,HairChoice>> hairChoices;
    thumbnailAssets.clear();
    auto thumbnail=[&](void* row){
        const auto* method=api.find(api.objectClass(row),"GetThumbAssetName","System.String");
        return method?api.string(api.call(method,row)):std::string{};
    };
    for(int i=0,n=count(hairs.get());i<n;++i) {
        Root row(api,item(hairs.get(),i));
        const auto id=api.text(row.get(),"Id");
        if(api.text(row.get(),"HairAssetId").empty() || api.text(row.get(),"FaceAssetId").empty())continue;
        HairChoice choice{api.text(row.get(),"Name"),strings(api.get(row.get(),"WearableCostumeIds")),
            strings(api.get(row.get(),"NotWearableCostumeIds"))};
        choice.thumbnail=thumbnail(row.get());choice.order=api.value<int>(api.get(row.get(),"Order","System.Int32"));
        hairChoices[api.text(row.get(),"CharacterId")].emplace(id,std::move(choice));
    }
    Root ornaments(api,allMaster("OrnamentHair"));
    for(int i=0,n=count(ornaments.get(),1000);i<n;++i) {
        Root row(api,item(ornaments.get(),i));auto base=api.text(row.get(),"BaseHairId");
        for(auto& [owner,choices]:hairChoices)if(choices.count(base))
            for(const auto& id:strings(api.get(row.get(),"OrnamentHairIds"))) {
                auto variant=choices.find(id);
                if(variant!=choices.end()&&id!=base)variant->second.ornamentBase=base;
            }
    }
    // NPC records sometimes reuse another character's placeholder model. Only
    // expose a costume whose actual body asset belongs to the named character.
    std::map<std::string,Appearance> npc;
    for(int i=0,n=count(costumes.get());i<n;++i) {
        Root row(api,item(costumes.get(),i));
        const auto owner=api.text(row.get(),"CharacterId");
        Root character(api,masterRow("Character",owner));
        if(!character.get())continue;
        const auto asset=api.text(character.get(),"AssetId"), body=api.text(row.get(),"BodyAssetId");
        if(asset.empty() || body.rfind(asset+"-",0)!=0)continue;
        const auto id=api.text(row.get(),"Id"), hair=api.text(row.get(),"DefaultHairId");
        auto head=hairChoices[owner].find(hair);
        if(head==hairChoices[owner].end() || !head->second.fits(id))continue;
        costumeChoices[owner].emplace(id,CostumeChoice{api.text(row.get(),"Name"),hair,thumbnail(row.get()),
            api.value<int>(api.get(row.get(),"Order","System.Int32"))});
        npc.emplace(owner,Appearance{id,hair});
    }
    for(int i=0,n=count(characters.get(),1000);i<n;++i) {
        Root row(api,item(characters.get(),i));
        const auto id=api.text(row.get(),"Id");
        const int type=api.value<int>(api.get(row.get(),"Type","@enum"));
        Character value{id,api.text(row.get(),"Name"),{},type==1||type==3||type==4,false};
        value.collaboration=type==4;
        value.order=api.value<int>(api.get(row.get(),"Order","System.Int32"));
        auto defaultCostume=api.text(row.get(),"DefaultCostumeId");
        Root costume(api,masterRow("Costume",defaultCostume));
        if(value.playable && costume.get() && api.text(costume.get(),"CharacterId")==id)
            value.appearance={defaultCostume,api.text(costume.get(),"DefaultHairId")};
        else if(npc.count(id)) value.appearance=npc.at(id);
        // This mascot is an ADV whole-body asset. Its Costume/Hair records are
        // Sakura placeholders, so they must never be used as its visual model.
        if(id=="char-gyo")value.modelAssets={"mdl_oth_gyogyottosan-normal-00"};
        if(value.modelAssets.empty() && (value.appearance.costume.empty() || value.appearance.hair.empty()))continue;
        if(value.modelAssets.empty()) {
            value.costumes=std::move(costumeChoices[id]);value.hairs=std::move(hairChoices[id]);
        }
        value.owned=userRow("Characters","UserCharacter",id)!=nullptr;
        next.emplace(id,value);
        if(!value.playable || !value.owned || !userRow("Costumes","UserCostume",defaultCostume) ||
           !userRow("Hairs","UserHair",value.appearance.hair))continue;
        InitialCharacter base{value.appearance};
        Root poses(api,userCollection("PhotoPoses")), key(api,api.newString(id.c_str()));
        void* args[]={key.get()};
        Root ids(api,api.call(api.find(api.objectClass(poses.get()),"GetUserPhotoPoseIds","@ref",{"System.String"}),poses.get(),args));
        int firstPose=INT32_MAX;
        each(ids.get(),[&](void* value){
            auto pid=api.string(value);Root pose(api,masterRow("PhotoPose",pid));
            int order=api.value<int>(api.get(pose.get(),"Order","System.Int32"));
            if(order<firstPose){firstPose=order;base.pose=pid;}
        });
        if(!base.pose.empty()) {
            Root pose(api,masterRow("PhotoPose",base.pose));
            base.looking=api.value<int>(api.get(pose.get(),"LookingType","@enum"));
        }
        // A default expression has no unlocking condition and has a real
        // motion for the baseline character. Never guess an ID suffix.
        Root facials(api,allMaster("PhotoFacial")),motions(api,allMaster("PhotoFacialMotion"));
        int firstFacial=INT32_MAX;
        for(int j=0,m=count(facials.get());j<m;++j) {
            Root facial(api,item(facials.get(),j));
            if(!api.text(facial.get(),"UnlockConditionId").empty())continue;
            int order=api.value<int>(api.get(facial.get(),"Order","System.Int32"));
            if(order>=firstFacial)continue;
            auto fid=api.text(facial.get(),"Id");
            for(int k=0,p=count(motions.get());k<p;++k){
                Root motion(api,item(motions.get(),k));
                if(api.text(motion.get(),"PhotoFacialId")==fid && api.text(motion.get(),"CharacterId")==id){base.facial=fid;firstFacial=order;break;}
            }
        }
        nextInitial.emplace(id,std::move(base));
    }
    addStoryModels(next);
    resolveCatalogThumbnails(next);
    catalog=std::move(next);initial=std::move(nextInitial);
    __android_log_print(ANDROID_LOG_INFO,Tag,"Photo catalog: %zu models, %zu owned defaults",catalog.size(),initial.size());
}

// Each sub-adapter owns a separate runtime boundary. Protocol processing never
// writes a transition or an inventory; rendering never edits a request.
#include "photo_requests.inc"
#include "photo_models.inc"
#include "photo_poses.inc"
#include "photo_content.inc"
#include "photo_thumbnails.inc"
}

void install(SymbolResolver resolve) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(initialized.exchange(true))return;
    try {
        api.load(resolve);
        apiReady=true;
        requestReady=installRequests();
        try{modelReady=installModels();}catch(const std::exception& e){fail(e.what());}
        try{contentReady=installContent();}catch(const std::exception& e){fail(e.what());}
        try{characterReady=installCharacters();}catch(const std::exception& e){fail(e.what());}
        try{thumbnailReady=installThumbnails();}catch(const std::exception& e){fail(e.what());}
        __android_log_print(ANDROID_LOG_INFO,Tag,"Adapters: requests=%d models=%d limited=%d characters=%d",
                            requestReady,modelReady,contentReady,characterReady);
    }catch(const std::exception& e){fail(e.what());}
}
void accountChanged() {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    ++account;normalize=limited=collaborators=false;collabPoses=false;rules.clear();catalog.clear();initial.clear();
    resetRequests();resetModels();clearContentRows();resetThumbnails();thumbnailAssets.clear();
    if(!apiReady)return;
    try {refreshCatalog();}catch(const std::exception& e){fail(e.what());}
}
std::string status() {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    Json choices=Json::array(), mappings=Json::array();
    for(const auto& [id,c]:catalog) {
        Json costumes=Json::array(),hairs=Json::array();
        for(const auto& [key,v]:c.costumes)costumes.push_back({{"id",key},{"name",v.name},{"hair",v.hair},{"thumbnail",v.thumbnail},{"order",v.order}});
        for(const auto& [key,v]:c.hairs)hairs.push_back({{"id",key},{"name",v.name},{"wearable",v.wearable},{"excluded",v.excluded},
            {"ornament_base",v.ornamentBase},{"thumbnail",v.thumbnail},{"order",v.order}});
        choices.push_back({{"id",id},{"name",c.name},{"playable",c.playable},{"owned",c.owned},
            {"collaboration",c.collaboration},{"order",c.order},
            {"costumes",costumes},{"hairs",hairs},{"default_costume",c.appearance.costume},
            {"fixed_model",!c.modelAssets.empty()}});
    }
    for(const auto& r:rules)mappings.push_back({{"source",r.source},{"target",r.target},{"context",r.context},
        {"costume",r.appearance.costume},{"hair",r.appearance.hair},{"ornament",r.appearance.ornament}});
    return Json{{"normalize",normalize},{"limited",limited},{"collaborators",collaborators},
        {"request_available",requestReady},{"model_available",modelReady},{"limited_available",contentReady},
        {"character_available",characterReady},{"catalog",choices},{"rules",mappings},{"error",error},
        {"thumbnail_defaults",{{"hair",defaultHairThumbnail},{"ornament",defaultOrnamentThumbnail}}},
        {"normalized_requests",normalizedRequests},{"rendered_models",renderedModels}}.dump();
}
bool configure(const std::string& json) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    try {
        const auto data=Json::parse(json);
        bool n=data.at("normalize").get<bool>(),l=data.at("limited").get<bool>(),c=data.at("collaborators").get<bool>();
        std::vector<Rule> next;
        for(const auto& row:data.at("rules"))next.push_back({row.at("source"),row.at("target"),row.at("context"),
            {row.value("costume",std::string{}),row.value("hair",std::string{}),row.value("ornament",std::string{})}});
        if((n&&!requestReady)||(l&&!contentReady)||(c&&!characterReady)||(!next.empty()&&!modelReady))
            throw std::runtime_error("当前游戏版本的接口不可用");
        if(n&&initial.empty())throw std::runtime_error("初始拍摄数据尚未就绪，请重新打开游戏的拍摄页面");
        validateRules(next,catalog);
        if((l||c||!next.empty())&&!n)throw std::runtime_error("请先开启使用已有角色初始拍摄数据");
        normalize=n;limited=l;collaborators=c;collabPoses=c;rules=std::move(next);error.clear();return true;
    }catch(const std::exception& e){fail(e.what());return false;}
}
bool posesEnabled(){return collabPoses.load();}
std::string thumbnail(const std::string& asset) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!thumbnailReady || !thumbnailAssets.count(asset))return "!";
    auto found=thumbnailCache.find(asset);
    if(found!=thumbnailCache.end())return found->second;
    if((!thumbnailLoad || thumbnailLoad->asset!=asset) &&
       std::find(thumbnailQueue.begin(),thumbnailQueue.end(),asset)==thumbnailQueue.end() && thumbnailQueue.size()<24) {
        thumbnailQueue.push_back(asset);thumbnailsPending=true;
    }
    return {};
}
void processThumbnails() {
    if(!thumbnailsPending.load(std::memory_order_relaxed))return;
    std::lock_guard<std::recursive_mutex> guard(mutex);
    tickThumbnails();
}
void cancelThumbnails() {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    thumbnailQueue.clear();thumbnailReset=true;thumbnailsPending=true;
}
std::string poseMotionSource(const std::string& character) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    return collaborators?collaborationMotionSource(character):"";
}
}
