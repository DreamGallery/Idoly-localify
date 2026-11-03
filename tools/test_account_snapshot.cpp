#include <cassert>
#include "runtime/account_snapshot.cpp"
namespace localify::frames { bool available(){return false;} bool subscribe(const char*,Callback){return false;} }
int main(){
    using namespace localify::account_snapshot;
    install([](const char*)->void*{return nullptr;});
    assert(!nlohmann::json::parse(status())["available"].get<bool>());
    assert(!request(false));assert(document().empty());assert(protobuf().empty());
}
