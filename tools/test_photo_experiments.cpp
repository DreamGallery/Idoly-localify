#include "runtime/photo_experiments.cpp"
#include <cassert>

int __android_log_print(int,const char*,const char*,...){return 0;}
void* shadowhook_hook_func_addr(void*,void*,void**){assert(false);return nullptr;}
using namespace localify::photo_experiments;

int main(){
    // UI thumbnail requests cannot invoke Unity or request arbitrary assets.
    // Queue limits, duplicate suppression and close/reset remain native-only.
    thumbnailReady=true;thumbnailAssets={"known"};
    assert(thumbnail("file:///private")=="!");
    assert(thumbnail("known").empty() && thumbnailQueue.size()==1);
    assert(thumbnail("known").empty() && thumbnailQueue.size()==1);
    for(int i=0;i<30;++i){auto key="asset"+std::to_string(i);thumbnailAssets.insert(key);thumbnail(key);}
    assert(thumbnailQueue.size()==24);
    cancelThumbnails();assert(thumbnailQueue.empty() && thumbnailReset);
    processThumbnails();assert(!thumbnailsPending && !thumbnailReset);
    for(int i=0;i<100;++i)rememberThumbnail(std::to_string(i),std::string(65536,'a'));
    assert(thumbnailBytes<=4*1024*1024 && thumbnailCache.size()<=96);
    resetThumbnails();processThumbnails();assert(thumbnailCache.empty() && thumbnailBytes==0);
    thumbnailAssets.clear();thumbnailReady=false;
    // IL2CPP reference-field setters consume an object, not &object. A stack
    // address survives the immediate write but crashes subsequent enumeration.
    struct ResponseLists { void* lists[3]; void* common; };
    int values[4]{};
    ResponseLists source{{&values[0],&values[1],&values[2]},&values[3]},destination{};
    destination.common=&destination;
    static size_t fields[]{0,1,2};
    for(size_t i=0;i<3;++i)responseFields[i]=&fields[i];
    api.fieldGet=[](void* object,void* field,void* output){
        *static_cast<void**>(output)=static_cast<ResponseLists*>(object)->lists[*static_cast<size_t*>(field)];
    };
    api.fieldSetObject=[](void* object,void* field,void* value){
        static_cast<ResponseLists*>(object)->lists[*static_cast<size_t*>(field)]=value;
    };
    copyResponseLists(&destination,&source);
    for(size_t i=0;i<3;++i)assert(destination.lists[i]==source.lists[i]);
    assert(destination.common==&destination);

    // A mascot lacks a facial controller; skipping its eye correction must not
    // disable the same update on ordinary idols or unrelated game actors.
    static int mascot=0,idol=0,eyeUpdates=0,lookUpdates=0;
    api.target=[](uint32_t handle)->void*{return handle==55?&mascot:nullptr;};
    wholeActors={55};
    originalEyeCorrection=[](void* actor,const Method*){assert(actor==&idol);++eyeUpdates;};
    originalLookAtEffector=[](void* actor,void*,const Method*){assert(actor==&idol);++lookUpdates;};
    eyeCorrection(&mascot,nullptr);eyeCorrection(&idol,nullptr);
    lookAtEffector(&mascot,nullptr,nullptr);lookAtEffector(&idol,nullptr,nullptr);
    assert(eyeUpdates==1 && lookUpdates==1);wholeActors.clear();

    // Outfit ownership stays original outside the pose selection validator;
    // nested/non-pose validation and exceptional exits restore their scope.
    static int ownershipReads=0;
    originalHasDefaultCostume=[](void*,const Method*){++ownershipReads;return false;};
    collaborators=true;validatingPoseOutfits=false;
    assert(!hasDefaultCostume(&idol,nullptr) && ownershipReads==1);
    collaborators=false;validatingPoseOutfits=true;
    assert(!hasDefaultCostume(&idol,nullptr) && ownershipReads==2);
    originalSetNgCharacters=[](void*,void*,const Method*){assert(!validatingPoseOutfits);throw 73;};
    try{setNgCharacters(nullptr,nullptr,nullptr);assert(false);}catch(int e){assert(e==73);}
    assert(validatingPoseOutfits);validatingPoseOutfits=false;

    // Only the ordinary photography restoration iterator enters the picker
    // scope. A shared generic implementation must not extend contest access.
    static int photoIterator=0,contestIterator=0,restores=0;
    api.objectClass=[](void* object){return object;};
    restoreIteratorClass=&photoIterator;
    originalRestoreMove=[](void* self,const Method*){
        assert(selectingCharacters==(self==&photoIterator));++restores;return true;
    };
    assert(restoreMove(&photoIterator,nullptr));assert(!selectingCharacters);
    assert(restoreMove(&contestIterator,nullptr));assert(!selectingCharacters && restores==2);
    originalRestoreMove=[](void*,const Method*)->bool{assert(selectingCharacters);throw 74;};
    try{restoreMove(&photoIterator,nullptr);assert(false);}catch(int e){assert(e==74);}
    assert(!selectingCharacters);

    // Reserving original slots prevents an unavailable collaboration character
    // from stealing a later real character's identity in a multi-actor photo.
    std::mt19937 random(149);
    assert((baselineCharacters({"collab","a","b"},{"a","b","c"},random)==std::vector<std::string>{"c","a","b"}));
    assert((baselineCharacters({"a","a"},{"a","b"},random)==std::vector<std::string>{"a","b"}));
    std::set<std::string> choices;
    for(int i=0;i<64;++i) {
        auto chosen=baselineCharacters({"collab1","a","collab2"},{"a","b","c"},random);
        assert(chosen[1]=="a" && chosen[0]!=chosen[2]);
        assert((chosen[0]=="b"||chosen[0]=="c") && (chosen[2]=="b"||chosen[2]=="c"));
        choices.insert(chosen.front());
    }
    assert(choices.size()==2);
    bool rejected=false;
    try{baselineCharacters({"collab","a","b"},{"a","b"},random);}catch(const std::exception&){rejected=true;}
    assert(rejected);
    catalog={{"a",{"a","A",{"costume-a","hair-a"},true,true}},
             {"b",{"b","B",{"costume-b","hair-b"},true,true}},
             {"npc",{"npc","NPC",{"costume-npc","hair-npc"},false,false}}};
    validateRules({{"a","npc",Live},{"a","b",Scene},{"b","npc",Both}},catalog);
    catalog.at("b").costumes.emplace("b-party",CostumeChoice{"Party","b-short"});
    catalog.at("b").hairs.emplace("b-short",HairChoice{"Short",{"b-party"},{}});
    Rule complete{"a","b",Both,{"b-party","b-short"}};
    validateRules({complete},catalog);
    auto appearance=selectedAppearance(complete,catalog.at("b"));
    assert(appearance.costume=="b-party" && appearance.hair=="b-short");
    HairChoice ornament{"Hairpin",{"b-party"},{}};ornament.ornamentBase="b-short";
    catalog.at("b").hairs.emplace("b-pin",ornament);
    appearance=selectedAppearance({"a","b",Both,{"b-party","b-short","b-pin"}},catalog.at("b"));
    assert(appearance.hair=="b-pin" && appearance.costume=="b-party");
    rejected=false;try{selectedAppearance({"a","b",Both,{"b-party","b-pin","b-pin"}},catalog.at("b"));}
    catch(const std::exception&){rejected=true;}assert(rejected);
    catalog.at("b").hairs.at("b-pin").excluded={"b-party"};
    rejected=false;try{selectedAppearance({"a","b",Both,{"b-party","b-short","b-pin"}},catalog.at("b"));}
    catch(const std::exception&){rejected=true;}assert(rejected);
    catalog.at("b").hairs.at("b-pin").excluded.clear();
    // An unchanged character may still change outfit; a foreign character's
    // hair must not be smuggled into the visual descriptor.
    validateRules({{"b","b",Both,{"b-party","b-short"}}},catalog);
    rejected=false;try{validateRules({{"a","b",Both,{"b-party","hair-a"}}},catalog);}catch(const std::exception&){rejected=true;}assert(rejected);
    catalog.at("npc").modelAssets={"mdl_oth_mascot"};
    assert(selectedAppearance({"a","npc",Both},catalog.at("npc")).costume.empty());
    rejected=false;try{validateRules({{"a","npc",Both,{"b-party","b-short"}}},catalog);}catch(const std::exception&){rejected=true;}assert(rejected);
    rejected=false;try{validateRules({{"a","npc",Both},{"a","b",Live}},catalog);}catch(const std::exception&){rejected=true;}assert(rejected);
    addStoryModels(catalog);
    const auto& lime=catalog.at("asset:lim");
    assert(!lime.playable && !lime.owned && lime.assetAppearance && lime.costumes.size()==2);
    validateRules({{"a","asset:lim",Both,{"mdl_chr_lim-casl-00_body","mdl_chr_lim-base-00_hair"}}},catalog);
    rejected=false;try{validateRules({{"asset:lim","a",Both}},catalog);}catch(const std::exception&){rejected=true;}assert(rejected);
    rejected=false;try{validateRules({{"a","asset:lim",Both,{"mdl_chr_lim-idol-00_body","mdl_chr_lmn-base-00_hair"}}},catalog);}catch(const std::exception&){rejected=true;}assert(rejected);
    requestReady=modelReady=contentReady=characterReady=true;
    initial.emplace("a",InitialCharacter{{"costume-a","hair-a"},"pose","facial",0});
    catalog.emplace("collab",Character{"collab","Collaboration",{"collab-clothes","collab-hair"},true,false});
    catalog.at("collab").collaboration=true;
    collaborators=true;
    assert(poseMotionSource("collab")=="a");
    assert(poseMotionSource("a").empty());
    assert(poseMotionSource("asset:lim").empty());
    collaborators=false;assert(poseMotionSource("collab").empty());
    assert(configure(R"({"normalize":true,"limited":true,"collaborators":false,"rules":[{"source":"a","target":"npc","context":3}]})"));
    const auto oldRules=rules.size();
    assert(!configure(R"({"normalize":false,"limited":true,"collaborators":false,"rules":[]})"));
    assert(normalize&&limited&&rules.size()==oldRules);
    assert(requestKind("PhotoContestCheckShootingRequest").empty());
    assert(requestKind("UserSetCostumeRequest").empty());
    assert(requestKind("PhotoCheckExpressionShootingRequest")=="expression");
    assert(selectOption({{"two",{2}},{"one",{1}}},1,"expired")=="one");
    rejected=false;try{selectOption({{"two",{2}}},1,"expired");}catch(const std::exception&){rejected=true;}assert(rejected);
    OriginalOptions live;
    live.musics={{"video"},{"limited",{},{},true,{"expired-stage"}},
                 {"ordinary",{},{},true,{}, {1}}};
    live.stages={{"house",{},{},false,{},{},1},{"hall",{},{},false,{},{},2}};
    assert((selectLiveOptions(live,"limited","expired-stage")==std::pair<std::string,std::string>{"ordinary","hall"}));
    assert((selectLiveOptions(live,"ordinary","hall")==std::pair<std::string,std::string>{"ordinary","hall"}));
    live.stages.pop_back();
    rejected=false;try{selectLiveOptions(live,"limited","expired-stage");}catch(const std::exception&){rejected=true;}assert(rejected);
    // A collaboration stage can stay unlocked after its song expires but is
    // hidden from normal shooting; only its explicitly linked song may use it.
    live.stages.push_back({"expired-stage",{},{},false,{},{},3,true});
    assert((selectLiveOptions(live,"limited","expired-stage")==std::pair<std::string,std::string>{"limited","expired-stage"}));
    live.musics.erase(live.musics.begin()+1);
    rejected=false;try{selectLiveOptions(live,"ordinary","expired-stage");}catch(const std::exception&){rejected=true;}assert(rejected);
    // Scene position IDs are not zero-based indices. An incompatible original
    // scene falls back only to an unlocked scene supporting the initial pose.
    const std::vector<PhotoOption> scenes{{"standing",{1},{{2,"sit"},{7,"stand"}}},
        {"sitting",{1},{{1,"sit"}}},{"duet",{2},{{3,"stand"}}}};
    assert((selectExpression(scenes,{"stand"},"standing")==std::pair<std::string,int>{"standing",7}));
    assert((selectExpression(scenes,{"stand"},"sitting")==std::pair<std::string,int>{"standing",7}));
    assert((selectExpression(scenes,{"stand","stand"},"expired")==std::pair<std::string,int>{"duet",3}));
    rejected=false;try{selectExpression(scenes,{"stand","sit"},"duet");}catch(const std::exception&){rejected=true;}assert(rejected);
    RequestPlan checked;checked.before={"collab","a","b","d","e"};checked.after={"c","a","b","d","e"};checked.scene="duet";checked.sequence=1;
    requestPlans.emplace("full-formation",checked);
    RequestPlan saved;
    assert(frozenPlan("duet",{"a","collab"},saved));
    assert((saved.before==std::vector<std::string>{"a","collab"}));
    assert((saved.after==std::vector<std::string>{"a","c"}));
    assert(!frozenPlan("other-song",{"a","collab"},saved));
    assert(!frozenPlan("duet",{"unselected"},saved));
    checked.sequence=2;checked.after.front()="f";requestPlans.emplace("new-session",checked);
    assert(frozenPlan("duet",{"collab"},saved) && saved.after.front()=="f");
    auto other=checked;other.scene="other-scene";requestPlans.emplace("other",other);
    discardPlans("duet");
    assert(!frozenPlan("duet",{"collab"},saved));
    assert(frozenPlan("other-scene",{"collab"},saved));

    static int request=0,context=0,transport=0,writes=0;static const char* requestName="PhotoCheckExpressionShootingRequest";
    api.objectClass=[](void* object){return object;};
    api.classNamespace=[](void*){return "Solis.Common.Proto.Api";};
    api.className=[](void*){return requestName;};
    originalSerialize=[](void* self,void* value,void* ctx,const Method*){assert(self==&transport&&value==&request&&ctx==&context);++writes;};
    normalize=false;requestPlans.clear();
    serialize(&transport,&request,&context,nullptr);assert(writes==1);
    normalize=true;requestName="PhotoContestCheckShootingRequest";
    serialize(&transport,&request,&context,nullptr);assert(writes==2);
    // Missing request schema while enabled must fail before transport. It must
    // never fall back to submitting the unnormalized experimental selection.
    requestName="PhotoCheckExpressionShootingRequest";
    api.methods=[](void*,void**)->const Method*{return nullptr;};
    api.parent=[](void*)->void*{return nullptr;};
    api.classImage=[](void* p){return p;};invalidOperationClass=&request;
    api.exception=[](void* img,const char* ns,const char* name,const char* message)->void*{
        assert(img==&request&&std::string(ns)=="System"&&std::string(name)=="InvalidOperationException"&&message[0]);return &request;
    };
    api.raise=[](void*){throw 42;};
    rejected=false;try{serialize(&transport,&request,&context,nullptr);}catch(int e){assert(e==42);rejected=true;}
    assert(rejected&&writes==2);
    assert(!error.empty());
    return 0;
}
