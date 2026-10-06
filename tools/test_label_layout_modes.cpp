#include "../app/src/main/cpp/font_label_geometry.h"
#include "../app/src/main/cpp/premium_gacha_labels.h"
#include "../app/src/main/cpp/gacha_pickup_layout.h"
#include "../app/src/main/cpp/guild_medal_font.h"
#include "../app/src/main/cpp/guild_points_layout.h"
#include <cassert>
int main() {
    // The support Label used to share the ReplaceAll-only Rank gate: migrating
    // the font policy must not disable its existing Chinese layout profile.
    assert(homeRankOrSupportLayoutCandidate("Label",true));
    assert(homeRankOrSupportLayoutCandidate("Label",false));
    assert(homeRankOrSupportLayoutCandidate("Rank",true));
    assert(!homeRankOrSupportLayoutCandidate("Rank",false));
    assert(!homeRankOrSupportLayoutCandidate("Serif",true));
    assert(!homeRankOrSupportLayoutCandidate("Title",false));
    const std::string mission = "LoveWindow(Clone)/LoveMissionScreen(Clone)/ScreenCanvas/ScreenRoot/MissionTab/";
    const std::string receive = "TabPageContainer/LimitedPage/LoveMissionList/Scroll/Viewport/Content/LoveMissionListCell(Clone)/AnimationRoot/MissionCell/ReceiveButton/Text";
    assert(loveMissionLabel(mission + receive, false));
    assert(loveMissionLabel("WindowSystem/WindowRoot/" + mission + receive, false));
    assert(!loveMissionLabel("Other" + mission + receive, false));
    assert(!loveMissionLabel(mission + receive + "/Icon", false));
    assert(loveMissionLabel(mission + "FloatingActionButton/Base/Text", true));
    assert(!loveMissionLabel(mission + "FloatingActionButton/Base/Text", false));
    assert(!loveMissionLabel(mission + receive, true));
    using premium_gacha_labels::hidePurchaseParticle;
    const std::string particle(premium_gacha_labels::purchaseParticlePath);
    assert(hidePurchaseParticle("を", particle, true));
    assert(!hidePurchaseParticle("を", particle, false));
    assert(!hidePurchaseParticle("で", particle, true));
    assert(!hidePurchaseParticle("を含む文章", particle, true));
    assert(!hidePurchaseParticle("を", particle + "/Other", true));
    assert(!hidePurchaseParticle("を", "Other/ShopButton/Text/Layout/Sub", true));
    std::string drawParticle = particle;
    drawParticle.replace(drawParticle.find("ShopButton"), 10, "TicketButton");
    assert(!hidePurchaseParticle("を", drawParticle, true));
    using gacha_pickup_layout::classify;
    using gacha_pickup_layout::Label;
    const std::string pickup = "GachaWindow(Clone)/NormalGachaScreen(Clone)/NormalGachaSheet(Clone)/Canvas/Root/UIContentArea/SheetContainer/ContentContainer/ContentMask/ContentViewport/Content/";
    assert(classify(pickup + "PickupInfo/Description", "请选择作为精选对象的偶像。") == Label::Instruction);
    assert(classify("WindowSystem/WindowRoot/HomeWindow(Clone)/" + pickup + "PickupInfo/Description", "请选择作为精选对象的偶像。") == Label::Instruction);
    for (const auto* group : {"DrawButtons/StoneButton", "OtherButtons/OtherButton"}) {
        for (int i = 1; i <= 3; ++i) {
            const auto path = pickup + group + std::to_string(i) + "/description";
            assert(classify(path, "请完成精选。") == Label::DrawPrompt);
            assert(classify(path, "请完成\n精选。") == Label::DrawPrompt);
            assert(classify(path, "请完成\n精选\n。") == Label::DrawPrompt);
            assert(classify(path, "钻石不足") == Label::None);
            assert(classify(path, "ピックアップを完了してください。") == Label::None);
            assert(classify(path + "/Label", "请完成精选。") == Label::None);
        }
    }
    assert(classify(pickup + "DrawButtons/StoneButton10/description", "请完成精选。") == Label::None);
    assert(classify(pickup + "DrawButtons/StoneButton4/description", "请完成精选。") == Label::None);
    assert(classify("Other" + pickup + "DrawButtons/StoneButton1/description", "请完成精选。") == Label::None);
    assert(classify(pickup + "PickupInfo/PickupCardList/Label", "请完成精选。") == Label::None);
    const std::string guild = "WindowSystem/WindowRoot/LiveWindow(Clone)/GuildWindow(Clone)/GuildTopScreen(Clone)/ScreenCanvas/ScreenRoot/";
    assert(guildTopButtonTitle(guild + "LiveButton/Title"));
    assert(guildTopButtonTitle(guild + "TrainingButton/Title"));
    assert(!guildTopButtonTitle(guild + "LiveButton/Season"));
    assert(!guildTopButtonTitle(guild + "TrainingButton/Title/Child"));
    assert(!guildTopButtonTitle("Other/TrainingButton/Title"));
    float x = 0;
    // Captured left-pivot title boxes differ in width; both centers must land
    // on the center of their respective parent, with no cumulative offset.
    assert(centeredLabelX(-252.5f,505,421,.5f,.5f,0,x) && std::fabs(x + 210.5f) < .001f);
    assert(centeredLabelX(-251.5f,503,360,.5f,.5f,0,x) && std::fabs(x + 180) < .001f);
    assert(centeredLabelX(-503,1006,720,.5f,.5f,0,x) && std::fabs(x + 360) < .001f);
    using guild_medal_font::decorate;
    assert(decorate("联盟奖牌：2,146", "Original SDF") == "联盟奖牌：<font=\"Original SDF\">2,146</font>");
    assert(decorate("ユニオンメダル：2,146", "Original SDF") == "联盟奖牌：<font=\"Original SDF\">2,146</font>");
    assert(decorate("ユニオンメダル：2,14", "Original SDF").empty());
    assert(decorate("联盟奖牌：0", "Original SDF") == "联盟奖牌：<font=\"Original SDF\">0</font>");
    assert(decorate("联盟奖牌：12,345,678", "Original SDF") == "联盟奖牌：<font=\"Original SDF\">12,345,678</font>");
    assert(decorate(decorate("联盟奖牌：2,146", "Original SDF"), "Original SDF").empty());
    assert(decorate("其他奖牌：2,146", "Original SDF").empty());
    assert(decorate("联盟奖牌：2,,146", "Original SDF").empty());
    assert(decorate("联盟奖牌：2,14", "Original SDF").empty());
    assert(decorate("联盟奖牌：,146", "Original SDF").empty());
    assert(decorate("联盟奖牌：146,", "Original SDF").empty());
    assert(decorate("联盟奖牌：", "Original SDF").empty());
    assert(decorate("联盟奖牌：2,146", "Bad\"Font").empty());
    assert(decorate("联盟奖牌：2,146", "").empty());
    using namespace guild_points_layout;
    assert(guild_points_layout::classify(seasonPath) == Kind::Season);
    assert(guild_points_layout::classify(rankingPath) == Kind::Ranking);
    assert(guild_points_layout::classify("Other/Point") == Kind::None);
    assert(guild_points_layout::classify(std::string(rankingPath) + "Label") == Kind::None);
    float nativeDescent, notoDescent;
    assert(scaledDescent({34, 1, 29.92f, -4.08f}, 40, nativeDescent));
    assert(scaledDescent({90, 1, 104.4f, -25.92f}, 40, notoDescent));
    assert(!scaledDescent({0, 1, 0, 0}, 40, nativeDescent));
    const float y = bottomBaselineY(-139.5f, -41.53253f, 0, notoDescent, -41.53253f, 0, nativeDescent);
    assert(std::fabs(y - (-132.78f)) < .001f);
    // Both rendered baselines coincide, independently of prior number Y.
    assert(std::fabs((y - 41.53253f - nativeDescent) - (-139.5f - 41.53253f - notoDescent)) < .001f);
    assert(std::fabs(bottomBaselineY(-139.5f, -41.53253f, 3, notoDescent, -20, 2, nativeDescent) -
        (y - 20.53253f)) < .001f);
}
