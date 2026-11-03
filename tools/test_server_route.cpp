#include <cassert>
#include "runtime/server_route.cpp"
int __android_log_print(int,const char*,const char*,...){return 0;}
void* shadowhook_hook_func_addr(void*,void*,void**){assert(false);return nullptr;}
int main(){
    using namespace localify::server_route;
    using Json=nlohmann::json;
    Json value{{"schema_version",1},{"enabled",true},{"endpoint","https://server.example:443"},{"login_token","separate-local-credential-1234"},{"ca_pem",""}};
    assert(validate(value.dump()));
    for(const char* url:{"http://server.example","https://user:pass@server.example","https://server.example/?token=x","https://server.example:0","https://server.example:65536","https://server.example\n","https://server.example/path","file:///data/user/0"}){
        auto bad=value;bad["endpoint"]=url;assert(!validate(bad.dump()));
    }
    for(const char* url:{"http://127.0.0.1:50051","http://[::1]:50051","https://server.example"}){
        auto valid=value;valid["endpoint"]=url;assert(validate(valid.dump()));
    }
    for(const char* key:{"enabled","endpoint","login_token","ca_pem","schema_version"}){auto bad=value;bad.erase(key);assert(!validate(bad.dump()));}
    auto bad=value;bad["login_token"]="a\nsecret-token-value";assert(!validate(bad.dump()));
    bad=value;bad["ca_pem"]="-----BEGIN PRIVATE KEY-----";assert(!validate(bad.dump()));
    assert(blockedAccountMethod("Solis.Common.Proto.Api.Auth","Recreate"));
    assert(blockedAccountMethod("Migration","Execute"));
    assert(!blockedAccountMethod("Auth","Login"));assert(!blockedAccountMethod("User","Get"));
    assert(blockedAccountMethod("/api.Auth","Recreate"));
    const auto names=headerNames("X-Auth-Token: hidden\r\nx-app-version: 6.1.0\r\nX-Device: hidden\r\n\r\n");
    assert(names.size()==3 && !keepHeader(names[0]) && keepHeader(names[1]) && !keepHeader(names[2]));
    assert(!keepHeader("authorization") && !keepHeader("x-advertising-id"));
    bool rejected=false;try{headerNames(" folded value\r\n");}catch(...){rejected=true;}assert(rejected);
    assert(configure(value.dump()));
    assert(status().find("separate-local-credential")==std::string::npos);
    install([](const char*)->void*{return nullptr;});
    auto state=Json::parse(status());assert(!state["available"].get<bool>() && state["enabled"].get<bool>());
    assert(!configure(value.dump()));
}
