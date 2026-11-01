// Included only by photo_experiments.cpp. This boundary receives protobuf
// requests before transport encryption; it never stores image bytes or tokens.
struct PhotoPosition { int number; std::string initialPose; };
struct PhotoOption {
    std::string id;
    std::vector<int> amounts;
    std::vector<PhotoPosition> positions;
    bool realtime=false;
    std::vector<std::string> stages;
    std::vector<int> excludedStageTypes;
    int stageType=0;
    bool hiddenStage=false;
};
struct OriginalOptions {
    std::vector<PhotoOption> activities, musics, stages, expressions;
} originalOptions;
struct RequestPlan {
    std::vector<std::string> before, after;
    std::map<std::string,InitialCharacter> defaults;
    std::string expression, activity, music, stage;
    int position=0;
    std::string scene;
    uint64_t sequence=0;
};
std::map<std::string,RequestPlan> requestPlans;
uint64_t requestSequence=0;
using Serialize = void(*)(void*,void*,void*,const Method*);
Serialize originalSerialize=nullptr;
void* invalidOperationClass=nullptr;

void resetRequests() { originalOptions={};requestPlans.clear();requestSequence=0; }

std::vector<int> numbers(void* list) {
    std::vector<int> out;
    const auto* getter=api.find(api.objectClass(list),"get_Item","System.Int32",{"System.Int32"});
    if(!getter)getter=api.find(api.objectClass(list),"get_Item","@enum",{"System.Int32"});
    for(int i=0,n=count(list,100);i<n;++i){void* args[]={&i};out.push_back(api.value<int>(api.call(getter,list,args)));}
    return out;
}
void replaceNumbers(void* list,const std::vector<int>& values,bool enumeration=false) {
    clear(list);
    auto* add=api.find(api.objectClass(list),"Add","System.Void",{enumeration?"@enum":"System.Int32"});
    for(auto value:values){void* args[]={&value};api.call(add,list,args);}
}
bool has(void* object,const char* property) {
    return api.find(api.objectClass(object),(std::string("get_")+property).c_str(),"@ref")!=nullptr;
}
std::string selectOption(const std::vector<PhotoOption>& options,int characters,const std::string& original) {
    auto compatible=[&](const PhotoOption& o){return o.amounts.empty()||std::find(o.amounts.begin(),o.amounts.end(),characters)!=o.amounts.end();};
    // An already unlocked scene can keep its ID; only a local extension needs
    // an owned baseline scene. This preserves legitimate per-scene progress.
    for(const auto& o:options)if(o.id==original&&compatible(o))return o.id;
    for(const auto& o:options)if(compatible(o))return o.id;
    throw std::runtime_error("当前账号没有人数相符的已开放拍摄场景");
}
std::pair<std::string,std::string> selectLiveOptions(const OriginalOptions& options,
                                                   const std::string& music,const std::string& stage) {
    // A music and a stage may both be unlocked but invalid as a pair. Use the
    // game's stage restrictions and never substitute a video-only song for 3D.
    auto ordered=[](const auto& values,const auto& preferred){
        std::vector<const PhotoOption*> result;
        for(const auto& value:values)if(value.id==preferred)result.push_back(&value);
        for(const auto& value:values)if(value.id!=preferred)result.push_back(&value);
        return result;
    };
    for(const auto* m:ordered(options.musics,music)) {
        if(!m->realtime)continue;
        for(const auto* s:ordered(options.stages,stage)) {
            if(s->hiddenStage&&std::find(m->stages.begin(),m->stages.end(),s->id)==m->stages.end())continue;
            if(!m->stages.empty()&&std::find(m->stages.begin(),m->stages.end(),s->id)==m->stages.end())continue;
            if(std::find(m->excludedStageTypes.begin(),m->excludedStageTypes.end(),s->stageType)!=m->excludedStageTypes.end())continue;
            return {m->id,s->id};
        }
    }
    throw std::runtime_error("当前账号没有可搭配的已开放歌曲与舞台");
}
std::pair<std::string,int> selectExpression(const std::vector<PhotoOption>& options,
                                          const std::vector<std::string>& poses,const std::string& original) {
    // Position numbers are business keys, often starting at 1. Select a real
    // position compatible with the initial poses; never manufacture slot 0.
    auto position=[&](const PhotoOption& option)->int {
        if(!option.amounts.empty() && std::find(option.amounts.begin(),option.amounts.end(),int(poses.size()))==option.amounts.end())return -1;
        for(const auto& slot:option.positions)if(slot.number>=0 && !slot.initialPose.empty() &&
            std::all_of(poses.begin(),poses.end(),[&](const auto& pose){return pose==slot.initialPose;}))return slot.number;
        return -1;
    };
    if(poses.empty())throw std::runtime_error("拍摄初始姿势列表为空");
    for(const auto& option:options)if(option.id==original){int n=position(option);if(n>=0)return {option.id,n};}
    for(const auto& option:options){int n=position(option);if(n>=0)return {option.id,n};}
    throw std::runtime_error("当前账号没有人数及初始姿势相符的已开放拍摄场景");
}
std::string requestKind(const char* name) {
    static const std::map<std::string,std::string> allowed{
        {"PhotoCheckShootingRequest","normal"},{"PhotoCreateShootingsRequest","normal"},
        {"PhotoCheckExpressionShootingRequest","expression"},{"PhotoCreateExpressionShootingsRequest","expression"},
        {"PhotoCheckSpecialShootingRequest","special"},{"PhotoCreateSpecialShootingsRequest","special"},
        {"PhotoCheckExcursionShootingRequest","excursion"},{"PhotoCreateExcursionShootingsRequest","excursion"}};
    auto it=allowed.find(name?name:"");return it==allowed.end()?"":it->second;
}

std::string normalizeCharacter(const RequestPlan& plan,const std::string& id) {
    auto it=std::find(plan.before.begin(),plan.before.end(),id);
    if(it==plan.before.end())throw std::runtime_error("拍摄条目的角色与选择列表不一致");
    return plan.after.at(size_t(it-plan.before.begin()));
}
bool frozenPlan(const std::string& scene,const std::vector<std::string>& characters,RequestPlan& result) {
    const RequestPlan* latest=nullptr;
    for(const auto& [key,plan]:requestPlans) {
        if(plan.scene!=scene || (latest && latest->sequence>=plan.sequence))continue;
        if(!std::all_of(characters.begin(),characters.end(),[&](const auto& id){
            return std::find(plan.before.begin(),plan.before.end(),id)!=plan.before.end();
        }))continue;
        latest=&plan;
    }
    if(!latest)return false;
    result=*latest;result.before=characters;result.after.clear();
    // A duet is checked as a five-slot formation but saved with only its two
    // visible actors. Retain each actor's original mapping, in request order.
    for(const auto& id:characters)result.after.push_back(normalizeCharacter(*latest,id));
    return true;
}
void discardPlans(const std::string& scene) {
    for(auto it=requestPlans.begin();it!=requestPlans.end();)
        if(it->second.scene==scene)it=requestPlans.erase(it);else ++it;
}
void normalizeItem(void* value,const RequestPlan& plan,bool expression) {
    if(has(value,"MainCharacterId")) {
        auto id=api.text(value,"MainCharacterId");
        if(!id.empty())setText(value,"MainCharacterId",normalizeCharacter(plan,id));
    }
    if(!expression)return;
    Root ids(api,api.get(value,"CharacterIds"));auto before=strings(ids.get());
    std::vector<std::string> after,poses,facials;std::vector<int> looks;
    for(const auto& id:before){
        auto mapped=normalizeCharacter(plan,id);const auto& base=plan.defaults.at(mapped);
        if(base.pose.empty()||base.facial.empty())throw std::runtime_error("找不到已有角色的初始姿势或表情");
        after.push_back(mapped);poses.push_back(base.pose);facials.push_back(base.facial);looks.push_back(base.looking);
    }
    replaceStrings(ids.get(),after);
    replaceStrings(api.get(value,"PhotoPoseIds"),poses);
    replaceStrings(api.get(value,"PhotoFacialIds"),facials);
    replaceNumbers(api.get(value,"PhotoLookingTypes"),looks,true);
    // Legacy single-character fields coexist with the repeated fields.
    const auto& base=plan.defaults.at(plan.after.front());
    if(base.pose.empty()||base.facial.empty())throw std::runtime_error("找不到初始姿势或表情");
    setText(value,"PhotoPoseId",base.pose);setText(value,"PhotoFacialId",base.facial);
    setNumber(value,"PhotoLookingType",base.looking,true);
    // Position numbers are scene layout slots, not resource IDs. They must
    // describe the baseline scene when a limited scene was substituted.
    setNumber(value,"StagePositionNumber",plan.position);
}

void* normalizedRequest(void* request,const std::string& kind) {
    std::vector<std::string> before;
    if(has(request,"CharacterIds"))before=strings(api.get(request,"CharacterIds"));
    if(before.empty()&&has(request,"CharacterId"))before.push_back(api.text(request,"CharacterId"));
    if(before.empty()||before.size()>10)throw std::runtime_error("拍摄角色列表为空或超出支持人数");
    Json identity{{"kind",kind}};
    for(const char* p:{"PhotoExpressionId","PhotoActivityId","PhotoMusicId","PhotoStageId","SpecialPhotoShootingId","ExcursionPlaceId"})
        identity[p]=has(request,p)?api.text(request,p):"";
    const auto scene=identity.dump();
    identity["characters"]=before;
    const auto key=identity.dump();
    const bool check=std::string(api.className(api.objectClass(request))).find("Check")!=std::string::npos;
    // A new check after disabling starts a normal session. An old experiment
    // on the same song/scene must not affect that session's later save.
    if(check&&!normalize){discardPlans(scene);return request;}
    RequestPlan plan;
    const bool frozen=!check&&frozenPlan(scene,before,plan);
    if(!normalize&&!frozen)return request;
    if(!frozen) {
        plan.scene=scene;plan.sequence=++requestSequence;
        std::vector<std::string> owned;
        for(const auto& [id,value]:initial)
            if(kind!="expression"||(!value.pose.empty()&&!value.facial.empty()))owned.push_back(id);
        // Choose once per check. The frozen plan, including saves containing
        // only a subset of the formation, keeps this identity for the session.
        static std::mt19937 random(std::random_device{}());
        plan.before=before;plan.after=baselineCharacters(before,owned,random);
        for(const auto& id:plan.after)plan.defaults.emplace(id,initial.at(id));
        if(kind=="expression") {
            const auto expression=api.text(request,"PhotoExpressionId");
            Root sceneRow(api,masterRow("PhotoExpression",expression));
            const auto amounts=numbers(api.get(sceneRow.get(),"CharacterAmountArray"));
            // Like Live, the check can include unused formation slots. The
            // expression's declared cast size determines its baseline scene.
            if(amounts.size()!=1 || amounts.front()<1 || size_t(amounts.front())>plan.after.size())
                throw std::runtime_error("无法确定当前拍摄场景的实际出镜人数");
            std::vector<std::string> poses;
            for(int i=0;i<amounts.front();++i)poses.push_back(plan.defaults.at(plan.after[i]).pose);
            auto selected=selectExpression(originalOptions.expressions,poses,expression);
            plan.expression=std::move(selected.first);plan.position=selected.second;
        }
        if(kind=="normal") {
            auto aid=api.text(request,"PhotoActivityId"),mid=api.text(request,"PhotoMusicId"),sid=api.text(request,"PhotoStageId");
            if(!aid.empty())plan.activity=selectOption(originalOptions.activities,int(before.size()),aid);
            if(!mid.empty()&&!sid.empty()) {
                auto selected=selectLiveOptions(originalOptions,mid,sid);
                plan.music=std::move(selected.first);plan.stage=std::move(selected.second);
            }else {
                if(!mid.empty())plan.music=selectOption(originalOptions.musics,int(before.size()),mid);
                if(!sid.empty())plan.stage=selectOption(originalOptions.stages,int(before.size()),sid);
            }
        }
    }
    Root copy(api,clone(request));
    std::vector<std::string> costumes,hairs;
    for(const auto& id:plan.after){costumes.push_back(plan.defaults.at(id).appearance.costume);hairs.push_back(plan.defaults.at(id).appearance.hair);}
    if(has(copy.get(),"CharacterIds")) {
        replaceStrings(api.get(copy.get(),"CharacterIds"),plan.after);
        replaceStrings(api.get(copy.get(),"CostumeIds"),costumes);
        replaceStrings(api.get(copy.get(),"HairIds"),hairs);
    }
    if(has(copy.get(),"CharacterId")) {
        const auto single=api.text(copy.get(),"CharacterId");
        if(!single.empty()) {
            const auto target=normalizeCharacter(plan,single);const auto& base=plan.defaults.at(target);
            setText(copy.get(),"CharacterId",target);setText(copy.get(),"CostumeId",base.appearance.costume);setText(copy.get(),"HairId",base.appearance.hair);
        }
    }
    if(!plan.expression.empty())setText(copy.get(),"PhotoExpressionId",plan.expression);
    if(!plan.activity.empty())setText(copy.get(),"PhotoActivityId",plan.activity);
    if(!plan.music.empty())setText(copy.get(),"PhotoMusicId",plan.music);
    if(!plan.stage.empty())setText(copy.get(),"PhotoStageId",plan.stage);
    for(const char* p:{"CreateExpressionShootingParams","CreateShootingParams"})if(has(copy.get(),p)) {
        Root entries(api,api.get(copy.get(),p));
        for(int i=0,n=count(entries.get(),100);i<n;++i)normalizeItem(item(entries.get(),i),plan,kind=="expression");
    }
    std::string identities;
    for(size_t i=0;i<plan.before.size();++i)
        identities+=(i?",":"")+plan.before[i]+"->"+plan.after[i];
    if(kind=="normal"&&!frozen)
        __android_log_print(ANDROID_LOG_INFO,Tag,"Photo baseline: music=%s stage=%s activity=%s",
            plan.music.c_str(),plan.stage.c_str(),plan.activity.c_str());
    requestPlans[key]=std::move(plan);++normalizedRequests;
    if(requestPlans.size()>32) {
        auto oldest=std::min_element(requestPlans.begin(),requestPlans.end(),[](const auto& a,const auto& b){return a.second.sequence<b.second.sequence;});
        requestPlans.erase(oldest);
    }
    __android_log_print(ANDROID_LOG_INFO,Tag,"Normalized %s, slots=%zu frozen=%d identities=%s (request copy)",
        api.className(api.objectClass(request)),before.size(),frozen,identities.c_str());
    return copy.get();
}

void serialize(void* self,void* request,void* context,const Method* method) {
    const char* ns=request?api.classNamespace(api.objectClass(request)):nullptr;
    auto kind=ns&&std::strcmp(ns,"Solis.Common.Proto.Api")==0?requestKind(api.className(api.objectClass(request))):"";
    if(kind.empty()){originalSerialize(self,request,context,method);return;}
    std::string failure;
    std::unique_ptr<Root> copy;
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        if(!normalize&&requestPlans.empty()){originalSerialize(self,request,context,method);return;}
        try {copy=std::make_unique<Root>(api,normalizedRequest(request,kind));}
        catch(const std::exception& e){failure=e.what();fail(failure);}
    }
    if(!failure.empty()) {
        // A failed normalization must not silently send the experimental IDs.
        // Raising a managed exception lets gRPC report the failed call normally.
        void* exception=api.exception(api.classImage(invalidOperationClass),"System","InvalidOperationException",failure.c_str());
        api.raise(exception);return;
    }
    originalSerialize(self,copy->get(),context,method);
}
bool installRequests() {
    invalidOperationClass=api.klass("System","InvalidOperationException","mscorlib.dll");
    if(!invalidOperationClass)return false;
    return hook(api.find(api.klass("Qua.Network","DefaultMarshallerFactory","quaunity-api.Runtime.dll"),"Serialize","System.Void",
        {"Google.Protobuf.IMessage","Grpc.Core.SerializationContext"}),serialize,originalSerialize);
}
