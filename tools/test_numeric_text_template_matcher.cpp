#include <cassert>
#include <string>

#include "../app/src/main/cpp/numeric_text_template_matcher.h"

int main() {
    NumericTextTemplateMatcher matcher;
    assert(matcher.add("{number}日目のログインボーナスです",
                       "这是第{number}天的登录奖励"));
    assert(matcher.size() == 1);
    assert(matcher.translate("2日目のログインボーナスです") == "这是第2天的登录奖励");
    assert(matcher.translate("8日目のログインボーナスです") == "这是第8天的登录奖励");
    assert(matcher.translate("100日目のログインボーナスです") == "这是第100天的登录奖励");
    assert(matcher.isRendered("这是第100天的登录奖励"));
    assert(!matcher.isRendered("这是第经理天的登录奖励"));
    assert(matcher.translate("经理日目のログインボーナスです").empty());
    assert(matcher.translate("２日目のログインボーナスです").empty());
    assert(matcher.translate("2日目のログインボーナスです。 ").empty());
    assert(matcher.translate("2日目のログインボーナスです。{user}").empty());
    assert(matcher.translate("1234567日目のログインボーナスです").empty());
    assert(!matcher.add("{number}！", "第{number}天"));
    assert(!matcher.add("{number}日目のログインボーナスです",
                        "重复第{number}天"));
    assert(!matcher.add("{number}{number}日目のログインボーナスです",
                        "这是第{number}天的登录奖励"));
    assert(!matcher.add("{user}日目のログインボーナスです",
                        "这是第{number}天的登录奖励"));
    assert(!matcher.add("{number}日目のログインボーナスです",
                        "这是第{number}天的登录奖励{other}"));
}
