#include <chrono>
#include <cstdlib>

#include <nlohmann/json.hpp>

#include "core/logging/log.h"
#include "middleware/steam/local_ticket.h"
#include "server/http/routes.h"
#include "server/http/session.h"
#include "state/character_name.h"
#include "state/creation_data.h"
#include "state/identity.h"
#include "state/world.h"

// Javelin HTTP gateway ("JavelinGatewayService-CF"). The client sends the account token
// in the x-nw-auth header and signs each call with the credentials from this service.
// The token names the account of a request: a route that reads or changes player data
// starts with request_account.
// Field names and wrappers come from the client model readers (image RVA 0x74E0000 area).
//
// Error reply: the client reads the error name from the header x-amzn-ErrorType, else from
// the JSON member __type (AWS JSON error reader at image RVA 0x7481A00), and shows the text
// @mm_loginservices_<name>. A name that a route does not expect opens a "connection failed"
// popup. Use a 4xx status: the client can send a request with a 5xx reply again.
// The reply 401 for a token that names no account has no error name (unauthorized).

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;

std::int64_t now_milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/**
 * GET /prod/credentials/omni
 * Reader: AuthStackCredentialsProvider. `expiration` is Unix time in milliseconds.
 */
service::HttpResponse omni_credentials(const RouteContext&) {
    const json reply = {
        {"accessKeyId", "AKIASOULWARDENLOCAL000001"},
        {"secretAccessKey", "soulwarden/local/secret/access/key/0000000001"},
        {"sessionToken", "soulwarden-local-session-token"},
        {"expiration", now_milliseconds() + 12LL * 3600 * 1000},
    };
    return json_reply(200, reply.dump());
}

/** Error names that the client has a text for (localization keys mm_loginservices_<name>). */
constexpr const char* kErrorNameNotAvailable = "NameNotAvailableError";
constexpr const char* kErrorNameLength = "NameLengthError";
constexpr const char* kErrorNameValidation = "NameValidationError";
constexpr const char* kErrorNameInUse = "ALREADY_EXISTS";
constexpr const char* kErrorTooManyCharacters = "TooManyCharactersError";
constexpr const char* kErrorWorldNotEligible = "WorldNotEligibleForCharacterCreation";
constexpr const char* kErrorCreateFailed = "CreateCharacterFailed";
/** This reply does not occur in a session of the client: its result in the client was not seen. */
constexpr const char* kErrorUnknownCharacter = "UnknownCharacter";

/** @return An error reply in the form that the client reads. */
service::HttpResponse gateway_error(int status, const char* name, const char* message) {
    service::HttpResponse response = json_reply(status, json{{"__type", name}, {"message", message}}.dump());
    response.headers.emplace_back("x-amzn-ErrorType", name);
    return response;
}

/** Reply to a request with an access token that names no account. */
service::HttpResponse unauthorized() {
    log::write("gateway: no account for the access token");
    return json_reply(401, R"({"message":"Unauthorized"})");
}

/**
 * Parses a request body. The client wraps each body in one named object, for example
 * {"LoginQueueRequest": {...}}. The wrapper is removed.
 */
json request_body(const service::HttpRequest& request) {
    json body = json::parse(request.body, nullptr, false);
    if (body.is_object() && body.size() == 1 && body.begin().value().is_object()) {
        return body.begin().value();
    }
    return body;
}

/**
 * @return The text member of a request body. An empty text when the body is not an object, or
 *         when the member is not there or is not a text.
 */
std::string text_member(const json& body, const char* name) {
    if (!body.is_object()) {
        return {};
    }
    const auto member = body.find(name);
    return member != body.end() && member->is_string() ? member->get<std::string>() : std::string{};
}

/** @return The integer value of one query parameter, or the fallback. */
int query_integer(const service::HttpRequest& request, const std::string& name, int fallback) {
    const std::string needle = name + "=";
    std::size_t position = 0;
    while (position < request.query.size()) {
        const std::size_t end = request.query.find('&', position);
        const std::string pair = request.query.substr(position, end == std::string::npos ? end : end - position);
        if (pair.rfind(needle, 0) == 0) {
            return std::atoi(pair.c_str() + needle.size());
        }
        if (end == std::string::npos) {
            break;
        }
        position = end + 1;
    }
    return fallback;
}

json world_json(const Services& services) {
    const state::LocalWorld& world = state::local_world();
    return {
        {"WorldId", world.worldId},
        {"WorldName", world.name},
        {"PublicName", world.name},
        {"WorldSet", world.worldSet},
        {"WorldStatus", "ACTIVE"},
        {"WorldType", "OpenWorld"},
        {"WorldVersion", services.worldVersion},
        {"PublicStatusCode", 0},
        {"ConnectionCount", 1},
        {"MaxConnectionCount", world.maxConnectionCount},
        {"MaxAccountCharacters", world.maxAccountCharacters},
        {"IsFull", false},
        {"IsRecommended", true},
        {"TransferToRegion", ""},
        {"WorldMetrics",
         {
             {"QueueSize", 0},
             {"QueueWaitTimeSec", 0},
             {"WorldAgeDays", 30},
             {"WorldPopulationStatus", "low"},
         }},
    };
}

/**
 * @return The text for PublishedData of a character: its menu document, made at each request with the one writer from the values
 *         of the character (stage U4; the original server made the text from the character at each publish, 0x67F0600). The
 *         appearance names and the backstory come from its creation data, which does not change; a character that the backend
 *         seeded has none and gets the default appearance. An empty text for such a character when the backend has no appearance
 *         tables.
 */
std::string published_data(const Services& services, const state::Character& character) {
    state::MenuDocument document;
    document.name = character.name;
    document.worldId = character.worldId;
    // The create route keeps only creation data that it can read. A row from before that check can have a different text: the
    // client does not get such a text, and the row gets the default appearance.
    if (const auto creationData = state::read_creation_data(character.creationParams)) {
        document.appearance = creationData->appearance;
        document.backstory = creationData->archetype;
    } else if (const auto names = services.appearanceTables.names(state::kDefaultAppearance)) {
        document.appearance = *names;
    } else {
        return {};
    }
    // The level of the row for the character list of the main menu (the member `currentLevel`, protocol notes 2.2.5 and 4.12).
    document.level = services.store.progress(character.characterId).level;
    // The clothing of the menu body: the armor rows of the paperdoll in the store. The inventory system writes the rows of a change
    // before it changes the entity, thus the rows are the equipment of the entity, also while the character is in the world, and
    // this thread reads no world state. One item query for each character (the place index of the table), no scan of the world.
    // The catalog is a read-only object that the backend loads at its start.
    const gamedata::ItemCatalog* catalog = services.world.options().itemCatalog;
    if (catalog != nullptr && catalog->loaded()) {
        const std::vector<state::ItemRow> armor = services.store.list_slots(
            character.characterId, state::kContainerPaperdoll, 0, static_cast<std::uint32_t>(state::kMenuSlotKeys.size() - 1));
        document.equipment = world::menu_equipment(*catalog, armor, state::female_body(document.appearance));
    }
    return state::make_menu_document(document);
}

json character_json(const Services& services, const state::Character& character) {
    json document = {
        {"CharacterId", character.characterId},
        {"PersonaId", character.personaId},
        {"WorldId", character.worldId},
        {"Name", character.name},
        {"CreatedDate", character.createdDate},
        {"ModifiedDate", character.modifiedDate},
        {"NameModifiedDate", character.createdDate},
        {"FtueCompleted", character.ftueCompleted},
        {"IsFreshStart", false},
        {"IsNameLatent", false},
        {"IsTrialOwner", false},
        {"MustRename", false},
        {"NeedsTransfer", false},
        {"Transferrable", false},
        {"OwnerState", "ACTIVE"},
    };
    // The menu makes the body of the character from this text (client reader at image RVA
    // 0x171A500 and 0x16B3850): the appearance and the clothing. It has the same form as the
    // creation data of the create request. Without it the menu shows a white body.
    const std::string publishedData = published_data(services, character);
    if (!publishedData.empty()) {
        document["PublishedData"] = publishedData;
    }
    return document;
}

/** GET /prod/game/getlogininfo: worlds, and the characters of the account of the request. */
service::HttpResponse get_login_info(const Services& services, const RouteContext& context) {
    const auto account = request_account(services, context.request);
    if (!account) {
        return unauthorized();
    }
    json characters = json::array();
    for (const state::Character& character : services.store.list_characters(account->personaId)) {
        characters.push_back(character_json(services, character));
    }
    log::write("gateway: account %s, Steam id %s, has %zu character(s)",
               account->personaId.c_str(),
               state::masked_steam_id(account->steamId).c_str(),
               characters.size());
    const json reply = {
        {"LoginInfoList",
         {
             {"Characters", characters},
             {"Worlds", json::array({world_json(services)})},
             {"NameReservations", json::array()},
             {"PendingWorldMerges", json::array()},
             {"MaxChannelCharacters", state::local_world().maxAccountCharacters},
             {"CrossRegionTransferCooldownMins", 0},
         }},
    };
    return json_reply(200, reply.dump());
}

/** GET /prod/game/worlds/list */
service::HttpResponse list_worlds(const Services& services, const RouteContext&) {
    const json world = world_json(services);
    const json reply = {
        {"ListWorldsResponseBody",
         {
             {"Worlds", json::array({world})},
             {"RecommendedWorlds",
              json::array({json{{"RecommendationType", "Recommended"}, {"World", world}}})},
         }},
    };
    return json_reply(200, reply.dump());
}

/**
 * POST /prod/game/worlds/{world}/characters/validator: body {"Name": text}.
 * The client reads no body of a 2xx reply (job at image RVA 0x6419EF0). Its result callback
 * (image RVA 0x2EAB900) shows the error names of a name as the state of the name field.
 * The client sends only a name that its name rule permits (state/character_name.h).
 */
service::HttpResponse validate_character(const Services& services, const RouteContext& context) {
    const std::string name = text_member(request_body(context.request), "Name");
    switch (state::check_name(name)) {
    case state::NameRule::permitted:
        break;
    case state::NameRule::length:
        log::write("gateway: the name check refused a name of %zu bytes: its length", name.size());
        return gateway_error(400, kErrorNameLength, "Name must have 3 to 15 characters");
    case state::NameRule::characters:
        log::write("gateway: the name check refused a name of %zu bytes: its characters", name.size());
        return gateway_error(400, kErrorNameValidation, "Name has characters that are not permitted");
    }
    if (services.store.character_name_exists(name)) {
        log::write("gateway: the name %s is in use", name.c_str());
        return gateway_error(400, kErrorNameNotAvailable, "Name is not available");
    }
    return json_reply(200, "{}");
}

/**
 * POST /prod/game/worlds/{world}/characters: creates one character for the account of the
 * request. The client reads only Character.CharacterId of the reply (job at image RVA
 * 0x6414350) and then logs in with that id.
 */
service::HttpResponse create_character(const Services& services, const RouteContext& context) {
    const auto account = request_account(services, context.request);
    if (!account) {
        return unauthorized();
    }
    const json body = request_body(context.request);
    state::Character character;
    character.name = text_member(body, "Name");
    // The client makes the name check before this request. A request with a name that the
    // name rule refuses does not come from the client: the text of the name is not logged.
    if (state::check_name(character.name) != state::NameRule::permitted) {
        log::write("gateway: no character for account %s: the name rule refuses the name (%zu bytes)",
                   account->personaId.c_str(),
                   character.name.size());
        return gateway_error(400, kErrorCreateFailed, "Name is not permitted");
    }
    character.characterId = state::new_uuid();
    character.personaId = account->personaId;
    character.worldId = context.parameters.at("world");
    character.createdDate = state::now_iso8601();
    character.modifiedDate = character.createdDate;
    // The backend has no tutorial world: a new character starts in the open world. The
    // member SkipFtue of the request is not kept.
    character.ftueCompleted = true;
    if (character.worldId != state::local_world().worldId) {
        return gateway_error(404, kErrorWorldNotEligible, "World not found");
    }
    // The client reads this text again as PublishedData of the character (character_json).
    // Thus the backend keeps only a text that has the form of the client.
    character.creationParams = text_member(body, "CharacterCreationParams");
    const auto creationData = state::read_creation_data(character.creationParams);
    if (!creationData) {
        log::write("gateway: no character %s for account %s: the creation data (%zu bytes) did not parse",
                   character.name.c_str(),
                   account->personaId.c_str(),
                   character.creationParams.size());
        return gateway_error(400, kErrorCreateFailed, "Creation data is not valid");
    }
    character.archetype = creationData->archetype;
    if (services.store.list_characters(character.personaId).size()
        >= static_cast<std::size_t>(state::local_world().maxAccountCharacters)) {
        return gateway_error(409, kErrorTooManyCharacters, "Character limit reached");
    }
    // The items of the archetype: the clothing and the first weapon of a new character. The
    // catalog is a read-only object that the backend loads at its start, thus this thread can
    // read it. The rows go into the database in the transaction that inserts the character.
    //
    // The backstory table of the game has rows for tests and for later parts of the game, with
    // items that a new character must not get. The catalog has the list of the backstories
    // that the character creation of the client can send: a request with a different backstory
    // does not come from the creation screen, and it makes no character. A catalog with no
    // such list (a file of an earlier tool) gives no item.
    const gamedata::ItemCatalog* catalog = services.world.options().itemCatalog;
    const bool creationKnown = catalog != nullptr && catalog->loaded() && catalog->has_creation_backstories();
    if (creationKnown && !character.archetype.empty() && !catalog->creation_backstory(character.archetype)) {
        // The text of the backstory is a text of the request: it is not logged.
        log::write("gateway: no character %s for account %s: the backstory of the creation data (%zu bytes) is not a "
                   "backstory of the character creation",
                   character.name.c_str(),
                   account->personaId.c_str(),
                   character.archetype.size());
        return gateway_error(400, kErrorCreateFailed, "Backstory is not permitted");
    }
    std::vector<state::ItemRow> startItems;
    if (creationKnown) {
        startItems = world::start_items(*catalog, character.archetype);
    }
    // Stage 1.8a (docs/systems/quests.md): the start of the quests of a new character, in the transaction of the character. The quest data
    // is a read-only object that the backend loads at its start, thus this thread can read it. The start objective of the start territory
    // (TerritoryDefinition.ObjectiveID of Windsward, decision D1 of the user) as a pending one-time grant for the first world entry, the
    // start position on its beach, and the achievement of the archetype (AchievementUnlockOverride of the backstory row). No quest data:
    // nothing (the character starts as before).
    std::optional<state::WorldPosition> startPosition;
    const state::NewCharacterRows questRows =
        world::Quests::new_character_rows(services.world.options().questData, character.archetype, startPosition);
    if (startPosition) {
        character.position = startPosition;
    }
    switch (services.store.insert_character(character, startItems, questRows)) {
    case state::InsertResult::inserted:
        break;
    case state::InsertResult::nameInUse:
        log::write("gateway: no character %s for account %s: the name is in use",
                   character.name.c_str(),
                   account->personaId.c_str());
        return gateway_error(409, kErrorNameInUse, "Name is in use");
    case state::InsertResult::failed:
        log::write("gateway: no character %s for account %s: the database failed",
                   character.name.c_str(),
                   account->personaId.c_str());
        return gateway_error(400, kErrorCreateFailed, "Character was not created");
    }
    if (!questRows.grants.empty() || !questRows.achievements.empty()) {
        log::write("gateway: character %s starts on the beach of territory %u with the pending start objective %s and %zu archetype "
                   "achievement(s)",
                   character.characterId.c_str(), static_cast<unsigned>(world::kStartTerritory),
                   questRows.grants.empty() ? "(none)" : questRows.grants.front().objective.c_str(), questRows.achievements.size());
    }
    log::write("gateway: created character %s (%s) for account %s, archetype %s, creation data %zu bytes, %zu start "
               "item(s)%s",
               character.name.c_str(),
               character.characterId.c_str(),
               account->personaId.c_str(),
               character.archetype.empty() ? "(none)" : character.archetype.c_str(),
               character.creationParams.size(),
               startItems.size(),
               startItems.empty() ? " (the item catalog has no items for this archetype: tools/gamedata/items.sh makes them)" : "");
    return json_reply(200, json{{"Character", character_json(services, character)}}.dump());
}

/**
 * DELETE /prod/game/characters/{character}: only a character of the account of the request.
 * The client reads no body of a 2xx reply (job at image RVA 0x64148E0).
 * A character that is in the world: the world ends its session (the client of that session
 * gets the message "kicked") and removes its entity, and then the row goes away. The two
 * steps are one task of the world, so that no session registers the character between them.
 */
service::HttpResponse delete_character(const Services& services, const RouteContext& context) {
    const auto account = request_account(services, context.request);
    if (!account) {
        return unauthorized();
    }
    const std::string characterId = context.parameters.at("character");
    const auto character = services.store.find_character(characterId);
    if (!character || character->personaId != account->personaId) {
        log::write("gateway: account %s has no character %s: nothing deleted",
                   account->personaId.c_str(),
                   characterId.c_str());
        return gateway_error(404, kErrorUnknownCharacter, "Character not found");
    }
    state::Store& store = services.store;
    world::CallStatus status = world::CallStatus::done;
    const auto deleted = services.world.call<state::DeleteResult>(
        [&store, characterId](world::World& target) {
            const bool inWorld = target.remove_character(characterId, world::LostCause::deleted);
            if (inWorld) {
                log::write("gateway: character %s was in the world: its entity was removed before the delete",
                           characterId.c_str());
            }
            const state::DeleteResult result = store.delete_character(characterId);
            if (result == state::DeleteResult::failed) {
                log::write("gateway: the database did not accept the delete of character %s: its rows are there%s",
                           characterId.c_str(),
                           inWorld ? ", and its entity is not in the world any more" : "");
            }
            return result;
        },
        kWorldCallLimit,
        &status);
    if (status == world::CallStatus::notStarted) {
        // The world cancelled the task: it does not run later, and the character is there.
        // A 4xx status: the client can send a request with a 5xx reply again.
        log::write("gateway: the world did not start the delete of character %s: nothing deleted", characterId.c_str());
        return gateway_error(409, kErrorUnknownCharacter, "Character was not deleted");
    }
    if (!deleted) {
        // The task started and stopped with an error: this reply does not say "no change".
        log::write("gateway: the delete of character %s stopped with an error: the state of the character is not known",
                   characterId.c_str());
        return gateway_error(500, kErrorUnknownCharacter, "The delete of the character did not complete");
    }
    if (*deleted == state::DeleteResult::failed) {
        // The world part of the delete is done (a session of the character got the message
        // "kicked"), and the rows stay: the delete is not complete. This reply does not say
        // that the character is not there.
        return gateway_error(500, kErrorUnknownCharacter, "The delete of the character did not complete");
    }
    if (*deleted == state::DeleteResult::notFound) {
        log::write("gateway: account %s has no character %s: nothing deleted",
                   account->personaId.c_str(),
                   characterId.c_str());
        return gateway_error(404, kErrorUnknownCharacter, "Character not found");
    }
    log::write("gateway: deleted character %s (%s) of account %s",
               character->name.c_str(),
               characterId.c_str(),
               account->personaId.c_str());
    return json_reply(200, "{}");
}

json login_token(const Services& services,
                 const state::Account& account,
                 const state::Character& character,
                 const std::string& ticketId,
                 int tokenVersion) {
    const std::int64_t now = now_milliseconds();
    return {
        {"TokenVersion", tokenVersion},
        {"TicketId", ticketId},
        {"PersonaId", account.personaId},
        {"CharacterId", character.characterId},
        {"WorldId", character.worldId},
        {"ChannelId", "STEAM_APP_ID.1063730"},
        {"SteamAppId", 1063730},
        {"SteamUserId", account.steamId},
        {"ClientCapabilities", ""},
        {"RepAddress", services.repAddress},
        {"HostHash", ""},
        {"LocationGroupId", ""},
        {"LocationId", ""},
        {"AccountAge", 365},
        {"AccountIsLocked", false},
        {"IsPermanentAppOwner", true},
        {"IsTrialOwner", false},
        {"IssueTime", now},
        {"GenerateTime", now},
        {"JwtClaims", ""},
        {"Signature", "soulwarden-local-signature"},
    };
}

/** POST /prod/game/login/queue/v2: the local world has no queue, so the token is immediate. */
service::HttpResponse queue_login(const Services& services, const RouteContext& context) {
    const auto account = request_account(services, context.request);
    if (!account) {
        return unauthorized();
    }
    const json body = request_body(context.request);
    const std::string characterId = text_member(body, "CharacterId");
    const auto character = services.store.find_character(characterId);
    if (!character || character->personaId != account->personaId) {
        log::write("gateway: account %s has no character %s: no login token",
                   account->personaId.c_str(),
                   characterId.c_str());
        return gateway_error(404, kErrorUnknownCharacter, "Character not found");
    }
    const json reply = {
        {"LoginQueueResponse",
         {
             {"AllowQueueTransfer", false},
             {"EstimatedTime", 0},
             {"QueueName", ""},
             {"RecommendedTransferWorldId", ""},
             {"RefreshInterval", 5},
             {"Token",
              login_token(services,
                          *account,
                          *character,
                          state::new_uuid(),
                          query_integer(context.request, "tokenVersion", 10))},
         }},
    };
    log::write("gateway: login token for character %s (%s), account %s, Steam id %s",
               character->name.c_str(),
               character->characterId.c_str(),
               account->personaId.c_str(),
               state::masked_steam_id(account->steamId).c_str());
    // The request has the ticket of the client again. Stub authentication: a ticket for a
    // different Steam id is only reported.
    const auto ticket = steam::read_local_ticket(text_member(body, "SteamAuthTicket"));
    if (ticket && ticket->steamId != account->steamId) {
        log::write("gateway: the ticket of the login request names Steam id %s, not the Steam id of the account",
                   state::masked_steam_id(ticket->steamId).c_str());
    }
    return json_reply(200, reply.dump());
}

} // namespace

void register_gateway_routes(Router& router, const Services& services) {
    const auto bind = [&services](service::HttpResponse (*handler)(const Services&, const RouteContext&)) {
        return [&services, handler](const RouteContext& context) { return handler(services, context); };
    };
    router.add("GET", "gateway-cf", "/prod/credentials/omni", "gateway.omni_credentials", omni_credentials);
    for (const char* suffix : {"", "/jwt", "/jwt/omni"}) {
        const std::string tail = suffix;
        router.add("GET", "gateway", "/prod/game/getlogininfo" + tail, "gateway.get_login_info", bind(get_login_info));
        router.add("GET", "gateway", "/prod/game/worlds/list" + tail, "gateway.list_worlds", bind(list_worlds));
        router.add("POST", "gateway", "/prod/game/worlds/{world}/characters/validator" + tail,
                   "gateway.validate_character", bind(validate_character));
        router.add("POST", "gateway", "/prod/game/worlds/{world}/characters" + tail,
                   "gateway.create_character", bind(create_character));
        router.add("DELETE", "gateway", "/prod/game/characters/{character}" + tail,
                   "gateway.delete_character", bind(delete_character));
        router.add("POST", "gateway", "/prod/game/login/queue/v2" + tail, "gateway.queue_login", bind(queue_login));
    }
}

} // namespace soulwarden::backend::server::http
