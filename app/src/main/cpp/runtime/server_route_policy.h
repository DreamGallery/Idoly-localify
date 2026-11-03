#pragma once
#include "json.hpp"
#include <regex>
#include <stdexcept>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
#include <cctype>

namespace localify::server_route {
struct Config { bool enabled=false; std::string endpoint,token,certificate; };
inline Config parse(const std::string& text) {
    if(text.size()>40000)throw std::invalid_argument("Server configuration too large");
    const auto j=nlohmann::json::parse(text);
    if(!j.is_object() || j.size()!=5 || j.at("schema_version")!=1 || !j.at("enabled").is_boolean() ||
       !j.at("endpoint").is_string() || !j.at("login_token").is_string() || !j.at("ca_pem").is_string())
        throw std::invalid_argument("Invalid server configuration");
    Config c{j.at("enabled").get<bool>(),j.at("endpoint").get<std::string>(),j.at("login_token").get<std::string>(),j.at("ca_pem").get<std::string>()};
    if(!c.enabled && c.endpoint.empty() && c.token.empty() && c.certificate.empty())return c;
    std::smatch match;
    static const std::regex address(R"(^(https|http)://([a-z0-9](?:[a-z0-9.-]*[a-z0-9])?|\[[0-9a-f:]+\])(?::([0-9]{1,5}))?$)");
    if(c.endpoint.size()>512 || !std::regex_match(c.endpoint,match,address))throw std::invalid_argument("Invalid server address");
    if(match[3].matched && (std::stoi(match[3])<1 || std::stoi(match[3])>65535))throw std::invalid_argument("Invalid server port");
    const auto host=match[2].str();
    if(match[1]=="http" && host!="127.0.0.1" && host!="[::1]" && host!="localhost")
        throw std::invalid_argument("HTTP is limited to loopback; use HTTPS");
    if(c.token.size()<16 || c.token.size()>1024 || c.token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~")!=std::string::npos)
        throw std::invalid_argument("Invalid custom-server login token");
    if(c.certificate.size()>32768 || c.certificate.find('\0')!=std::string::npos ||
       (!c.certificate.empty() && (c.certificate.find("-----BEGIN CERTIFICATE-----")!=0 || c.certificate.find("PRIVATE KEY")!=std::string::npos)))
        throw std::invalid_argument("Invalid CA certificate");
    return c;
}
inline bool blockedAccountMethod(std::string service,const std::string& method) {
    const auto pos=service.find_last_of("./");if(pos!=std::string::npos)service=service.substr(pos+1);
    return service=="Migration" || (service=="Auth" && method!="Login");
}
inline bool keepHeader(const std::string& name){
    static const std::set<std::string> allowed{"user-agent","te","grpc-accept-encoding","grpc-encoding","grpc-timeout",
        "x-app-version","x-platform","x-master-version","x-request-id","x-request-timestamp","x-request-signature"};
    return allowed.count(name)!=0;
}
inline std::vector<std::string> headerNames(const std::string& text){
    std::vector<std::string> names;
    for(size_t start=0;start<text.size();){
        auto end=text.find('\n',start);if(end==std::string::npos)end=text.size();
        auto line=text.substr(start,end-start);start=end+1;
        if(line.empty() || line=="\r")continue;
        const auto colon=line.find(':');
        if(colon==std::string::npos || colon==0 || colon>128)throw std::runtime_error("Malformed request header");
        auto name=line.substr(0,colon);
        if(name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_!")!=std::string::npos)
            throw std::runtime_error("Invalid request header name");
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        names.push_back(name);if(names.size()>100)throw std::runtime_error("Too many request headers");
    }
    return names;
}
}
