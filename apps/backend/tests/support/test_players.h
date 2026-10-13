#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include "state/store.h"
#include "support/client_frames.h"

// Accounts and characters of the tests. All values are made up. A character has a fixed id,
// so that the bytes that a session sends are the same in each run.

namespace soulwarden::backend::tests {

struct Player {
    state::Account account;
    std::string steamId;
    std::string personaName;
    std::string characterId;
    std::string characterName;

    /** @return The fields of the registration request of this player. */
    [[nodiscard]] Registration registration() const {
        return Registration{characterId, account.personaId, steamId, personaName};
    }
};

/** Adds one account with one character to the store. */
inline Player add_player(state::Store& store,
                         const std::string& steamId,
                         const std::string& personaName,
                         const std::string& characterId,
                         const std::string& characterName,
                         std::optional<state::WorldPosition> position) {
    Player player;
    player.steamId = steamId;
    player.personaName = personaName;
    player.characterId = characterId;
    player.characterName = characterName;
    state::AccountOrigin origin{};
    const auto account = store.login_account(steamId, personaName, false, origin);
    if (!account) {
        throw std::runtime_error("test store: no account");
    }
    player.account = *account;
    state::Character character;
    character.characterId = characterId;
    character.personaId = account->personaId;
    character.worldId = kWorldId;
    character.name = characterName;
    character.createdDate = "2026-01-01T00:00:00.000Z";
    character.modifiedDate = character.createdDate;
    if (store.insert_character(character) != state::InsertResult::inserted) {
        throw std::runtime_error("test store: the character was not inserted");
    }
    if (position && !store.set_character_position(characterId, *position)) {
        throw std::runtime_error("test store: the position was not set");
    }
    return player;
}

/** @return The saved position of a character, or no value. */
inline std::optional<state::WorldPosition> saved_position(state::Store& store, const std::string& characterId) {
    const auto character = store.find_character(characterId);
    return character ? character->position : std::nullopt;
}

} // namespace soulwarden::backend::tests
