#pragma once

#include <string>
#include <string_view>

namespace soulwarden::backend::encoding {

/** @return URL-safe base64 text without padding. */
[[nodiscard]] std::string base64url(const std::string& data);

/** Decodes URL-safe base64 text without padding. @return False when the text is not correct. */
[[nodiscard]] bool base64url_decode(std::string_view text, std::string& output);

/** @return Standard base64 text with padding. */
[[nodiscard]] std::string base64(const std::string& data);

/** Decodes standard base64 text with or without padding. @return False when the text is not correct. */
[[nodiscard]] bool base64_decode(std::string_view text, std::string& output);

/** @return An unsigned local JSON Web Token with the given payload document. */
[[nodiscard]] std::string local_jwt(const std::string& payloadJson);

/**
 * Reads the payload document of a JSON Web Token. The signature is not checked.
 * @return False when the token does not have three parts or the payload does not decode.
 */
[[nodiscard]] bool jwt_payload(std::string_view token, std::string& payloadJson);

} // namespace soulwarden::backend::encoding
