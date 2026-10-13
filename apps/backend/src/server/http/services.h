#pragma once

#include <chrono>
#include <string>

#include "gamedata/appearance_tables.h"
#include "state/store.h"
#include "world/world.h"

namespace soulwarden::backend::server::http {

/** A route waits this long for a task of the world. */
inline constexpr std::chrono::milliseconds kWorldCallLimit{2000};

/** Shared backend services that route handlers use. */
struct Services {
    state::Store& store;
    /** Game server entry point that the login token gives to the client. */
    std::string repAddress;
    /** World version text. The client requires its own version text inside this value. */
    std::string worldVersion;
    /** Development aid: an account with no character gets a character with this name at its login. */
    std::string seedCharacter;
    /** Name lists of the client. Not loaded when the game data file is not there. */
    const gamedata::AppearanceTables& appearanceTables;
    /**
     * The world. A route runs in an HTTP thread: it does not read or change world state. It
     * gives the world a task (World::call) and waits for the result.
     */
    world::World& world;
};

} // namespace soulwarden::backend::server::http
