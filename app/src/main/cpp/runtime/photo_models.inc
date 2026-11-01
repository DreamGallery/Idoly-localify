// Visual replacements are attached to real transition/ActorCostume instances.
// Weak handles prevent a closed scene from leaving a global active-scene flag.
struct ModelSession {
    uint32_t transition=0;
    std::vector<uint32_t> actors;
    unsigned context=0;
    uint64_t account=0;
    std::vector<Rule> rules;
    std::set<std::string> reported;
    ~ModelSession(){if(transition)api.release(transition);for(auto actor:actors)api.release(actor);}
};
std::vector<std::shared_ptr<ModelSession>> sessions;
struct WholeModel {
    uint32_t descriptor=0;
    std::set<std::string> assets;
    ~WholeModel(){if(descriptor)api.release(descriptor);}
};
std::vector<std::shared_ptr<WholeModel>> wholeModels;
std::vector<uint32_t> wholeActors;
thread_local std::shared_ptr<WholeModel> buildingWholeModel;
thread_local std::shared_ptr<ModelSession> rendering;
struct RenderScope {
    std::shared_ptr<ModelSession> previous;
    explicit RenderScope(std::shared_ptr<ModelSession> value):previous(std::move(rendering)){rendering=std::move(value);}
    ~RenderScope(){rendering=std::move(previous);}
};
void resetModels(){
    sessions.clear();wholeModels.clear();
    for(auto actor:wholeActors)api.release(actor);wholeActors.clear();
}
std::shared_ptr<ModelSession> forTransition(void* data) {
    for(const auto& s:sessions)if(s->account==account && api.target(s->transition)==data)return s;
    return {};
}
std::shared_ptr<ModelSession> forActor(void* actor) {
    for(const auto& s:sessions)if(s->account==account && api.target(s->transition))
        for(auto handle:s->actors)if(api.target(handle)==actor)return s;
    return {};
}
void registerTransition(void* data) {
    if(!data||forTransition(data))return;
    const std::string name=api.className(api.objectClass(data));
    static const std::map<std::string,unsigned> allowed{
        {"PhotographyLiveTransitionData",Live},{"PhotographyLiveScreenTransitionData",Live},
        {"SpecialPhotographyLiveTransitionData",Live},{"PhotographyExpressionTransitionData",Scene},
        {"PhotographyActivityTransitionData",Scene},{"SpecialPhotographyActivityTransitionData",Scene}};
    auto type=allowed.find(name);
    if(type==allowed.end()||rules.empty())return;
    sessions.erase(std::remove_if(sessions.begin(),sessions.end(),[](const auto& s){return !api.target(s->transition);}),sessions.end());
    if(sessions.size()>=16)throw std::runtime_error("仍有过多拍摄场景未释放，请重启游戏");
    auto s=std::make_shared<ModelSession>();s->transition=api.weak(data,false);s->account=account;
    s->context=type->second;s->rules=rules;
    if(!s->transition)throw std::runtime_error("Cannot retain photography transition");
    for(const char* property:{"ActorCostumeList","DisplayActorCostumeList"}) {
        Root list(api,api.get(data,property));
        for(int i=0,n=count(list.get(),10);i<n;++i){
            void* actor=item(list.get(),i);auto handle=api.weak(actor,false);
            if(!handle)throw std::runtime_error("Cannot retain photography actor");
            s->actors.push_back(handle);
        }
    }
    sessions.push_back(s);
    __android_log_print(ANDROID_LOG_INFO,Tag,"Model session: %s, actors=%zu rules=%zu",name.c_str(),s->actors.size(),s->rules.size());
}
using StaticOne=void*(*)(void*,const Method*);
std::array<StaticOne,3> originalDownloads{};
template<size_t I> void* downloads(void* data,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    try{registerTransition(data);}catch(const std::exception& e){fail(e.what());}
    return originalDownloads[I](data,method);
}
using Descriptor=void*(*)(void*,void*,void*,const Method*);
Descriptor originalDescriptor=nullptr;
void wholeModel(void* result,const Character& target,void* props) {
    auto assets=target.modelAssets;
    if(props)each(props,[&](void* name){assets.push_back(api.string(name));});
    replaceStrings(api.get(result,"assets"),assets);
    setText(result,"name",target.id.substr(5));
    auto model=std::make_shared<WholeModel>();model->descriptor=api.weak(result,false);
    if(!model->descriptor)throw std::runtime_error("Cannot retain standalone model descriptor");
    model->assets.insert(target.modelAssets.begin(),target.modelAssets.end());
    wholeModels.erase(std::remove_if(wholeModels.begin(),wholeModels.end(),[](const auto& v){return !api.target(v->descriptor);}),wholeModels.end());
    wholeModels.push_back(std::move(model));
}
void* descriptor(void* costume,void* hair,void* props,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(rendering && rendering->account==account && costume)try {
        auto source=api.text(costume,"CharacterId");
        auto* rule=replacement(rendering->rules,source,rendering->context);
        if(rule){
            const auto& target=catalog.at(rule->target);
            auto appearance=selectedAppearance(*rule,target);
            Root result(api);
            if(!target.modelAssets.empty()) {
                result=Root(api,originalDescriptor(costume,hair,props,method));
                wholeModel(result.get(),target,props);
            }
            else if(target.assetAppearance) {
                result=Root(api,originalDescriptor(costume,hair,props,method));
                const auto& head=target.hairs.at(appearance.hair);
                std::vector<std::string> assets{appearance.costume,head.faceAsset,appearance.hair};
                if(props)each(props,[&](void* name){assets.push_back(api.string(name));});
                replaceStrings(api.get(result.get(),"assets"),assets);
                setText(result.get(),"name",target.modelName);
            }
            else {
                Root body(api,masterRow("Costume",appearance.costume)),head(api,masterRow("Hair",appearance.hair));
                if(!body.get()||!head.get())throw std::runtime_error("Replacement model no longer exists");
                result=Root(api,originalDescriptor(body.get(),head.get(),props,method));
            }
            ++renderedModels;
            if(rendering->reported.insert(source+"/"+target.id).second)
                __android_log_print(ANDROID_LOG_INFO,Tag,"Visual model: %s -> %s costume=%s hair=%s",source.c_str(),target.id.c_str(),appearance.costume.c_str(),appearance.hair.c_str());
            return result.get();
        }
    }catch(const std::exception& e){fail(e.what());}
    return originalDescriptor(costume,hair,props,method);
}

// The mascot has a humanoid chain but names its hand endpoints Arm_End.
// Adapt only the freshly instantiated parts of a marked photo descriptor;
// neither the cached prefab nor ordinary ADV actors are modified.
using BuildModel=void(*)(void*,void*,const Method*);
BuildModel originalBuildModel=nullptr;
void buildModel(void* self,void* resources,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(wholeModels.empty()){originalBuildModel(self,resources,method);return;}
    auto previous=buildingWholeModel;buildingWholeModel.reset();
    try {
        Root description(api,api.get(self,"descriptor"));
        for(const auto& candidate:wholeModels)if(api.target(candidate->descriptor)==description.get()){buildingWholeModel=candidate;break;}
        if(buildingWholeModel) {
            wholeActors.erase(std::remove_if(wholeActors.begin(),wholeActors.end(),[](auto h){if(api.target(h))return false;api.release(h);return true;}),wholeActors.end());
            bool existing=false;for(auto h:wholeActors)if(api.target(h)==self){existing=true;break;}
            if(!existing){auto h=api.weak(self,false);if(!h)throw std::runtime_error("Cannot retain standalone actor");wholeActors.push_back(h);}
        }
    }catch(const std::exception& e){fail(e.what());}
    struct Restore {std::shared_ptr<WholeModel> previous;~Restore(){buildingWholeModel=std::move(previous);}} restore{std::move(previous)};
    originalBuildModel(self,resources,method);
}
bool isWholeActor(void* actor) {
    for(auto handle:wholeActors)if(api.target(handle)==actor)return true;
    return false;
}
using EyeCorrection=void(*)(void*,const Method*);
EyeCorrection originalEyeCorrection=nullptr;
void eyeCorrection(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!isWholeActor(self))originalEyeCorrection(self,method);
}
using LookAtEffector=void(*)(void*,void*,const Method*);
LookAtEffector originalLookAtEffector=nullptr;
void lookAtEffector(void* self,void* effector,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!isWholeActor(self))originalLookAtEffector(self,effector,method);
}
using InitializePart=void(*)(void*,void*,void*,void*,const Method*);
InitializePart originalInitializePart=nullptr;
void initializePart(void* self,void* id,void* asset,void* root,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    Root partId(api,id);
    if(buildingWholeModel && buildingWholeModel->assets.count(api.string(asset)))try {
        if(api.string(asset)=="mdl_oth_gyogyottosan-normal-00") {
            Root transform(api,api.get(self,"transform","UnityEngine.Transform"));
            std::vector<Root> pending;pending.emplace_back(api,transform.get());
            int aliases=0;
            for(size_t i=0;i<pending.size();++i) {
                if(i>=512)throw std::runtime_error("Unexpected standalone skeleton size");
                auto node=pending[i].get();auto name=api.text(node,"name");
                if(name=="LeftArm_End" || name=="RightArm_End") {
                    setText(node,"name",name=="LeftArm_End"?"LeftHand":"RightHand");++aliases;
                }
                int children=api.value<int>(api.get(node,"childCount","System.Int32"));
                auto* child=api.find(api.objectClass(node),"GetChild","UnityEngine.Transform",{"System.Int32"});
                for(int j=0;j<children;++j){void* args[]={&j};pending.emplace_back(api,api.call(child,node,args));}
            }
            if(aliases!=2)throw std::runtime_error("Standalone model hand endpoints changed");
            partId=Root(api,api.newString("body"));
            __android_log_print(ANDROID_LOG_INFO,Tag,"Standalone skeleton adapted: %s",api.string(asset).c_str());
        }
    }catch(const std::exception& e){fail(e.what());}
    originalInitializePart(self,partId.get(),asset,root,method);
}

// Generated iterator names may change their numeric suffix between releases.
// Bind their typed field and method signatures rather than an address/offset.
void* nestedPrefix(void* parent,const char* prefix) {
    void* it=nullptr;void* found=nullptr;
    while(void* child=api.nestedTypes(parent,&it))if(std::string(api.className(child)).rfind(prefix,0)==0){
        if(found)throw std::runtime_error("Ambiguous photography generated type");found=child;
    }
    return found;
}
struct CapturedReference {void* klass=nullptr;void* field=nullptr;bool value=false;};
CapturedReference captured(void* klass,const char* name,const std::string& expected) {
    void* field=klass?api.field(klass,name):nullptr;
    if(!field||!api.matches(api.fieldType(field),expected))throw std::runtime_error("Photography capture field changed");
    bool value=api.isValueType(klass);
    if(value){uint32_t alignment=0;auto size=api.valueSize(klass,&alignment);auto offset=api.fieldOffset(field);
        if(offset<2*sizeof(void*)||offset+sizeof(void*)>size_t(size)+2*sizeof(void*))throw std::runtime_error("Invalid state-machine field layout");}
    return {klass,field,value};
}
void* capturedValue(const CapturedReference& field,void* instance) {
    void* result=nullptr;
    if(field.value)std::memcpy(&result,static_cast<char*>(instance)+api.fieldOffset(field.field)-2*sizeof(void*),sizeof(result));
    else api.fieldGet(instance,field.field,&result);
    return result;
}
using Move=bool(*)(void*,const Method*);
std::array<Move,2> originalIteratorMoves{};
std::array<CapturedReference,2> iteratorData{};
template<size_t I> bool iteratorMove(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    void* data=capturedValue(iteratorData[I],self);
    try{registerTransition(data);}catch(const std::exception& e){fail(e.what());}
    RenderScope scope(forTransition(data));
    return originalIteratorMoves[I](self,method);
}
using Lambda=void*(*)(void*,void*,const Method*);
Lambda originalLiveActor=nullptr;
void* liveActor(void* self,void* actor,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    RenderScope scope(forActor(actor));return originalLiveActor(self,actor,method);
}
struct Task{void* source;int16_t token;};
struct Cancellation{void* source;};
using InitializeSpot=Task(*)(void*,Cancellation,const Method*);
InitializeSpot originalSpot=nullptr;
Task spot(void* self,Cancellation ct,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    std::shared_ptr<ModelSession> session;
    try{session=forActor(api.get(self,"ActorCostume"));}catch(const std::exception& e){fail(e.what());}
    // In 6.1.0 the descriptor is built before the first await. The resumed
    // state only handles the already loaded actor and does not build a model.
    RenderScope scope(session);return originalSpot(self,ct,method);
}
using MoveVoid=void(*)(void*,const Method*);
MoveVoid originalLiveInitialize=nullptr;
CapturedReference liveInitializeData;
void liveInitialize(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    void* data=capturedValue(liveInitializeData,self);
    try{registerTransition(data);}catch(const std::exception& e){fail(e.what());}
    RenderScope scope(forTransition(data));
    originalLiveInitialize(self,method);
}
MoveVoid originalExpressionInitialize=nullptr;
CapturedReference expressionInitializeData;
void expressionInitialize(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    void* data=capturedValue(expressionInitializeData,self);
    try{registerTransition(data);}catch(const std::exception& e){fail(e.what());}
    RenderScope scope(forTransition(data));
    originalExpressionInitialize(self,method);
}
bool taskLayout() {
    auto* task=api.klass("Cysharp.Threading.Tasks","UniTask","UniTask.dll");
    auto* ct=api.klass("System.Threading","CancellationToken","mscorlib.dll");
    uint32_t alignment=0;
    if(!task||!ct||!api.isValueType(task)||!api.isValueType(ct)||api.valueSize(task,&alignment)!=sizeof(Task)||api.valueSize(ct,&alignment)!=sizeof(Cancellation))return false;
    for(auto f:std::vector<std::pair<const char*,size_t>>{{"source",0},{"token",offsetof(Task,token)}}){
        auto* field=api.field(task,f.first);if(!field||api.fieldOffset(field)!=2*sizeof(void*)+f.second)return false;
    }
    auto* field=api.field(ct,"_source");return field&&api.fieldOffset(field)==2*sizeof(void*);
}
bool installModels() {
    if(!taskLayout())return false;
    auto* utility=api.klass("Solis.Photography.Data","PhotographyDataUtility");
    auto* actor=api.find(api.klass("Solis.Common","SolisActorUtility"),"CreateActorDescriptor","Solis.Common.SolisActorDescriptor",
        {"Solis.Common.Proto.Master.Costume","Solis.Common.Proto.Master.Hair","System.Collections.Generic.IEnumerable<System.String>"},true);
    auto* liveIterator=nestedPrefix(utility,"<LiveTransitionDataToAssetNames>");
    auto* sceneIterator=nestedPrefix(utility,"<ActivityTransitionDataToAssetNames>");
    iteratorData[0]=captured(liveIterator,"data","Solis.Photography.Data.PhotographyLiveTransitionData");
    iteratorData[1]=captured(sceneIterator,"data","Solis.Photography.Data.ISpotPhotographyTransitionData");
    auto* liveState=nestedPrefix(api.klass("Solis.Photography","PhotographyLiveModePresenter"),"<InitializeAsync>");
    liveInitializeData=captured(liveState,"data","Solis.Photography.Data.PhotographyLiveTransitionData");
    auto* expressionState=nestedPrefix(api.klass("Solis.Photography","PhotographyExpression3DPresenter"),"<InitializeAsync>");
    expressionInitializeData=captured(expressionState,"data","Solis.Photography.Data.ISpotPhotographyTransitionData");
    auto* closure=nestedPrefix(api.klass("Solis.Photography.Data","PhotographyLiveTransitionData"),"<>c");
    const Method* lambda=nullptr;void* it=nullptr;
    while(auto* m=api.methods(closure,&it))if(std::string(api.methodName(m)).rfind("<CreateLiveData>",0)==0 && api.paramCount(m)==1 &&
        api.type(api.paramType(m,0))=="Solis.Common.Data.ActorCostume" && api.type(api.returnType(m))=="Solis.Common.SolisActorDescriptor")lambda=m;
    bool ready=hook(actor,descriptor,originalDescriptor);
    auto* controller=api.klass("Solis.Common","SolisActorController","solis-submodule.Runtime.dll");
    ready&=hook(api.find(controller,"BuildModel","System.Void",{"System.Collections.Generic.IEnumerable<UnityEngine.GameObject>"}),buildModel,originalBuildModel);
    ready&=hook(api.find(controller,"UpdateLookAtEyePitchCorrection","System.Void"),eyeCorrection,originalEyeCorrection);
    ready&=hook(api.find(controller,"SetSolisCustomLookAtEffector","System.Void",{"Solis.Common.SolisCustomLookAtEffector"}),lookAtEffector,originalLookAtEffector);
    ready&=hook(api.find(api.klass("Solis.Common","SolisActorModelParts","solis-submodule.Runtime.dll"),"Initialize","System.Void",
        {"System.String","System.String","UnityEngine.Transform"}),initializePart,originalInitializePart);
    ready&=hook(api.find(utility,"GetDownloadTargets","@ref",{"Solis.Photography.Data.IPhotographyTransition"},true),downloads<0>,originalDownloads[0]);
    ready&=hook(api.find(utility,"GetDownloadTargets","@ref",{"Solis.Photography.Data.PhotographyLiveTransitionData"},true),downloads<1>,originalDownloads[1]);
    ready&=hook(api.find(utility,"GetDownloadTargets","@ref",{"Solis.Photography.Data.ISpotPhotographyTransitionData"},true),downloads<2>,originalDownloads[2]);
    ready&=hook(api.find(liveIterator,"MoveNext","System.Boolean"),iteratorMove<0>,originalIteratorMoves[0]);
    ready&=hook(api.find(sceneIterator,"MoveNext","System.Boolean"),iteratorMove<1>,originalIteratorMoves[1]);
    ready&=hook(lambda,liveActor,originalLiveActor);
    ready&=hook(api.find(api.klass("Solis.Photography.Data","PhotographySpotData"),"InitializeAsync","Cysharp.Threading.Tasks.UniTask",{"System.Threading.CancellationToken"}),spot,originalSpot);
    ready&=hook(api.find(liveState,"MoveNext","System.Void"),liveInitialize,originalLiveInitialize);
    ready&=hook(api.find(expressionState,"MoveNext","System.Void"),expressionInitialize,originalExpressionInitialize);
    return ready;
}
