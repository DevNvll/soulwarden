#pragma once

#include <string>
#include <string_view>

namespace soulwarden::backend::encoding {

/**
 * Decodes hexadecimal text of any letter case.
 * @return False when the text has an odd size or a character that is not a hexadecimal digit.
 */
[[nodiscard]] bool hex_decode(std::string_view text, std::string& output);

} // namespace soulwarden::backend::encoding
