#include "../app/src/main/cpp/shop_exchange_description_matcher.h"
#include <cassert>
int main() {
    const std::string path="WindowSystem/WindowRoot/ShopWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/SelectItem/MaskBase/Description";
    std::string actualPath=path; actualPath.replace(actualPath.find("ShopWindow"),10,"ExchangeWindow");
    const std::string body="アイドルを才能開花させることができる";
    const std::string prefix=body+"\n在庫リセット\n";
    std::unordered_map<std::string,std::string> reviewed={{body,"可让偶像才能开花"},{"在庫リセット","库存重置"}};
    const auto match=[&](std::string date) {return ShopExchangeDescriptionMatcher::translate(prefix+date,path,reviewed);};
    assert(match("10/1(木) 00:00")=="可让偶像才能开花\n库存重置\n10/1(周四) 00:00");
    assert(match("12/31(月) 23:59")=="可让偶像才能开花\n库存重置\n12/31(周一) 23:59");
    assert(match("2/29(日) 01:02").find("2/29(周日) 01:02")!=std::string::npos);
    for (const auto date:{"10/1(木)00:00","10/1(木) 00:00 extra","10/1(木) 00:00\n","2027/10/1(木) 00:00","13/1(木) 00:00","4/31(木) 00:00","2/30(木) 00:00","10/0(木) 00:00","10/1(木) 24:00","10/1(木) 00:60","10/1(X) 00:00","１０/1(木) 00:00"}) assert(match(date).empty());
    assert(ShopExchangeDescriptionMatcher::translate(prefix+"10/1(木) 00:00",path+"Extra",reviewed).empty());
    assert(ShopExchangeDescriptionMatcher::translate("未知"+prefix+"10/1(木) 00:00",path,reviewed).empty());
    assert(ShopExchangeDescriptionMatcher::translate(body+"\r\n在庫リセット\n10/1(木) 00:00",path,reviewed).empty());
    assert(ShopExchangeDescriptionMatcher::translate(prefix+prefix+"10/1(木) 00:00",path,reviewed).empty());
    const std::string original=prefix+"10/1(木) 00:00";
    const std::string rendered=match("10/1(木) 00:00");
    const auto preserve=[&](bool same, bool key, std::string input, std::string output, std::string targetPath) {
        return ShopExchangeDescriptionMatcher::preserveTrackedSource(same,key,original,rendered,input,output,targetPath);
    };
    assert(ShopExchangeDescriptionMatcher::translate(original,actualPath,reviewed)==rendered);
    assert(preserve(true,false,rendered,rendered,actualPath));
    assert(!preserve(true,false,rendered,rendered,actualPath+"Extra"));
    assert(ShopExchangeDescriptionMatcher::translate(original,actualPath+"Extra",reviewed).empty());
    assert(preserve(true,false,rendered,rendered,path)); // Repeated setter retains reload source.
    assert(!preserve(false,false,rendered,rendered,path)); // Reused address.
    assert(!preserve(true,true,rendered,rendered,path)); // Explicit new I18n assignment.
    assert(!preserve(true,false,prefix+"11/1(日) 00:00",rendered,path)); // New date is new source.
    assert(!preserve(true,false,"new body",rendered,path));
    assert(!preserve(true,false,rendered,"changed output",path));
    assert(!preserve(true,false,rendered,rendered,path+"Extra"));
    const std::string exchangeName="WindowSystem/WindowRoot/ShopWindow(Clone)/ExchangeDetailScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/SelectItem/MaskBase/Name";
    const std::string shopName="WindowSystem/WindowRoot/ShopWindow(Clone)/NormalShopScreen(Clone)/ScreenCanvas/ScreenRoot/ScreenSplitArea/Fullscreen/Upper/Header/Name";
    reviewed["ブルームピース"]="开花碎片";
    std::string actualName=exchangeName; actualName.replace(actualName.find("ShopWindow"),10,"ExchangeWindow");
    assert(ShopScopedQuantityNameMatcher::translate("ブルームピース×10",actualName+"Extra",reviewed).empty());
    for (const auto& namePath:{exchangeName,actualName,shopName}) {
        assert(ShopScopedQuantityNameMatcher::translate("ブルームピース×10",namePath,reviewed)=="开花碎片×10");
        assert(ShopScopedQuantityNameMatcher::translate("ブルームピース×999999",namePath,reviewed)=="开花碎片×999999");
        for (const auto quantity:{"3,000", "10,000,000", "999,999,999"}) {
            const std::string suffix=std::string("×")+quantity;
            assert(ShopScopedQuantityNameMatcher::translate("ブルームピース"+suffix,namePath,reviewed)=="开花碎片"+suffix);
        }
        for (const auto quantity:{"3,00", "3,0000", "03,000", "0,000", "1000,000", "1,000,000,000", "1,,000", "1,000,00", "1,０００"})
            assert(ShopScopedQuantityNameMatcher::translate(std::string("ブルームピース×")+quantity,namePath,reviewed).empty());
        for (const auto input:{"ブルームピース×", "ブルームピース×1234567", "ブルームピース×１０", "ブルームピース×10 extra", "ブルームピース×10×2", "ブルームピースx10", "未知×5"})
            assert(ShopScopedQuantityNameMatcher::translate(input,namePath,reviewed).empty());
    }
    assert(ShopScopedQuantityNameMatcher::translate("ブルームピース×10",exchangeName+"Extra",reviewed).empty());
    assert(ShopScopedQuantityNameMatcher::translate("ブルームピース×3,000",actualName+"Extra",reviewed).empty());
    assert(ShopScopedQuantityNameMatcher::translate("未知×3,000",actualName,reviewed).empty());
    assert(ShopScopedQuantityNameMatcher::translate("ブルームピース×3,000×2",actualName,reviewed).empty());
    reviewed["ブルームピース"]="";
    assert(ShopScopedQuantityNameMatcher::translate("ブルームピース×10",shopName,reviewed).empty());
    reviewed[body]=""; assert(match("10/1(木) 00:00").empty());
    reviewed[body]="可让偶像才能开花"; reviewed.erase("在庫リセット"); assert(match("10/1(木) 00:00").empty());
}
