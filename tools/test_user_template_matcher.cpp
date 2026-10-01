#include <cassert>
#include <string>

#include "../app/src/main/cpp/user_template_matcher.h"

int main() {
    UserTemplateMatcher matcher;
    matcher.add("啊！来了来了！\\n{user}先生！");
    matcher.add("啊！来了来了！\n{user}先生！");
    matcher.add("{user}，今天也一起努力吧！");
    matcher.add("马上就在这里等着{user}");
    matcher.add("{user}也一起去，{user}也一起回来！");
    matcher.add("{user}！"); // Too little context to identify a translated line.
    assert(matcher.size() == 5);
    assert(matcher.matches("啊！来了来了！\\n经理先生！"));
    assert(matcher.matches("啊！来了来了！\n经理先生！"));
    assert(matcher.matches("经理，今天也一起努力吧！"));
    assert(matcher.matches("马上就在这里等着经理"));
    assert(matcher.matches("经理也一起去，经理也一起回来！"));
    assert(!matcher.matches("经理也一起去，社长也一起回来！"));
    assert(!matcher.matches("啊！来了来了！\\n先生！"));
    assert(!matcher.matches("其他人！"));
    assert(!matcher.matches("经理，今天也一起休息吧！"));
    assert(!matcher.matches(std::string(129, 'x') + "，今天也一起努力吧！"));
}
