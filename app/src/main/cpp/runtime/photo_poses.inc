// Collaboration characters have complete models but no photography motion
// rows. Borrow an owned idol's motion set in a detached photo actor only.
std::string collaborationMotionSource(const std::string& character) {
    auto target=catalog.find(character);
    if(target==catalog.end() || !target->second.collaboration)return {};
    for(const auto& [id,base]:initial) {
        auto actor=catalog.find(id);
        if(actor!=catalog.end() && !actor->second.collaboration && !base.pose.empty() && !base.facial.empty())return id;
    }
    return {};
}
using SetPoseCostume=void(*)(void*,void*,void*,const Method*);
SetPoseCostume originalSetPoseCostume=nullptr;
void* poseActorCostumeField=nullptr;
const Method* actorCostumeConstructor=nullptr;

// The selection page also rejects characters without an owned default outfit.
// Limit this accommodation to its pose-scene validation call, preserving the
// normal ownership result everywhere else (including account and contest UI).
thread_local bool validatingPoseOutfits=false;
using SetNgCharacters=void(*)(void*,void*,const Method*);
SetNgCharacters originalSetNgCharacters=nullptr;
using HasDefaultCostume=bool(*)(void*,const Method*);
HasDefaultCostume originalHasDefaultCostume=nullptr;
void setNgCharacters(void* self,void* scene,const Method* method) {
    struct Restore { bool old=validatingPoseOutfits; ~Restore(){validatingPoseOutfits=old;} } restore;
    validatingPoseOutfits=scene&&api.objectClass(scene)==api.klass("Solis.Common.Proto.Master","PhotoExpression");
    originalSetNgCharacters(self,scene,method);
}
bool hasDefaultCostume(void* self,const Method* method) {
    if(validatingPoseOutfits) {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        if(collaborators&&self)try {
            if(!collaborationMotionSource(api.text(self,"CharacterId")).empty())return true;
        }catch(const std::exception& e){fail(e.what());}
    }
    return originalHasDefaultCostume(self,method);
}

void setPoseCostume(void* self,void* costume,void* impossible,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    Root original(api,costume),borrowed(api);
    std::string source,target;
    if(collaborators && costume)try {
        Root body(api,api.get(costume,"Costume"));target=api.text(body.get(),"CharacterId");
        source=collaborationMotionSource(target);
        if(!source.empty()) {
            const auto& base=initial.at(source);
            Root body(api,masterRow("Costume",base.appearance.costume)),hair(api,masterRow("Hair",base.appearance.hair));
            borrowed=Root(api,api.newObject(api.klass("Solis.Common.Data","ActorCostume")));
            void* args[]={body.get(),hair.get()};api.call(actorCostumeConstructor,borrowed.get(),args);
        }
    }catch(const std::exception& e){borrowed=Root(api);fail(e.what());}
    if(!borrowed.get()){originalSetPoseCostume(self,costume,impossible,method);return;}
    struct Restore {
        void* actor;void* costume;
        ~Restore(){api.fieldSetObject(actor,poseActorCostumeField,costume);}
    } restore{self,original.get()};
    originalSetPoseCostume(self,borrowed.get(),impossible,method);
    try {
        // Facial animations can be shared; another idol's voice must not be.
        Root facials(api,api.get(self,"PhotoFacialMotions"));
        // Preserve the concrete List<T> expected by SetFaceAsync. A managed
        // array fits a reference field but has a different native layout.
        Root list(api,create(api.objectClass(facials.get())));
        for(int i=0,n=count(facials.get(),1000);i<n;++i) {
            Root copy(api,clone(item(facials.get(),i)));
            setText(copy.get(),"VoiceAssetId","");
            append(list.get(),copy.get(),Master+std::string("PhotoFacialMotion"));
        }
        const auto listType=api.type(api.returnType(api.find(api.objectClass(self),"get_PhotoFacialMotions","@ref")));
        void* args[]={list.get()};api.call(api.find(api.objectClass(self),"set_PhotoFacialMotions","System.Void",{listType}),self,args);
        __android_log_print(ANDROID_LOG_INFO,Tag,"Collaboration photo motions: %s <- %s",target.c_str(),source.c_str());
    }catch(const std::exception& e){fail(e.what());}
}
bool installCollaborationPoses() {
    auto* actor=api.klass("Solis.Photography.Data","PhotographySpotData");
    poseActorCostumeField=actor?api.field(actor,"<ActorCostume>k__BackingField"):nullptr;
    if(!poseActorCostumeField || !api.matches(api.fieldType(poseActorCostumeField),"Solis.Common.Data.ActorCostume"))return false;
    actorCostumeConstructor=api.find(api.klass("Solis.Common.Data","ActorCostume"),".ctor","System.Void",{Master+std::string("Costume"),"@ref"});
    if(!actorCostumeConstructor)return false;
    bool ready=hook(api.find(api.klass("Solis.Photography.Data","PhotographyExpressionActor"),"SetActorCostume","System.Void",
        {"Solis.Common.Data.ActorCostume","@ref"}),setPoseCostume,originalSetPoseCostume);
    ready&=hook(api.find(api.klass("Solis.OutGame","PhotographySelectDataModel"),"SetNgCharacters","System.Void",
        {"Solis.Common.Data.IMasterSpotPhotographyData"}),setNgCharacters,originalSetNgCharacters);
    ready&=hook(api.find(api.klass("Solis.Common.Proto.Transaction","UserCharacter"),"HasDefaultCostume","System.Boolean"),
        hasDefaultCostume,originalHasDefaultCostume);
    return ready;
}
