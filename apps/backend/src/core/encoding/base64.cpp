#include "core/encoding/base64.h"

#include <utility>

namespace soulwarden::backend::encoding {
namespace {

/** @return The value of one character of the standard or of the URL-safe alphabet, or -1. */
int digit_value(char character, bool urlSafe) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == (urlSafe ? '-' : '+')) {
        return 62;
    }
    if (character == (urlSafe ? '_' : '/')) {
        return 63;
    }
    return -1;
}

/** Decodes text that has no padding. @return False when the text is not correct. */
bool decode(std::string_view text, bool urlSafe, std::string& output) {
    // One character alone cannot hold a full byte.
    if (text.size() % 4 == 1) {
        return false;
    }
    std::string decoded;
    unsigned value = 0;
    int bits = 0;
    for (const char character : text) {
        const int digit = digit_value(character, urlSafe);
        if (digit < 0) {
            return false;
        }
        value = (value << 6) | static_cast<unsigned>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded += static_cast<char>((value >> bits) & 0xFF);
        }
    }
    output = std::move(decoded);
    return true;
}

std::string encode(const std::string& data, const char* alphabet, bool padding) {
    std::string output;
    std::size_t index = 0;
    while (index + 2 < data.size()) {
        const unsigned value = (static_cast<unsigned char>(data[index]) << 16)
                               | (static_cast<unsigned char>(data[index + 1]) << 8)
                               | static_cast<unsigned char>(data[index + 2]);
        output += alphabet[(value >> 18) & 63];
        output += alphabet[(value >> 12) & 63];
        output += alphabet[(value >> 6) & 63];
        output += alphabet[value & 63];
        index += 3;
    }
    const std::size_t remaining = data.size() - index;
    if (remaining == 1) {
        const unsigned value = static_cast<unsigned char>(data[index]) << 16;
        output += alphabet[(value >> 18) & 63];
        output += alphabet[(value >> 12) & 63];
        if (padding) output += "==";
    } else if (remaining == 2) {
        const unsigned value = (static_cast<unsigned char>(data[index]) << 16)
                               | (static_cast<unsigned char>(data[index + 1]) << 8);
        output += alphabet[(value >> 18) & 63];
        output += alphabet[(value >> 12) & 63];
        output += alphabet[(value >> 6) & 63];
        if (padding) output += '=';
    }
    return output;
}

} // namespace

std::string base64url(const std::string& data) {
    return encode(data, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", false);
}

bool base64url_decode(std::string_view text, std::string& output) {
    return decode(text, true, output);
}

std::string base64(const std::string& data) {
    return encode(data, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", true);
}

bool base64_decode(std::string_view text, std::string& output) {
    while (!text.empty() && text.back() == '=') {
        text.remove_suffix(1);
    }
    return decode(text, false, output);
}

std::string local_jwt(const std::string& payloadJson) {
    return base64url(R"({"alg":"none","typ":"JWT","kid":"soulwarden-local"})") + "." + base64url(payloadJson) + "."
           + base64url("soulwarden-local-signature");
}

bool jwt_payload(std::string_view token, std::string& payloadJson) {
    const std::size_t first = token.find('.');
    const std::size_t second = first == std::string_view::npos ? first : token.find('.', first + 1);
    if (second == std::string_view::npos) {
        return false;
    }
    return base64url_decode(token.substr(first + 1, second - first - 1), payloadJson);
}

} // namespace soulwarden::backend::encoding
