#include "core/encoding/hex.h"

#include <utility>

namespace soulwarden::backend::encoding {
namespace {

/** @return The value of one hexadecimal digit, or -1. */
int digit_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

} // namespace

bool hex_decode(std::string_view text, std::string& output) {
    if (text.size() % 2 != 0) {
        return false;
    }
    std::string decoded;
    decoded.reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        const int high = digit_value(text[index]);
        const int low = digit_value(text[index + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded += static_cast<char>((high << 4) | low);
    }
    output = std::move(decoded);
    return true;
}

} // namespace soulwarden::backend::encoding
