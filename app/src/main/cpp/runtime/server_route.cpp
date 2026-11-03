#include "server_route.h"
#include "server_route_policy.h"
#include "managed_api.h"
#include "inline_hook.h"
#include <shadowhook.h>
#include <android/log.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace localify::server_route {
namespace {
using managed::Method;using managed::Root;
managed::Api api;Config config;
std::mutex mutex;
std::atomic<bool> installed=false,ready=false;
std::atomic<unsigned> channels=0,logins=0,requests=0;
std::string error;
std::unique_ptr<Root> endpoint,localToken,rootCertificate,sessionToken,endpointUri;
std::string endpointScheme,endpointHost;int endpointPort=0;
void *invalidOperation=nullptr,*uriClass=nullptr;
const Method *uriCtor=nullptr,*headerRemove=nullptr,*headerAdd=nullptr;
// This value-return ABI is used only after verifying all runtime field offsets.
struct TokenResult {void* token;void* error;};
struct StringTask {void* source;TokenResult result;int16_t token;};
struct Cancellation {void* source;};
static_assert(sizeof(StringTask)==32 && sizeof(Cancellation)==8);
StringTask (*originalIdToken)(bool,Cancellation,const Method*)=nullptr;
StringTask (*originalIdTokenSimple)(Cancellation,const Method*)=nullptr;
void* (*originalHost)(void*,const Method*)=nullptr;
void* (*originalRoots)(void*,const Method*)=nullptr;
void* (*originalChannel)(void*,void*,const Method*)=nullptr;
void* (*originalSend)(void*,void*,Cancellation,const Method*)=nullptr;
void (*originalLoginToken)(void*,void*,const Method*)=nullptr;
void (*originalCheckToken)(void*,void*,const Method*)=nullptr;
void (*originalSessionMerge)(void*,void*,const Method*)=nullptr;
void (*originalSessionBuffer)(void*,void*,const Method*)=nullptr;
void* sessionField=nullptr;
void (*originalApiError)(void*,void*,void*,void*,void*,const Method*)=nullptr;

template<class F> void hook(const Method* method,F replacement,F& original) {
    if(!method || !installInlineHook(method->pointer,reinterpret_cast<void*>(replacement),original,shadowhook_hook_func_addr))
        throw std::runtime_error("Required network hook unavailable: "+managed::missingSignature);
}
void reject(const char* message) {
    // Never include tokens, response bodies or private URLs in error messages.
    api.raise(api.exception(api.classImage(invalidOperation),"System","InvalidOperationException",message));
}
void ensureReady(){if(!ready)reject("Custom server adapter unavailable; connection blocked until configuration is repaired");}
void* host(void* self,const Method* method){
    if(!config.enabled)return originalHost(self,method);
    ensureReady();return endpoint->get();
}
void* roots(void* self,const Method* method){
    if(!config.enabled)return originalRoots(self,method);
    ensureReady();return rootCertificate->get();
}
void* channel(void* address,void* options,const Method* method){
    if(!config.enabled)return originalChannel(address,options,method);
    ensureReady();
    ++channels;return originalChannel(endpointUri->get(),options,method);
}
StringTask idToken(bool refresh,Cancellation cancellation,const Method* method){
    if(!config.enabled)return originalIdToken(refresh,cancellation,method);
    ensureReady();return {nullptr,{localToken->get(),nullptr},0};
}
StringTask idTokenSimple(Cancellation cancellation,const Method* method){
    if(!config.enabled)return originalIdTokenSimple(cancellation,method);
    ensureReady();return {nullptr,{localToken->get(),nullptr},0};
}
void loginToken(void* self,void* token,const Method* method){
    if(config.enabled){ensureReady();token=localToken->get();++logins;}
    originalLoginToken(self,token,method);
}
void checkToken(void* self,void* token,const Method* method){
    if(config.enabled){ensureReady();token=localToken->get();}
    originalCheckToken(self,token,method);
}
void captureSession(void* self){
    if(!config.enabled)return;
    ensureReady();void* value=nullptr;api.fieldGet(self,sessionField,&value);
    std::lock_guard<std::mutex> lock(mutex);sessionToken=std::make_unique<Root>(api,value);
}
void sessionMerge(void* self,void* input,const Method* method){
    originalSessionMerge(self,input,method);captureSession(self);
}
void sessionBuffer(void* self,void* input,const Method* method){
    originalSessionBuffer(self,input,method);captureSession(self);
}
// Shared generic ApiBase header methods may have separate IL2CPP instantiations.
// The non-generic HTTP/2 handler is the final, single outgoing boundary instead.
void* send(void* self,void* request,Cancellation cancellation,const Method* method){
    if(!config.enabled)return originalSend(self,request,cancellation,method);
    ensureReady();
    try {
        Root uri(api,api.get(request,"RequestUri","System.Uri"));
        if(api.text(uri.get(),"Scheme")!=endpointScheme || api.text(uri.get(),"Host")!=endpointHost ||
           api.value<int>(api.get(uri.get(),"Port","System.Int32"))!=endpointPort)
            return originalSend(self,request,cancellation,method);
        const auto path=api.text(uri.get(),"AbsolutePath");
        auto slash=path.find_last_of('/');
        if(slash==std::string::npos || blockedAccountMethod(path.substr(0,slash),path.substr(slash+1)))
            throw std::runtime_error("Official account transfer is unavailable on a custom server");
        Root headers(api,api.get(request,"Headers","System.Net.Http.Headers.HttpRequestHeaders"));
        auto* toString=api.find(api.objectClass(headers.get()),"ToString","System.String");
        const auto names=headerNames(api.string(api.call(toString,headers.get()),65536));
        for(const auto& name:names)if(!keepHeader(name)){
            Root key(api,api.newString(name.c_str()));void* args[]{key.get()};api.call(headerRemove,headers.get(),args);
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(sessionToken && sessionToken->get()){
                Root key(api,api.newString("x-auth-token"));void* args[]{key.get(),sessionToken->get()};
                if(!api.value<bool>(api.call(headerAdd,headers.get(),args)))throw std::runtime_error("Custom session header rejected");
            }
        }
        ++requests;
    }catch(const std::exception& ex){
        {std::lock_guard<std::mutex> lock(mutex);error=ex.what();}
        reject("Custom server request validation failed");return nullptr;
    }
    return originalSend(self,request,cancellation,method);
}
void apiError(void* self,void* name,void* request,void* apiError,void* headers,const Method* method){
    if(!config.enabled){originalApiError(self,name,request,apiError,headers,method);return;}
    std::lock_guard<std::mutex> lock(mutex);error="自定义服务器请求失败，请检查服务端兼容性与连接";
}
void* nested(void* parent,const char* name){
    if(!parent)return nullptr;
    void* iterator=nullptr;while(auto* type=api.nestedTypes(parent,&iterator))if(std::strcmp(api.className(type),name)==0)return type;
    return nullptr;
}
void validateTask(const Method* method){
    if(!method)throw std::runtime_error("Login task signature unavailable");
    auto* type=api.classFromType(api.returnType(method));uint32_t alignment=0;
    if(!type || api.type(api.returnType(method))!="Cysharp.Threading.Tasks.UniTask<System.ValueTuple<System.String,Qua.Seed.Error>>" || !api.isValueType(type) || api.valueSize(type,&alignment)!=sizeof(StringTask))
        throw std::runtime_error("Unsupported login task ABI: "+api.type(api.returnType(method)));
    for(const auto& field:std::initializer_list<std::pair<const char*,size_t>>{{"source",0},{"result",8},{"token",24}}){
        void* f=api.field(type,field.first);
        if(!f || api.fieldOffset(f)!=2*sizeof(void*)+field.second)throw std::runtime_error("Unsupported login task field layout");
    }
    auto* result=api.classFromType(api.fieldType(api.field(type,"result")));
    if(!result || !api.isValueType(result) || api.valueSize(result,&alignment)!=sizeof(TokenResult))throw std::runtime_error("Unsupported token result ABI");
    auto* first=api.field(result,"Item1");auto* second=api.field(result,"Item2");
    if(!first || !second || api.fieldOffset(first)!=16 || api.fieldOffset(second)!=24 ||
       api.type(api.fieldType(first))!="System.String" || api.type(api.fieldType(second))!="Qua.Seed.Error" ||
       api.isValueType(api.classFromType(api.fieldType(second))))throw std::runtime_error("Unsupported token result layout");
    auto* cancellation=api.classFromType(api.paramType(method,api.paramCount(method)-1));
    auto* source=cancellation?api.field(cancellation,"_source"):nullptr;
    if(!cancellation || !source || api.valueSize(cancellation,&alignment)!=sizeof(Cancellation) || api.fieldOffset(source)!=2*sizeof(void*))
        throw std::runtime_error("Unsupported cancellation ABI");
}
}
bool validate(const std::string& json){try{parse(json);return true;}catch(...){return false;}}
bool configure(const std::string& json){std::lock_guard<std::mutex> lock(mutex);if(installed)return false;try{config=parse(json);return true;}catch(...){config={true,"","",""};return false;}}
void install(void* (*resolve)(const char*)) {
    if(installed.exchange(true))return;
    try {
        api.load(resolve);
        invalidOperation=api.klass("System","InvalidOperationException","mscorlib.dll");
        if(!invalidOperation)throw std::runtime_error("Missing network error type");
        auto* network=api.klass("Solis.Common.Network","Api");
        // The transport gate is installed first. Partial setup cannot fall back to the official endpoint.
        auto* grpc=api.klass("Grpc.Net.Client","GrpcChannel","Grpc.Net.Client.dll");
        hook(api.find(grpc,"ForAddress","Grpc.Net.Client.GrpcChannel",{"System.Uri","Grpc.Net.Client.GrpcChannelOptions"},true),channel,originalChannel);
        hook(api.find(network,"GetHost","System.String"),host,originalHost);
        if(config.enabled && config.endpoint.empty())throw std::runtime_error("Saved server configuration is invalid; connection blocked");
        uriClass=api.klass("System","Uri","System.dll");uriCtor=api.find(uriClass,".ctor","System.Void",{"System.String"});
        auto* headers=api.klass("System.Net.Http.Headers","HttpHeaders","System.Net.Http.dll");
        headerRemove=api.find(headers,"Remove","System.Boolean",{"System.String"});
        headerAdd=api.find(headers,"TryAddWithoutValidation","System.Boolean",{"System.String","System.String"});
        if(!uriCtor || !headerRemove || !headerAdd)throw std::runtime_error("Network header interfaces unavailable: "+managed::missingSignature);
        auto* seed=nested(api.klass("Solis.Common.Seed","SeedWrapper"),"Auth");
        const std::string result="Cysharp.Threading.Tasks.UniTask<System.ValueTuple<System.String,Qua.Seed.Error>>";
        const auto* tokenMethod=api.find(seed,"GetIdTokenAsync",result,{"System.Boolean","System.Threading.CancellationToken"},true);
        const auto* simple=api.find(seed,"GetIdTokenAsync",result,{"System.Threading.CancellationToken"},true);
        if(!tokenMethod || !simple)throw std::runtime_error("Login token adapter unavailable: "+managed::missingSignature);
        validateTask(tokenMethod);validateTask(simple);
        endpoint=std::make_unique<Root>(api,api.newString(config.endpoint.c_str()));
        localToken=std::make_unique<Root>(api,api.newString(config.token.c_str()));
        rootCertificate=std::make_unique<Root>(api,config.certificate.empty()?nullptr:api.newString(config.certificate.c_str()));
        hook(api.find(network,"GetSSLRootCertificates","System.String"),roots,originalRoots);
        if(config.enabled){
            endpointUri=std::make_unique<Root>(api,api.newObject(uriClass));void* args[]{endpoint->get()};api.call(uriCtor,endpointUri->get(),args);
            endpointScheme=api.text(endpointUri->get(),"Scheme");endpointHost=api.text(endpointUri->get(),"Host");
            endpointPort=api.value<int>(api.get(endpointUri->get(),"Port","System.Int32"));
        }
        hook(api.find(api.klass("Cysharp.Net.Http","YetAnotherHttpHandler","Cysharp.Net.Http.YetAnotherHttpHandler.dll"),"SendAsync","@ref",
            {"System.Net.Http.HttpRequestMessage","System.Threading.CancellationToken"}),send,originalSend);
        hook(tokenMethod,idToken,originalIdToken);hook(simple,idTokenSimple,originalIdTokenSimple);
        hook(api.find(api.klass("Solis.Common.Proto.Api","AuthLoginRequest"),"set_FirebaseIDToken","System.Void",{"System.String"}),loginToken,originalLoginToken);
        hook(api.find(api.klass("Solis.Common.Proto.Api","SystemCheckRequest"),"set_FirebaseIDToken","System.Void",{"System.String"}),checkToken,originalCheckToken);
        auto* response=api.klass("Solis.Common.Proto.Api","AuthLoginResponse");sessionField=api.field(response,"gameAuthToken_");
        if(!sessionField || api.type(api.fieldType(sessionField))!="System.String")throw std::runtime_error("Custom session field unavailable");
        // Observe the wire parser, not the tiny property getter that IL2CPP may inline.
        hook(api.find(response,"MergeFrom","System.Void",{"Google.Protobuf.CodedInputStream"}),sessionMerge,originalSessionMerge);
        hook(api.find(response,"pb::Google.Protobuf.IBufferMessage.InternalMergeFrom","System.Void",{"Google.Protobuf.ParseContext&"}),sessionBuffer,originalSessionBuffer);
        hook(api.find(network,"ApiErrorLog","System.Void",{"System.String","System.Object","Qua.Network.IApiError","Grpc.Core.Metadata"}),apiError,originalApiError);
        ready=true;__android_log_print(ANDROID_LOG_INFO,"IdolyServer","Server adapter ready; custom=%d",config.enabled);
    }catch(const std::exception& ex){std::lock_guard<std::mutex> lock(mutex);error=ex.what();__android_log_print(ANDROID_LOG_WARN,"IdolyServer","%s",ex.what());}
}
std::string status(){std::lock_guard<std::mutex> lock(mutex);return nlohmann::json{{"available",ready.load()},{"enabled",config.enabled},{"endpoint",config.enabled?config.endpoint:""},{"channels",channels.load()},{"login_requests",logins.load()},{"requests",requests.load()},{"error",error}}.dump(-1,' ',true);}
}
