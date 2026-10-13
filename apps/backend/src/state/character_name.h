#pragma once

#include <string>
#include <string_view>

// Name of a character: the rule of the client for a new name, and the compare form.
//
// Name rule (client code at image RVA 0x5CF59A0, called for a character name by 0x41F6530
// with the limits 3 and 15 and the pattern text at image RVA 0x8159340):
//   - 3 to 15 characters;
//   - no two spaces in sequence, and no space at the start or at the end;
//   - only these characters: space ' , - . the digits, the ASCII letters, and letters from
//     U+00C0 to U+017E (the pattern has some gaps);
//   - 6 digits at most.
// The client applies the rule before it sends a name. The backend applies it again: a name
// that the client refuses does not get into the database.
//
// Compare form: two names with the same compare form are one name. A name is unique for all
// accounts with no regard to the letter case. The compare form has the lower-case letter for
// each letter that the rule permits. SQLite changes the case of ASCII letters only (COLLATE
// NOCASE), thus the store compares this form.

namespace soulwarden::backend::state {

/** Result of the name rule. */
enum class NameRule {
    permitted,
    /** The name has less than 3 or more than 15 characters. */
    length,
    /** A character, the number of digits, or the position of a space is not permitted. */
    characters,
};

/**
 * Applies the name rule of the client. The client has one more rule that the backend does
 * not have: a list of words that are not permitted.
 * @param name UTF-8 text.
 */
[[nodiscard]] NameRule check_name(std::string_view name);

/**
 * @param name UTF-8 text. Bytes that are not UTF-8 stay as they are.
 * @return The name with each letter up to U+017F in lower case.
 */
[[nodiscard]] std::string name_compare_form(std::string_view name);

} // namespace soulwarden::backend::state
