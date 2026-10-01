#pragma once
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

class ShopExchangeDescriptionMatcher {
public:
    static bool isTargetPath(std::string_view path) {
        return path == "WindowSystem/WindowRoot/ShopWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/SelectItem/MaskBase/Description" ||
            path == "WindowSystem/WindowRoot/ExchangeWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/SelectItem/MaskBase/Description";
    }
    static bool mayMatch(std::string_view text) {
        return text.size() <= 4096 && text.find("\n在庫リセット\n") != std::string_view::npos;
    }
    static bool preserveTrackedSource(bool sameObject, bool explicitKey, std::string_view previousSource,
                                      std::string_view previousRendered, std::string_view input,
                                      std::string_view output, std::string_view path) {
        constexpr std::string_view marker="\n在庫リセット\n";
        const auto split=previousSource.find(marker);
        return sameObject && !explicitKey && isTargetPath(path) && mayMatch(previousSource) &&
            split>0 && split==previousSource.rfind(marker) &&
            !translatedResetDate(previousSource.substr(split+marker.size())).empty() &&
            previousSource != previousRendered && input == previousRendered && output == previousRendered;
    }
    static std::string translate(std::string_view text, std::string_view path,
                                 const std::unordered_map<std::string, std::string>& reviewed) {
        constexpr std::string_view marker = "\n在庫リセット\n";
        if (!isTargetPath(path) || !mayMatch(text)) return {};
        const auto split = text.find(marker);
        if (!split || split != text.rfind(marker)) return {};
        const auto body = reviewed.find(std::string(text.substr(0,split)));
        const auto label = reviewed.find("在庫リセット");
        if (body == reviewed.end() || body->second.empty() || label == reviewed.end() || label->second.empty()) return {};
        const auto date=translatedResetDate(text.substr(split+marker.size()));
        return date.empty() ? std::string() : body->second+"\n"+label->second+"\n"+date;
    }
private:
    static std::string translatedResetDate(std::string_view date) {
        const auto slash=date.find('/'), open=date.find('('), close=date.find(')');
        if (slash == std::string_view::npos || open == std::string_view::npos || close == std::string_view::npos ||
            slash >= open || open >= close || close+7 != date.size() || date[close+1]!=' ' || date[close+4]!=':') return {};
        const int month=number(date.substr(0,slash),1,2), day=number(date.substr(slash+1,open-slash-1),1,2);
        const int hour=number(date.substr(close+2,2),2,2), minute=number(date.substr(close+5,2),2,2);
        static constexpr std::array<int,12> days={31,29,31,30,31,30,31,31,30,31,30,31};
        if (month<1 || month>12 || day<1 || day>days[month-1] || hour<0 || hour>23 || minute<0 || minute>59) return {};
        static constexpr std::array<std::string_view,7> ja={"日","月","火","水","木","金","土"};
        static constexpr std::array<std::string_view,7> zh={"周日","周一","周二","周三","周四","周五","周六"};
        const auto weekday=date.substr(open+1,close-open-1);
        for (size_t i=0;i<ja.size();++i) if (weekday==ja[i]) {
            std::string result;
            result.append(date.substr(0,open+1)); result.append(zh[i]); result.append(date.substr(close));
            return result;
        }
        return {};
    }
    static int number(std::string_view text, size_t minimum, size_t maximum) {
        if (text.size()<minimum || text.size()>maximum) return -1;
        int result=0;
        for (char c:text) { if (c<'0' || c>'9') return -1; result=result*10+c-'0'; }
        return result;
    }
};

class ShopScopedQuantityNameMatcher {
public:
    static bool isTargetPath(std::string_view path) {
        return path=="WindowSystem/WindowRoot/ShopWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/SelectItem/MaskBase/Name" ||
            path=="WindowSystem/WindowRoot/ExchangeWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/SelectItem/MaskBase/Name" ||
            path=="WindowSystem/WindowRoot/ShopWindow(Clone)/NormalShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/Name";
    }
    static bool mayMatch(std::string_view text) { return text.size()<=1024 && text.find("×")!=std::string_view::npos; }
    static bool validQuantity(std::string_view quantity) {
        const auto comma=quantity.find(',');
        if (comma==std::string_view::npos) {
            if (quantity.empty() || quantity.size()>6) return false;
            for (char c:quantity) if (c<'0' || c>'9') return false;
            return true;
        }
        if (comma<1 || comma>3 || quantity.front()<'1' || quantity.front()>'9') return false;
        for (size_t i=0;i<comma;++i) if (quantity[i]<'0' || quantity[i]>'9') return false;
        const auto tail=quantity.size()-comma;
        if (tail!=4 && tail!=8) return false;
        for (size_t i=comma;i<quantity.size();++i) {
            if ((i-comma)%4==0) { if (quantity[i]!=',') return false; }
            else if (quantity[i]<'0' || quantity[i]>'9') return false;
        }
        return true;
    }
    static std::string translate(std::string_view text, std::string_view path,
                                 const std::unordered_map<std::string,std::string>& reviewed) {
        if (!isTargetPath(path) || !mayMatch(text)) return {};
        const auto split=text.find("×");
        if (!split || split!=text.rfind("×")) return {};
        const auto quantity=text.substr(split+std::string_view("×").size());
        if (!validQuantity(quantity)) return {};
        const auto base=reviewed.find(std::string(text.substr(0,split)));
        if (base==reviewed.end() || base->second.empty()) return {};
        std::string result=base->second+"×"; result.append(quantity); return result;
    }
};
