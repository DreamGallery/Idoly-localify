#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tmp_text_signature {
inline bool stringSetter(std::string_view result, bool isStatic,
                         const std::vector<std::string>& parameters) {
    return !isStatic && result == "System.Void" &&
        ((parameters.size() == 1 && parameters[0] == "System.String") ||
         (parameters.size() == 2 && parameters[0] == "System.String" &&
          parameters[1] == "System.Boolean"));
}
} // namespace tmp_text_signature
