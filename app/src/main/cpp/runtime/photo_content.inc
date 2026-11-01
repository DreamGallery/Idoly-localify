// Only detached photography page records are expanded. Conditions/characters
// in the global MasterDB and UserDataManager are never replaced or updated.
struct ContentRow { uint32_t object=0; std::string table; };
std::vector<ContentRow> contentRows;
uint32_t originalPhotoResponse=0;
void clearContentRows(){for(auto& row:contentRows)api.release(row.object);contentRows.clear();if(originalPhotoResponse)api.release(originalPhotoResponse);originalPhotoResponse=0;}
bool isContentRow(void* object) {
    for(const auto& row:contentRows)if(api.target(row.object)==object)return true;
    return false;
}
bool hasStarted(const std::string& id,int depth=0) {
    if(id.empty())return true;
    if(depth>8)return false;
    Root condition(api,masterRow("Condition",id));
    if(!condition.get())return false;
    Root settings(api,api.get(condition.get(),"Settings"));
    const int64_t now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for(int i=0,n=count(settings.get(),100);i<n;++i){
        Root setting(api,item(settings.get(),i)),term(api,api.get(setting.get(),"Term"));
        if(term.get()) {
            int64_t start=api.value<int64_t>(api.get(term.get(),"NowAfter","System.Int64"));
            if(start>0 && start<100000000000LL)start*=1000;
            if(start>now)continue;
        }
        Root satisfy(api,api.get(setting.get(),"SatisfyCondition"));
        if(satisfy.get()&&!hasStarted(api.text(satisfy.get(),"ConditionId"),depth+1))continue;
        return true;
    }
    return count(settings.get(),100)==0;
}
bool hasDeadline(const std::string& id,int depth=0) {
    if(id.empty()||depth>8)return false;
    Root condition(api,masterRow("Condition",id));if(!condition.get())return false;
    Root settings(api,api.get(condition.get(),"Settings"));
    for(int i=0,n=count(settings.get(),100);i<n;++i){
        Root setting(api,item(settings.get(),i));
        // Term is an absolute availability interval. Time is a recurring
        // time-of-day window and must not be treated as expired content.
        Root term(api,api.get(setting.get(),"Term"));
        if(term.get()&&api.value<int64_t>(api.get(term.get(),"NowBefore","System.Int64"))>0)return true;
        Root other(api,api.get(setting.get(),"SatisfyCondition"));
        if(other.get()&&hasDeadline(api.text(other.get(),"ConditionId"),depth+1))return true;
    }
    return false;
}
void captureOptions(void* list,const char* idProperty,const char* table,std::vector<PhotoOption>& result) {
    result.clear();
    for(int i=0,n=count(list);i<n;++i){
        Root value(api,item(list,i));
        if(!api.value<bool>(api.get(value.get(),"IsUnlocked","System.Boolean")))continue;
        PhotoOption option{api.text(value.get(),idProperty),{}};
        Root row(api,masterRow(table,option.id));
        if(!row.get())continue;
        // IsUnlocked alone can remain true on expired entries. Baselines must
        // also satisfy the original current visibility condition.
        auto conditionId=api.text(row.get(),"ViewConditionId");
        if(!conditionId.empty()) {
            Root condition(api,masterRow("Condition",conditionId));void* args[]={condition.get()};
            if(!api.value<bool>(api.call(api.find(api.klass("Solis.Common.Extensions","ConditionExtensions"),
                "IsSatisfied","System.Boolean",{Master+std::string("Condition")},true),nullptr,args)))continue;
        }
        if(std::strcmp(table,"PhotoQuestMusic")==0) {
            Root music(api,masterRow("Music",option.id));
            option.realtime=api.value<bool>(api.get(music.get(),"Is3DAvailable","System.Boolean"));
            option.stages=strings(api.get(music.get(),"SelectableStageIds"));
            option.excludedStageTypes=numbers(api.get(music.get(),"NotSelectableStageTypes"));
        }else if(std::strcmp(table,"PhotoQuestStage")==0) {
            Root stage(api,masterRow("Stage",option.id));
            option.stageType=api.value<int>(api.get(stage.get(),"Type","@enum"));
            option.hiddenStage=api.value<bool>(api.get(stage.get(),"IsHideWithPhotoShooting","System.Boolean"));
        }
        if(std::strcmp(table,"PhotoExpression")==0) {
            option.amounts=numbers(api.get(row.get(),"CharacterAmountArray"));
            const auto stage=api.text(row.get(),"StageId");
            Root positions(api,allMaster("PhotoExpressionStagePosition"));
            for(int j=0,m=count(positions.get(),1000);j<m;++j){
                Root position(api,item(positions.get(),j));
                if(api.text(position.get(),"StageId")==stage)
                    option.positions.push_back({api.value<int>(api.get(position.get(),"Number","System.Int32")),api.text(position.get(),"InitialPhotoPoseId")});
            }
            std::sort(option.positions.begin(),option.positions.end(),[](const auto& a,const auto& b){return a.number<b.number;});
        }
        // Music.DisplayCharacterAmount is the visible cast, not the request's
        // formation length. PhotoQuestStage.MaxCapacity is audience capacity.
        result.push_back(std::move(option));
    }
}
void expandContent(void* list,const char* table,const char* rowType,const char* idProperty) {
    std::map<std::string,void*> existing;
    for(int i=0,n=count(list);i<n;++i){void* row=item(list,i);existing.emplace(api.text(row,idProperty),row);}
    Root rows(api,allMaster(table));
    for(int i=0,n=count(rows.get());i<n;++i){
        Root master(api,item(rows.get(),i));auto id=api.text(master.get(),"Id");
        const auto condition=api.text(master.get(),"ViewConditionId");
        // Expired scenery and the game's collaboration music namespace. Normal
        // progression-locked songs/stages remain governed by the original list.
        bool candidate=hasDeadline(condition) || (std::strcmp(table,"PhotoQuestMusic")==0 && id.rfind("music-clb-",0)==0);
        if(!candidate||!hasStarted(condition))continue;
        Root value(api,existing.count(id)?existing.at(id):create(api.klass("Solis.Common.Proto.Api",rowType)));
        setText(value.get(),idProperty,id);bool yes=true;void* args[]={&yes};
        api.call(api.find(api.objectClass(value.get()),"set_IsUnlocked","System.Void",{"System.Boolean"}),value.get(),args);
        if(!existing.count(id))append(list,value.get(),Proto+std::string(rowType));
        auto weak=api.weak(value.get(),false);if(weak)contentRows.push_back({weak,table});
    }
}
using ResponseClone=void*(*)(void*,const Method*);
ResponseClone originalResponseClone=nullptr;
const Method* responseCloneMethod=nullptr;
std::array<void*,3> responseFields{};
void copyResponseLists(void* to,void* from) {
    for(auto* field:responseFields){
        void* value=nullptr;api.fieldGet(from,field,&value);
        // Reference fields take the object itself, not a pointer to a local
        // variable. Use the explicit object setter and its GC write barrier.
        api.fieldSetObject(to,field,value);
    }
}
void captureResponse(void* response) {
        // Retain an independent copy of only this API response. It is never
        // merged into account inventory and never sent to the server.
        Root baseline(api,originalResponseClone(response,responseCloneMethod));
        OriginalOptions next;
        captureOptions(api.get(response,"Activities"),"PhotoActivityId","PhotoActivity",next.activities);
        captureOptions(api.get(response,"Musics"),"MusicId","PhotoQuestMusic",next.musics);
        captureOptions(api.get(response,"Stages"),"StageId","PhotoQuestStage",next.stages);
        captureOptions(api.get(response,"Expressions"),"PhotoExpressionId","PhotoExpression",next.expressions);
        originalOptions=std::move(next);
        if(originalPhotoResponse)api.release(originalPhotoResponse);
        originalPhotoResponse=api.retain(baseline.get(),false);
        __android_log_print(ANDROID_LOG_INFO,Tag,"Original photo options: %zu activities, %zu music, %zu stages, %zu pose scenes",
            originalOptions.activities.size(),originalOptions.musics.size(),originalOptions.stages.size(),originalOptions.expressions.size());
}
void expandResponse(void* response) {
        if(limited){
            contentRows.erase(std::remove_if(contentRows.begin(),contentRows.end(),[](auto& row){if(api.target(row.object))return false;api.release(row.object);return true;}),contentRows.end());
            expandContent(api.get(response,"Musics"),"PhotoQuestMusic","PhotoMusic","MusicId");
            expandContent(api.get(response,"Stages"),"PhotoQuestStage","PhotoStage","StageId");
            expandContent(api.get(response,"Expressions"),"PhotoExpression","PhotoExpression","PhotoExpressionId");
            __android_log_print(ANDROID_LOG_INFO,Tag,"Limited photo options: %d music, %d stages, %d pose scenes",
                count(api.get(response,"Musics")),count(api.get(response,"Stages")),count(api.get(response,"Expressions")));
        }
}
void parsedResponse(void* response) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    try {
        captureResponse(response);
        Root copy(api,originalResponseClone(response,responseCloneMethod));
        expandResponse(copy.get());
        // Reflection writes use IL2CPP's GC barrier. Publish fully prepared
        // repeated-field references only; CommonResponse remains untouched.
        copyResponseLists(response,copy.get());
    }catch(const std::exception& e){fail(e.what());}
}
void* responseClone(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    Root copy(api,originalResponseClone(self,method));
    try {
        if(originalPhotoResponse){
            Root baseline(api,originalResponseClone(api.target(originalPhotoResponse),responseCloneMethod));
            copyResponseLists(copy.get(),baseline.get());
        }else captureResponse(self);
        expandResponse(copy.get());
    }catch(const std::exception& e){fail(e.what());}
    return copy.get();
}
using ParseResponse=void(*)(void*,void*,const Method*);
ParseResponse originalStreamParse=nullptr,originalBufferParse=nullptr;
thread_local int parsingDepth=0;
struct ParseScope {ParseScope(){++parsingDepth;}~ParseScope(){--parsingDepth;}};
void streamParse(void* self,void* input,const Method* method) {
    {ParseScope scope;originalStreamParse(self,input,method);}
    if(!parsingDepth)parsedResponse(self);
}
void bufferParse(void* self,void* input,const Method* method) {
    {ParseScope scope;originalBufferParse(self,input,method);}
    if(!parsingDepth)parsedResponse(self);
}
using Read=void*(*)(void*,const Method*);
std::array<Read,3> originalContentMaster{};
void allowCollaborationCharacters(void* expression) {
    Root impossible(api,api.get(expression,"ImpossibleShootingCharacterIds"));
    auto ids=strings(impossible.get());
    ids.erase(std::remove_if(ids.begin(),ids.end(),[](const auto& id){
        auto actor=catalog.find(id);return actor!=catalog.end()&&actor->second.collaboration;
    }),ids.end());replaceStrings(impossible.get(),ids);
}
using ExpressionItemConstructor=void(*)(void*,void*,bool,const Method*);
ExpressionItemConstructor originalExpressionItem=nullptr;
void expressionItem(void* self,void* expression,bool unlocked,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    Root copy(api);
    if(collaborators&&expression)try{copy=Root(api,clone(expression));allowCollaborationCharacters(copy.get());}
    catch(const std::exception& e){copy=Root(api);fail(e.what());}
    // The generated API getter may be inlined. The page model's constructor
    // is the stable boundary that retains a private expression row.
    originalExpressionItem(self,copy.get()?copy.get():expression,unlocked,method);
}
template<size_t I> void* contentMaster(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    void* original=originalContentMaster[I](self,method);
    if(!original)return original;
    const bool extended=isContentRow(self);
    if(!extended && !(I==2&&collaborators))return original;
    try {
        Root value(api,clone(original));
        if(extended){setText(value.get(),"ViewConditionId","");setText(value.get(),"UnlockConditionId","");}
        if(I==2 && collaborators)allowCollaborationCharacters(value.get());
        return value.get();
    }catch(const std::exception& e){fail(e.what());return original;}
}
bool installContent() {
    auto* response=api.klass("Solis.Common.Proto.Api","PhotoListShootingResponse");
    size_t i=0;for(const char* name:{"musics_","stages_","expressions_"}){
        void* field=response?api.field(response,name):nullptr;
        if(!field||!api.matches(api.fieldType(field),"@ref"))return false;
        responseFields[i++]=field;
    }
    responseCloneMethod=api.find(response,"Clone",Proto+std::string("PhotoListShootingResponse"));
    bool ready=hook(responseCloneMethod,responseClone,originalResponseClone);
    ready&=hook(api.find(response,"MergeFrom","System.Void",{"Google.Protobuf.CodedInputStream"}),streamParse,originalStreamParse);
    ready&=hook(api.find(response,"pb::Google.Protobuf.IBufferMessage.InternalMergeFrom","System.Void",{"Google.Protobuf.ParseContext&"}),bufferParse,originalBufferParse);
    ready&=hook(api.find(api.klass("Solis.Common.Proto.Api","PhotoMusic"),"get_Music",Master+std::string("PhotoQuestMusic")),contentMaster<0>,originalContentMaster[0]);
    ready&=hook(api.find(api.klass("Solis.Common.Proto.Api","PhotoStage"),"get_Stage",Master+std::string("PhotoQuestStage")),contentMaster<1>,originalContentMaster[1]);
    ready&=hook(api.find(api.klass("Solis.Common.Proto.Api","PhotoExpression"),"get_PhotoExpression_",Master+std::string("PhotoExpression")),contentMaster<2>,originalContentMaster[2]);
    ready&=hook(api.find(api.klass("Solis.OutGame","ExpressionPhotographyGridListItemModel"),".ctor","System.Void",
        {Master+std::string("PhotoExpression"),"System.Boolean"}),expressionItem,originalExpressionItem);
    return ready;
}

thread_local bool selectingCharacters=false;
struct SelectionScope{bool old=selectingCharacters;SelectionScope(){selectingCharacters=true;}~SelectionScope(){selectingCharacters=old;}};
using SelectInitialize=void(*)(void*,int,void*,const Method*);
SelectInitialize originalSelectInitialize=nullptr;
using SelectClick=void(*)(void*,void*,const Method*);
SelectClick originalSelectClick=nullptr;
void selectInitialize(void* self,int n,void* validate,const Method* method){
    SelectionScope scope;originalSelectInitialize(self,n,validate,method);
}
void selectClick(void* self,void* cell,const Method* method){
    SelectionScope scope;originalSelectClick(self,cell,method);
}
// Returning from shooting restores the formation through an async iterator,
// outside the picker callbacks above. Scope each synchronous MoveNext only.
// Its generic native implementation can be shared with the contest picker;
// bind the concrete ordinary-photo iterator and leave other callers untouched.
using RestorePrevious=void*(*)(void*,void*,Cancellation,const Method*);
RestorePrevious originalRestorePrevious=nullptr;
Move originalRestoreMove=nullptr;
void* photoTopClass=nullptr;
void* restoreIteratorClass=nullptr;
const Method* restoreMoveMethod=nullptr;
bool restoreMove(void* self,const Method* method) {
    if(api.objectClass(self)!=restoreIteratorClass)return originalRestoreMove(self,method);
    SelectionScope scope;return originalRestoreMove(self,method);
}
void* restorePrevious(void* self,void* param,Cancellation ct,const Method* method) {
    Root iterator(api,originalRestorePrevious(self,param,ct,method));
    if(!iterator.get() || api.objectClass(self)!=photoTopClass)return iterator.get();
    std::lock_guard<std::recursive_mutex> guard(mutex);
    try {
        auto* klass=api.objectClass(iterator.get());
        auto* move=api.find(klass,"MoveNext","System.Boolean");
        if(!move || (restoreMoveMethod && move->pointer!=restoreMoveMethod->pointer))
            throw std::runtime_error("Photography selection history iterator changed");
        if(!restoreMoveMethod) {
            restoreIteratorClass=klass;
            if(!hook(move,restoreMove,originalRestoreMove))
                throw std::runtime_error("Photography selection history adapter unavailable");
            restoreMoveMethod=move;
        }
    }catch(const std::exception& e){fail(e.what());}
    return iterator.get();
}
Read originalGroupCharacters=nullptr;
void* groupCharacters(void* self,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    void* original=originalGroupCharacters(self,method);
    if(!selectingCharacters||!collaborators)return original;
    try {
        Root list(api,original);std::vector<void*> values;std::set<std::string> ids;std::vector<Root> copies;
        auto add=[&](void* value){
            if(api.value<int>(api.get(value,"Type","@enum"))==4){
                copies.emplace_back(api,clone(value));value=copies.back().get();
                // The cell checks Character.UnlockCondition directly rather
                // than UserCharacter membership. Change its private row only.
                setText(value,"UnlockConditionId","");
            }
            values.push_back(value);ids.insert(api.text(value,"Id"));
        };
        for(int i=0,n=count(list.get(),100);i<n;++i)add(item(list.get(),i));
        // Add collaboration characters only to their own group, avoiding a
        // duplicate selectable cell in every ordinary idol group.
        Root mappings(api,api.get(self,"Mappings"));
        for(int i=0,n=count(mappings.get(),1000);i<n;++i){
            Root mapping(api,item(mappings.get(),i));
            auto id=api.text(mapping.get(),"CharacterId");if(ids.count(id)||!catalog.count(id))continue;
            Root character(api,masterRow("Character",id));
            if(api.value<int>(api.get(character.get(),"Type","@enum"))!=4)continue;
            add(character.get());
        }
        return referenceArray(values,api.klass("Solis.Common.Proto.Master","Character"));
    }catch(const std::exception& e){fail(e.what());return original;}
}
using FindCharacter=void*(*)(void*,void*,const Method*);
FindCharacter originalFindCharacter=nullptr;
void* findCharacter(void* self,void* id,const Method* method) {
    void* original=originalFindCharacter(self,id,method);
    if(original||!selectingCharacters)return original;
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!collaborators||api.objectClass(self)!=api.klass("Solis.Common.User","UserCharacterCollection"))return original;
    try {
        auto found=catalog.find(api.string(id));if(found==catalog.end()||!found->second.playable)return original;
        Root master(api,masterRow("Character",found->first));
        if(api.value<int>(api.get(master.get(),"Type","@enum"))!=4)return original;
        Root value(api,create(api.klass("Solis.Common.Proto.Transaction","UserCharacter")));
        setText(value.get(),"CharacterId",found->first);
        for(const char* property:{"FavoriteCostumeId","LiveCostumeId"})setText(value.get(),property,found->second.appearance.costume);
        for(const char* property:{"FavoriteHairId","LiveHairId"})setText(value.get(),property,found->second.appearance.hair);
        return value.get();
    }catch(const std::exception& e){fail(e.what());return original;}
}
using ValidateCharacter=bool(*)(void*,const Method*);
ValidateCharacter originalValidateCharacter=nullptr;
bool validateCharacter(void* character,const Method* method) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(collaborators&&character)try {
        if(api.value<int>(api.get(character,"Type","@enum"))==4){
            return !collaborationMotionSource(api.text(character,"Id")).empty();
        }
    }catch(const std::exception& e){fail(e.what());}
    return originalValidateCharacter(character,method);
}
bool installCharacters() {
    bool ready=installCollaborationPoses();
    photoTopClass=api.klass("Solis.OutGame","PhotographyTopPagePresenter");
    ready&=hook(api.find(photoTopClass,"SetPreviousDataAsync","@ref",
        {"Solis.OutGame.PhotographyTopTransitionParam","System.Threading.CancellationToken"}),restorePrevious,originalRestorePrevious);
    ready&=hook(api.find(api.klass("Solis.OutGame","PhotographyCharacterSelectListPresenter"),"Initialize","System.Void",
        {"System.Int32","@predicate"}),selectInitialize,originalSelectInitialize);
    ready&=hook(api.find(api.klass("Solis.OutGame","PhotographyCharacterSelectListPresenter"),"OnClickCell","System.Void",
        {"Solis.OutGame.PhotographyCharacterSelectListCellPresenter"}),selectClick,originalSelectClick);
    ready&=hook(api.find(api.klass("Solis.Common.Proto.Master","CharacterGroup"),"GetOrderedValidMainCharacters","@ref"),groupCharacters,originalGroupCharacters);
    ready&=hook(api.find(api.klass("Solis.Common.User","UserCharacterCollection"),"FindById","Solis.Common.Proto.Transaction.UserCharacter",{"System.String"}),findCharacter,originalFindCharacter);
    ready&=hook(api.find(api.klass("Solis.OutGame","PhotographyOutGameUtility"),"ValidateExpressionCharacter","System.Boolean",{"Solis.Common.Proto.Master.Character"},true),validateCharacter,originalValidateCharacter);
    return ready&&contentReady;
}
