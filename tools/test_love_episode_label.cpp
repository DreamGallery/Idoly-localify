#include "../app/src/main/cpp/love_episode_label.h"
#include <cassert>
int main() {
    using namespace love_episode_label;
    assert(format(path, "1") == "第1话进行中");
    assert(format(path, "28") == "第28话进行中");
    assert(format(path, "9999") == "第9999话进行中");
    assert(format(path, "N") == "");
    assert(format(path, "第1话进行中") == "第1话进行中");
    assert(format(path, "第28话进行中") == "第28话进行中");
    assert(!format(path, "10000"));
    assert(!format(path, "1号"));
    assert(!format(path, "１"));
    assert(!format(path, "1.0"));
    assert(!format(path, ""));
    assert(!format(std::string(path) + "/Other", "1"));
    assert(!format("Other/" + std::string(path), "1"));
    assert(!format("WindowSystem/WindowRoot/Other/EpisodeText", "1"));
    const auto parse = [](const std::u32string& text) {
        return numberBuffer(text.size() + 1, [&](size_t i) { return text[i]; });
    };
    assert(parse(U"1") == "1");
    assert(parse(U"28") == "28");
    assert(parse(U"9999") == "9999");
    assert(!parse(U"10000"));
    assert(!parse(U""));
    assert(!parse(U"第1话进行中"));
    assert(!parse(U"１"));
    assert(!parse(U"1.0"));
    assert(!numberBuffer(1, [](size_t) { return '1'; }));
}
