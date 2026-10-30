# New World Protocol Notes

Target: `NewWorld.exe` build `1.400.6031.6004151` (Steam build 22469132),
SHA-256 `8654f01d324636d9f74f1c793b0cc4a417c3c5fa9847d9913c358ca29e0fdc8e`.
All addresses are image-relative (RVA). Evidence level is given for each contract.

## 1. Client start sequence (behavioral proof)

```text
SteamAPI_Init (hook DLL)
-> GET  <channel host>/STEAM_APP_ID.1063730.json            channel document
-> POST tokenservice.amazongames.com /games/new-world/tokens account session (2.1)
-> GET  <JavelinGatewayService-CF>/prod/credentials/omni      signing credentials
-> title screen, first-run screens, main menu
-> GET  /prod/game/getlogininfo                               worlds and characters
-> empty row of the character list: character creation (2.2), then the login of the
   new character
-> Play: POST /prod/game/login/queue/v2                       login token with RepAddress
-> DTLS 1.2 to RepAddress (UDP), carrier handshake, registration
-> self identification            state WaitingForSpawnPoint
-> level information, spawn point notice, first state bundle: level load
                                  state WaitingForPlayerSpawn
-> state record of the local player (interest 1)
                                  state InGame, loading screen
-> player position in the movement state: world streaming, loading screen ends
-> a movement state each 200 ms keeps the session open and the input of the client on
<- input reports of the client (30 each second): the client moves its character itself
```

Client state names (string table at 0x84FA000): `QueryGameUpdateCheck`,
`WaitingForGameUpdateCheck`, `QueueGameLogin`, `WaitingForQueuedLogin`,
`QueryForRemoteConfigClass`, `WaitingForRemoteConfigClass`, `ObtainREPRequirements`,
`WaitingForREPRequirements`, `StartREPConnection`, `WaitingForREPConnection`,
`WaitingForActorGameConnection`, `WaitingForSpawnPoint`, `WaitingForPlayerSpawn`, `InGame`.

## 2. HTTP services (behavioral proof unless noted)

| Service | Route | Reply contract |
| --- | --- | --- |
| Channel | `GET /STEAM_APP_ID.<app>.json` | Region objects with `publicApis[]` (`clientTag`: `authStack`, `loginGateway`, `JavelinGatewayServiceV2`, `JavelinGatewayService-CF`) and `platformMetadata` |
| Account token | `POST /games/{game}/tokens` | `accessToken`, `expiresIn`, `fallbackToken`, `region`, `isNewAccount`, `account{identityId,personaId,type,ageGroup}`, `platformAccount{identityId,personaId,identityType,platform,ageGroup}` (reader 0x79B6D00). The backend sends `isNewAccount` false for a new account too: the value true was not tested. |
| Credentials | `GET /prod/credentials/omni` | `accessKeyId`, `secretAccessKey`, `sessionToken`, `expiration` (Unix ms; reader 0x5A955E0) |
| Login info | `GET /prod/game/getlogininfo` | `{"LoginInfoList":{Characters,Worlds,NameReservations,PendingWorldMerges,MaxChannelCharacters,CrossRegionTransferCooldownMins}}` (reader 0x74E42E0). A character with `PublishedData` (2.2.5) has its body in the menu. |
| World list | `GET /prod/game/worlds/list` | `{"ListWorldsResponseBody":{Worlds,RecommendedWorlds}}` |
| Name check | `POST /prod/game/worlds/{world}/characters/validator` | Status only (2.2.2) |
| Create character | `POST /prod/game/worlds/{world}/characters` | `{"Character":{CharacterId,...}}` (2.2.3) |
| Delete character | `DELETE /prod/game/characters/{character}` | Status only (2.2.6) |
| Login queue | `POST /prod/game/login/queue/v2?tokenVersion=10` | Request `{"LoginQueueRequest":{CharacterId,...}}`; reply `{"LoginQueueResponse":{Token:{RepAddress,...},...}}` (reader 0x74E4F20, 0x74E5990) |
| Entitlement sync | `POST /players/{persona}/games/new-world/platforms/steam/entitlements/sync`, host `client.entitlementservice.amazongames.com` | Request `{"syncTypes":["FirstTimeLogin","PrimeGamingSync","CodeRedemptionSync","TwitchDropsSync","SteamRetailSync"]}` (builder 0x74D5100); reply `{}` (4.29) |
| Entitlement list | `GET /players/{persona}/games/new-world/platforms/steam/entitlements`, same host | `{"entitlements":[{"alias":TEXT,"amount":{"freeAmount","paidAmount","pendingAmount","lossAmount","totalAmount"}}],"hasMoreResults":false}` (parsers 0x74C2770, 0x74C2E60, 0x74C15D0; 4.29) |
| Remote configuration | `GET /applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.<app>/versionless`, host `ags-javelin-remote-config.s3.amazonaws.com` | The document `{"javelin.enable-client-authoritative-melee":true,"javelin.enable-dungeon-group-restriction":false}` (2.3; the second setting from stage DG3). Each other object of a host with `.s3.` in its name: status 404, `NoSuchKey` |

World checks in the client (0x6423730): `WorldStatus == "ACTIVE"`, `WorldType` is `OpenWorld` or
`FTUE` (0x17C40A0), `WorldVersion` must contain the client version text, population text is
`low`, `medium`, or `high`.

Model field types come from `tools/aws_model.py READER_RVA`.

### 2.1 Player identity (behavioral proof unless noted)

The client gets its identity from three Steam functions of the hook. The addresses of the
client code are static proof.

| Steam function | Interface slot | Client use |
| --- | --- | --- |
| `ISteamUser::GetSteamID` | User 2 | Decimal text of the Steam id: the header `x-amzn-playerid`, and the client data of the registration request (3.4). Callers 0x5A7F3E0, 0x6429EC0, 0x6459C10. |
| `ISteamFriends::GetPersonaName` | Friends 0 | UTF-8 text. Caller 0x5A7F3E0. |
| `ISteamUser::GetAuthTicketForWebApi` | User 14 | One call with a null identity (0x5A9FBE0). The ticket comes in callback 168 (handler 0x5A936F0): `handle:u32 result:i32 size:i32 ticket:2560 bytes`. |

The client copies `size` bytes of the ticket (object +0x38C) and makes lower-case hexadecimal
text from them (0x5A871A0). It sends this one ticket three times, and no request has a field
for the Steam id:

| Step | Request | Ticket and identity in the request | Identity in the reply |
| --- | --- | --- | --- |
| 1 | `POST /games/new-world/tokens` | `{"platformIdentityType":"steam","platformAuth":{"ticket":HEX},"fallbackToken":...,"attributionData":{...}}` | `accessToken`, `account.identityId`, `account.personaId`, `platformAccount.identityId` (Steam id) |
| 2 | `GET /prod/credentials/omni` | `authorization: Bearer <accessToken>` | none |
| 3 | `/players/{personaId}/games/new-world/...` (entitlements, catalog, privacy settings) | `x-amzn-token: <accessToken>`, `x-amzn-playerid: <Steam id of the client>` | no route in the backend (404) |
| 4 | `GET /prod/game/getlogininfo` | `x-nw-auth: <accessToken>` | `Characters[].PersonaId` |
| 5 | `POST /prod/game/login/queue/v2` | `x-nw-auth`, body `CharacterId`, `SteamAuthTicket: HEX` | `Token.PersonaId`, `Token.SteamUserId` |
| 6 | `RegistrationRequestV3Msg` (3.4) | The fields of `Token`, the Steam id of the client, the text `steam\|HEX` | result 0 |

- `account.personaId` is the key of the player in the client: it is in the service paths of
  step 3, in the game log (`Omni CreateSession complete with result: 0, id: <personaId>`), and
  in the name of the settings file (below).
- The client keeps the access token as opaque text and sends it back in the three headers of
  steps 2 to 5.
- The client does not compare `platformAccount.identityId` or `Token.SteamUserId` with its own
  Steam id: world entry worked when the three values were different (runs before the account
  support).
- Ticket size: the client sends tickets of 49 to 295 bytes in step 1, and tickets of 64 and
  295 bytes passed steps 5 and 6 (world entry). A larger ticket was not tested.
- A persona name with characters that are not ASCII (14 bytes, and 128 bytes of 4-byte
  characters) goes from the launcher through the environment of Wine to the hook and to the
  backend with no change.

Identity contract of the project:

```text
launcher   reads the Steam id and the persona name of the last Steam login from
           <Steam>/config/loginusers.vdf and gives them to the hook in the environment:
           SOULWARDEN_STEAM_ID (17 decimal digits), SOULWARDEN_PERSONA_NAME (UTF-8 text)
hook       GetSteamID := SOULWARDEN_STEAM_ID, GetPersonaName := SOULWARDEN_PERSONA_NAME
           ticket := "SOULWARDEN-LOCAL-TICKET" ";" "1" ";" steam-id ";" persona-hex
                     steam-id    := 17 decimal digits, high 32 bits 0x01100001 (individual
                                    account, public universe)
                     persona-hex := lower-case hexadecimal text of the UTF-8 persona name
                                    (128 bytes at most)
backend    step 1: ticket -> Steam id -> account row (found, or made) -> persona id
                   access token := unsigned JSON Web Token with the claim personaId
           step 4, 5: x-nw-auth -> claim personaId -> account -> characters of that persona
           step 6: persona id, the two Steam ids, and the ticket are compared with the account
                   and the result goes to the log
```

- **Stub authentication.** The ticket is local text and not a Steam credential. The backend
  does not validate it, the access token has no signature, and a difference in step 5 or 6
  is only written to the log. This is acceptable only because the backend listens on loopback.
- With no Steam account (no `loginusers.vdf`, or `SOULWARDEN_STEAM_ID=local`) the hook uses the
  fallback identity: Steam id `0x0110000100A207A1` and the name `Soulwarden`. A token request with
  no local ticket gets the account of the same Steam id.
- A database from before the account support has one persona with a constant id. The first
  Steam id that logs in with a local ticket takes it (behavioral proof: world entry at the
  position that the database had). Each later Steam id gets a new account with random ids.
- A token request with no local ticket of version 1 does not take that persona. It gets the
  fallback account, as a new account with no character when the database has none. Such a
  request comes from a hook from before the account support (its ticket is 64 bytes of the
  marker text with no separator and no Steam id) or from a tool. Proof: requests to the
  backend alone on a copy of a version 2 database, not a run with the old hook.
- A gateway request with an access token that names no account gets status 401. The reaction
  of the client is not known (it does not occur in a normal session).

Settings file of the client: `@savedata@/usersettings_<hash>.javsave` (function 0x4591D00,
format `%s/%s%s_%llx.%s`). `<hash>` is the FNV-1a 64 hash of the channel id and the persona id
as one text (`STEAM_APP_ID.1063730` + persona id; proved for two personas). A persona with no
settings file gets five first-run screens before the main menu: narration, seizure warning,
basic settings, brightness, code of conduct (game log: `Failed to find Options file`).

### 2.2 Character creation (behavioral proof unless noted)

Proof: 6 creations in the live client (3 of them by `tools/scenarios/create.sh` alone), with
the HTTP captures of the requests, and 2 later creations by the script with the archetype
buttons 5 and 7. A function address with "trace" had a hit of `SOULWARDEN_TRACE` at that step. The
other addresses are static proof.

#### 2.2.1 Flow

```text
main menu, click on an empty row ("Create Character") of the character list
-> popup "Choose world type" with one panel ("Standard"), click on the panel
-> popup "Create character", "Yes"
-> console command "map FTUE_V2_CC" (0x41FAD90, trace): the client loads the creation level
   game log: Loading level FTUE_V2_CC
-> intro film (more than 80 s). A key shows "Hold ESC to skip". Escape for 3 s skips it.
-> camera sequence (less than 25 s), then the buttons of step 1 show
-> step 1 Archetype (7 buttons), step 2 Appearance, step 3 Identity (name), step 4 World
   step 3: POST .../characters/validator, 2 s after the last key (2.2.2)
   step 4: GET /prod/game/getlogininfo again, and the world content requests (404)
   "Continue" of step 4: POST .../characters (2.2.3)
-> end sequence (4.5 s), then the client logs in with the new character:
   RequestLogin(CharacterId of the reply, "FTUE") (0x2EF9BB0 and 0x6462B20, trace with
   the two texts)
   game log: Queuing login for world () and character (<new id>)
-> POST /prod/game/login/queue/v2 and the world entry of section 1
```

- The two popups come for an account with no character and for an account that has
  characters.
- No request goes to the backend before step 3. The archetype and the appearance are only in
  the creation data of the create request.
- Escape in step 3 goes back to step 2 (the first Escape only takes the focus from the name
  field). The other steps were not tested.
- The login of the new character is the same request as the login from the menu (`WorldId`
  is empty, `Queuing login for world ()`). The client enters the open world from the
  creation level with no return to the main menu. The world type text `FTUE` has no effect:
  the console variable `g_loginWithWorldTypes` is 0 (static proof, 0x6441C70).
- The requests of this flow have the query `channelId=STEAM_APP_ID.1063730` and the header
  `x-nw-auth` (2.1). A request with a body has `content-type: application/x-amz-json-1.1`.
  Each route also has the
  path forms `<path>/jwt` and `<path>/jwt/omni` (static proof, selector 0x646FCA0; the
  captures have only the plain form).

#### 2.2.2 Name check

```text
POST /prod/game/worlds/{worldId}/characters/validator?channelId=STEAM_APP_ID.1063730
{"ValidateCharacterBody":{"Name":"<name>"}}
```

Client code: job 0x6419EF0, body writer 0x74ED0D0, result callback 0x2EAB900 (trace).

**Name rule of the client** (static proof; behavioral proof for the results 3 and 5: a name
with 7 digits and a name with two spaces in sequence got a text of the client below the
field and no request). The client checks the name before it sends the request: 0x41F6530
calls the text check 0x5CF59A0 with the limits 3 and 15 and the pattern text at 0x8159340. A
name that the rule refuses gets no request. The check gives one of these results (the
script `namevalidationcommon` shows a text for each):

| Result | Rule |
| --- | --- |
| 1, too short; 2, too long | The name has 3 to 15 characters (UTF-16 units). |
| 5, multiple spaces | No two spaces in sequence. |
| 3, invalid characters | No white space at the start or at the end. Each character is in the pattern: the space, `'`, `,`, `-`, `.`, the digits, the ASCII letters, and the ranges U+00C0 to U+00D6, U+00D8 to U+00F6, U+00F8 to U+0108, U+010A to U+0124, U+0126 to U+0127, U+012A to U+012B, U+012D to U+0133, U+0136 to U+014D, U+0150 to U+0167, U+0169 to U+016B, U+016D to U+0174, U+0176 to U+017E. The pattern refuses a name with 7 digits. |
| 4, bad words | A check of the words of the name (call at 0x5CF5DAB, not read). The backend does not have this rule. |

| Reply | Result in the client |
| --- | --- |
| `200` with `{}` | Valid state: green mark in the name field, "Continue" is on. The client does not read the body (static proof). |
| `400`, header `x-amzn-ErrorType: NameNotAvailableError`, body `{"__type":"NameNotAvailableError","message":"..."}` | The text "This name is in use." below the field, "Continue" is off, no popup. |

The backend applies the name rule again (`check_name` in `state/character_name.h`; all of the
rule but the word list). A name with a length that is not permitted gets `400`
`NameLengthError`, and a name with a character, a number of digits, or a space position that
is not permitted gets `400` `NameValidationError`. The client has a text for each of the two
names (`mm_loginservices_NameLengthError` "Invalid name length.",
`mm_loginservices_NameValidationError` "This name contains invalid characters."), and its
callback shows them as the state of the name field (static proof, 0x2EAB900). Proof of the
backend rule: requests to the backend alone. The client does not send such a name. A name
of the client with 15 characters, a hyphen, a space, and an apostrophe got the valid state
and a character (behavioral proof, 1 creation).

The backend gives the reply `NameNotAvailableError` for a name that a character of any
account has. The compare ignores the letter case of each letter that the client permits: the
backend makes a compare form in lower case for the letters up to U+017F
(`state/character_name.h`), because `COLLATE NOCASE` of SQLite changes the case of ASCII
letters only. Proof: requests to the backend alone (`Élan` and `élan`, `Žofie` and `žofie`
are one name; `Elan` is a different name). The rule of the game service for these letters is
not known.

#### 2.2.3 Create

```text
POST /prod/game/worlds/{worldId}/characters?channelId=STEAM_APP_ID.1063730
{"CreateCharacterRequest":{"CharacterCreationParams":"<text, 2.2.5>","ClientCapabilities":[],
 "Name":"<name>","SkipFtue":false,"SteamAppId":1063730,"SteamAuthTicket":"<hexadecimal text>"}}
```

Client code: 0x2EB0FE0 (trace; builds the creation data), gateway call 0x642B130 (trace),
job 0x6414350, body writer 0x74ECF20 and 0x74EC2C0, reply reader 0x74E1090 and 0x74E1F60,
result callback 0x2EAAEC0 (trace), then 0x2EF9740 (trace; starts the end sequence).

- `SteamAuthTicket` is the ticket of 2.1 again. `SkipFtue` was false in each capture (5
  archetypes). `ClientCapabilities` was an empty list.
- Reply: `200` with `{"Character":{...}}`, the same members as a character of the login
  information. The client reads only `Character.CharacterId` (static proof) and logs in with
  it: the id of the game log line was the id of the reply in each run.
- The backend makes the row for the account of the token (`x-nw-auth`), keeps
  `CharacterCreationParams` with no change, and keeps the archetype (member `backstory` of
  the creation data). It sends `FtueCompleted` true: it has no tutorial world, and the
  character starts in the open world. It does not keep `SkipFtue`.
- The backend makes no row for a request that does not have the form of the client: a name
  that the name rule refuses (2.2.2), or creation data that it cannot read (2.2.5: base64,
  zlib, the document with `visualsBlob`, and an object stream with the root class
  `PlayerSaveData`). The reply is `400` `CreateCharacterFailed`, and the backend log has a
  line `gateway: no character ...` with the cause. Proof: requests to the backend alone
  (8 names and 9 texts that are not creation data got no row), and the creation data of
  each of the 10 different create requests in the captures of the client got a row.
- The client has a limit of 10 seconds for the reply (static proof, text
  `@mm_connerr_createcharacter_timeout`).

#### 2.2.4 Error reply of a gateway route

```text
HTTP status 4xx
x-amzn-ErrorType: <error name>
{"__type":"<error name>","message":"<text>"}
```

The client reads the error name from the header, else from the member `__type` (AWS JSON
error reader 0x7481A00, static proof) and shows the text of the localization key
`@mm_loginservices_<error name>` (0x16C14D0, 0x5C4CC10). The backend sends the two forms in
each error reply of the table. A reply with an error name that the route does not expect, or
with no error name, opens the popup "connection failed" (static proof, 0x2EAB900).

| Route | Case | Status and error name | Proof |
| --- | --- | --- | --- |
| Name check | Name in use | 400 `NameNotAvailableError` | behavioral |
| Name check | The name has less than 3 or more than 15 characters (also no name) | 400 `NameLengthError` | static (the client sends no such name), and a request to the backend alone |
| Name check | The name has a character, a number of digits, or a space position that the name rule refuses | 400 `NameValidationError` | static (the client sends no such name), and a request to the backend alone |
| Create | Name in use | 409 `ALREADY_EXISTS` | static, and a request to the backend alone |
| Create | The account has 4 characters | 409 `TooManyCharactersError` | static |
| Create | World id is not the local world | 404 `WorldNotEligibleForCharacterCreation` | static |
| Create | The name rule refuses the name (also no name) | 400 `CreateCharacterFailed` | static, and a request to the backend alone |
| Create | The backend cannot read the creation data (also no creation data) | 400 `CreateCharacterFailed` | static, and a request to the backend alone |
| Create | The database did not accept the row (not a name conflict) | 400 `CreateCharacterFailed` | request to the backend alone, with the database locked |
| Delete, login queue | The account has no character with this id | 404 `UnknownCharacter` | request to the backend alone. The client has the text key `mm_loginservices_UnknownCharacter`. The reaction of the client to this reply was not seen. |

Three replies of the backend have no error name (no header `x-amzn-ErrorType`, no member
`__type`). They do not occur in a session of the client, and the reaction of the client is
not known:

| Case | Reply |
| --- | --- |
| The access token of a request names no account (2.1) | 401 `{"message":"Unauthorized"}` |
| The path has no handler | 404 `{"message":"soulwarden: route not implemented"}` |
| A handler stops with an exception | 400 `{"message":"Bad Request"}`, and a line in the backend log |

A request member with a type that the handler does not expect (a number for `Name`) is the
same as an absent member.

#### 2.2.5 Creation data (`CharacterCreationParams`, `PublishedData`)

```text
text        := base64( zlib( document ) )      standard alphabet with padding; zlib header 78 DA
document    := {"currency":0,"visualsBlob":"<bytes>","position":{"x":0.0,"y":0.0,"z":0.0},
                "currentLevel":0,"worldId":"<world id of the request>"}
visualsBlob := binary engine object stream, 1904 to 1921 bytes in the captures (the size
               changes with the length of the texts)
```

Client code: document writer 0x17A75B0 and reader 0x16B3850 (trace, at the load of the
character list), zlib and base64 writer 0x16A6030 and reader 0x171A500.

- **The document is not valid UTF-8 text.** The JSON writer of the client writes a byte of
  `visualsBlob` below 0x20 as an escape (`\u0000`, `\n`, ...), writes `"` and `\` with a
  backslash, and copies each other byte with no change. A JSON library that needs UTF-8
  rejects the document: read the member as bytes (`state/creation_data.cpp`,
  `tools/research/creation_params.py decode FILE`).
- Object stream (big-endian; `tools/research/objstream.py`): tag `00`, version `u32` = 3,
  then elements. An element is `flags:u8`, a name `u32` when flag 0x40 is set (CRC-32 of the
  field name in lower case), a class version `u8` when 0x80 is set, a type id of 16 bytes, a
  value when 0x10 is set, the elements of its fields, and the end byte `00`. The value size
  is the low 3 bits of the flags, or with flag 0x20 a number with that many bytes. Each
  element of the client has the flag 0x08. A text with no characters has the value flag and
  the size 0 (flags `58`). The stream ends with one more byte `00` after the root element.
- The root element has the type id `{144C7576-7FBA-4250-B0C8-F06803657D79}` and class
  version 3 (class `PlayerSaveData`, reflection 0x36FDFA0). Its fields in the captures:

  | Field | Type | Value in the captures |
  | --- | --- | --- |
  | `playerName` | text | The name of the request. |
  | `companyName` | text | `<name>'s Company` |
  | `entityClassName` | text | `PlayerMale` or `PlayerFemale` |
  | `guildCrestName` | text | empty |
  | `gender` | text | `Male` or `Female` |
  | `race` | text | The face, for example `face26`. |
  | `skinTone`, `hairstyle`, `facialHair`, `hairColor`, `facialHairColor`, `eyeColor`, `faceMark`, `scar`, `tattoo`, `tattooColor` | text | Names of the character creation databases: `skin10`, `hairstyle13m`, `None`, `haircolor26`, `eyecolor3`, `facemark02`, `none`, `tattoo58`, `tattoocolor15`. |
  | `backstory` | text | The archetype. Buttons 1 to 7: `Archetype_Soldier`, `Archetype_Destroyer`, `Archetype_Ranger`, `Archetype_Musketeer`, `Archetype_Occultist`, `Archetype_Mystic`, `Archetype_Swordsman`. |
  | `startingFaction` | int | 0 |
  | `appearanceData` | container | empty |
  | `equipment` | map | 5 entries with the keys `gloves`, `shirt`, `hat`, `boots`, `pants`. Each value is an `EquipmentSaveData`: `itemName` (empty), `appearanceId` (a 32-bit number), `dyeSettings` (4 numbers, all 0). This is the clothing of the archetype, also the head item that the backstory gives with the quantity 0. |

- **`PublishedData` of a character of the login information: the menu document (stage U4).** The main menu makes the
  body of a character from this text: the gender, the appearance names, and the map `equipment` (the clothing). The
  level number of the row is the member `currentLevel` plus 1 (behavioral proof of stage 1.5: the row shows "21"). A
  character with no `PublishedData` has a white body in the menu. The original server made the text again from the
  character at each publish (static proof: the publish function 0x67F0600 of `PlayerComponent`, text
  "PublishCharacterMetadata failed to serialize PlayerSaveData!" 0x8558768, calls
  `PaperdollComponent::WriteEquipmentToPlayerSaveData`, RTTI text 0xA254730, loop 0x684E9B0, entry lambda 0x673EE40). From
  stage U4 the backend makes the text at each request of the character list with one writer (`make_menu_document` in
  `state/creation_data.cpp`) from the values of the character: the name (`playerName`, and `<name>'s Company`), the 12
  appearance names and the backstory of the stored creation data (a character with no creation data: the names of the
  default appearance of 4.5 and an empty backstory), the world id, `currentLevel` = the level of the row minus 1, and the
  map `equipment` from the paperdoll rows of the store. The other fields have the values of the table above. The zlib
  stream has stored blocks (header `78 01`); the client accepts it (behavioral proof from the seeded characters of the
  stages before U4: the menu showed the body with skin, face, and hair). The stored `CharacterCreationParams` does not change. Without the appearance
  tables a character with no creation data gets no `PublishedData`.
- **The map `equipment` (stage U4).** One entry for each armor item of the paperdoll slots 0 to 4, in the sequence of the
  slots. Evidence: static proof (0x673EE40), behavioral proof (5 create requests), live proof of stage U4 (the menu shows
  the armor; pictures of the result of stage U4).

  | Key | Paperdoll slot | Evidence |
  | --- | --- | --- |
  | `hat` | 0 head | static (0x673EE40 finds the visual slot name in the map at paperdoll component +0x23F8; the text `hat` 0x831C8F0 also sets the hair rule in 0x6956C40), game data (`m_paperdollVisualSlotMapping` of `slices/player.dynamicslice`), behavioral (5 of 5 create requests) |
  | `shirt` | 1 chest | the same |
  | `gloves` | 2 hands | the same |
  | `pants` | 3 legs | the same |
  | `boots` | 4 feet | the same |

  - The original server writes no entry for a slot above 4 (static proof, 0x673EE40): the menu has no weapon, shield,
    jewelry, paint, or tool. The backend writes no entry for an item with no look (an item with no class tag `Armor`, or
    an empty name; a decision of an agent that the orchestrator took: the original server wrote the value 0, and the
    reaction of the menu to 0 is not known).
  - `appearanceId` (`AZ::Crc32`, reflection 0x36FC490) = the CRC-32 (zlib polynomial) of the lower-case text of the
    cell `ArmorAppearanceM` (a male body) or `ArmorAppearanceF` (a female body) of the master item row. Static proof:
    0x673EE40 takes definition +0xDC when the gender text of the save data is `Male` (text 0x7FC5ED8), else +0xE0, for
    an item with the class tag 124 `armor` (bit test 0x5C5E550); the create function 0x2EB0FE0 uses the same fields.
    Behavioral proof: 25 of 25 entries of the 5 create requests (Ranger, Soldier, and Musketeer male; Mystic and
    Destroyer female) are equal to the rule (`m_Archetype_Ranger_gloves`: `4A435DB3`). The backend selects F only for
    the gender text `Female`. The loader of the fields +0xDC and +0xE0 was not found (inference: the two columns).
  - `itemName` is empty, and the 4 dye bytes are 0: each writer of the client (0x2EB0FE0) and of the original server
    (0x673EE40) leaves the item id empty. The original server writes the 4 dye bytes of the item (item +0x170), and a
    transmog appearance in place of the row when the item has one (item +0x218, setting `javelin.enable-transmog`): the
    backend has no dye and no transmog before stage 5.6.
  - The bytes of one entry (the entry `gloves` of a create request, 258 bytes; the key byte `5E` is `0x58 | length`):

    ```text
    48 41405E39 {EFF030FB-3358-5613-8740-93041334C206}            element: one pair
      5E A2756C5A {03AAAB3F-5C47-5A66-9EBC-D5FA4DB353C9} "gloves" 00     value1: the key, a text
      C8 3B7C3DE0 01 {903C9703-A791-454D-A576-45EDE8405B70}     value2: EquipmentSaveData, class version 1
        58 F45D1CCE {03AAAB3F-5C47-5A66-9EBC-D5FA4DB353C9} 00   itemName: an empty text
        48 CDCEA5F5 {9F4E062E-06A0-46D4-85DF-E0DA96467D3A}      appearanceId: AZ::Crc32
          5C 1D775834 {43DA906B-7DEF-4CA8-9790-854106D3F983} 4A 43 5D B3 00   value: 4 bytes, big-endian
        00
        C8 17454143 03 {2FA619C9-D71E-4E88-9BE9-16056F7E38FA}   dyeSettings: DyeData, class version 3
          59 51E3A083 {FAAD7488-08C8-5FCB-8FEE-AB1EBE4726B3} 00 00   m_rColorId (then m_gColorId 65B89FCC,
                                                                      m_bColorId 2D5891A8, m_aColorId A3D7964B)
        00
      00
    00
    ```

  - The client keeps the map as a hash map (insert 0x2EA46D0): the create requests have the order `gloves`, `shirt`,
    `hat`, `boots`, `pants`. The menu accepts the order of the slots (live proof of stage U4). With the values and the
    order of a create request the writer of the backend makes the document of the client byte for byte (5 of 5).
  - Not found: the loop of the client that reads the map in the menu (after 0x41F9E20 keeps the save data at component
    +0x1B8); the apply functions 0x6956C40 (slot name and appearance name) and 0x6957C00 (slot name, appearance row, and
    dye) of `CustomizableCharacterComponent` have no direct caller in the analysis. The reaction of the menu to an
    `itemName` with a value, to `appearanceId` 0, to a CRC that is not a row, and to a different key is not known.
- The appearance state of the world (4.5) has list indexes, not names. Index = position of
  the name in a list of the character creation databases of the game: gender in `Male`,
  `Female`; `race` in the list `Races` of the gender; skin tone, hairstyle, facial hair, and
  the two hair colors in the lists of the race; the other fields in one list each. The
  lists are game data: `tools/gamedata/appearance.sh` writes them to
  `~/.local/share/soulwarden/gamedata/appearance-tables.json` and the backend reads the file at
  its start (`--gamedata-dir`). With no file the backend uses the default appearance of 4.5
  and writes a log line. Proof: 2 characters had the body type, the skin tone, the hairstyle,
  and the hair color of the creation screen in the world (a view from the front), and a
  third one its body type and skin tone.

#### 2.2.6 Delete

Menu: the button `...` of a character row, "Delete Character", then a popup with "Confirm".
"Confirm" acts only after a long press of the pointer button (3 s in the test).

```text
DELETE /prod/game/characters/{characterId}        no query, no body
```

Client code (static proof): 0x41ED2E0, 0x642C420, 0x642C970, job 0x64148E0. Reply: `200`
with `{}`. The client then requests the login information again and the list has one row
less. The backend removes the row only when the character is a character of the account of
the token. The name is free again at once.

### 2.3 Remote configuration documents of the client (static proof and behavioral proof)

Research file: `~/Games/soulwarden/work/research/melee-mode.md`. Evidence of the live runs of stage
3.2a: `~/Games/soulwarden/work/evidence/hit/implement/` (files `H1-*`).

- The client reads settings from JSON documents of an S3 bucket (`[StackConfig]` in the game
  log). It asks for 16 documents in one start with a world entry, each one with a `GET`
  request to the host `ags-javelin-remote-config.s3.amazonaws.com` (the hook sends each
  request to the backend). The path is `/applications/CLASS/configuration-sets/SCOPE/ID/
  VERSION`: the class is `public` or `publicGameplay`, the scope is `ProductId` (the id
  `STEAM_APP_ID.1063730`), `RegionId`, `CognitoId`, or `WorldId`, and the version is
  `versionless` or the client version (`6031%3B6004151`). At its start, before the main menu,
  the client asks for the 12 documents of the first three scopes. At each world entry it asks
  for the 4 documents of the scope `WorldId` (state `QueryForRemoteConfigClass` of the game
  log). After a leave to the main menu it asks for the first 12 documents again (behavioral
  proof: one run with two world entries had 32 requests).
- **The reply of the backend.** The request for the path
  `/applications/publicGameplay/configuration-sets/ProductId/STEAM_APP_ID.1063730/versionless`
  of that host gets the status 200, the type `application/json`, and the body
  `{"javelin.enable-client-authoritative-melee":true}` (50 bytes), each time (stage DG3: the body
  `{"javelin.enable-client-authoritative-melee":true,"javelin.enable-dungeon-group-restriction":false}`, 99 bytes; section 4.67). Each other
  request to that host, and each request for an object of a different S3 host, keeps the
  reply 404 with the XML code `NoSuchKey` (`server/http/aws_routes.cpp`). The compare of the
  host is exact (with no port), and the route is for `GET` only. The compare of the path is
  a compare of the full text (`Router::add_exact`): a path with one more `/` at its end, at
  its start, or between two parts is a different object key and gets the reply 404 (case
  HT13; the usual routes of the backend compare path parts and ignore an empty part).
- What the reply must have (static proof): a status from 200 to 299 (0x7479588), the header
  `Content-Length` (the reader applies no body when the length of the result is 0; 0x853252),
  and a JSON object whose member name is the full name of a registered setting. The value is
  the JSON value `true`: the number 1 is not a true value for the reader of the client
  (0x83FBA0). The reply needs no `ETag` and no checksum.
- Which settings a document can change (static proof). The class `publicGameplay` has the
  mask 0 (0x89410): the apply function (0x8578A0) takes each registered setting whose flags
  are not 0, and it ignores a name that is not registered. The backend sends one setting.
- **The setting** `javelin.enable-client-authoritative-melee` has the default false
  (registration 0x6176A1C to 0x6176A3C). Its object is at image address 0x9F1F7C0 (the cache
  flag at +8, the value at +0xC). The initialization of each movement component (0x48423D0)
  copies the value to component +0x157D (at 0x4842590), and the getter 0x48403E0 reads only
  that copy: a change of the setting after the initialization does not change a component
  that is there.
- Live result (behavioral proof, memory reads with `tools/hx`; the copy is the byte of the
  movement component of the local player, and of the components of the creatures near it):

  | Start | Game log | Cache flag, value (0x9F1F7C8, 0x9F1F7CC) | Copy (+0x157D) |
  | --- | --- | --- | --- |
  | With the reply, world entry at once | `Retrieved layer's "remoteCfg.publicGameplay.ProductId" remote config, ... successfully, size: 50` | 1, 1 | 1 |
  | The same run, after a leave to the main menu and a second world entry | The same line a second time | 1, 1 | 1 (the new component) |
  | With the reply, in the main menu | The same line | 0, 0 (the client reads the setting at the first use) | no component |
  | With the reply, world entry after 95 s in the main menu | The same line | 1, 1 | 1 |
  | With no reply (`SOULWARDEN_MELEE_REPORT=0`) | no such line | 1, 0 | 0 |

  Thus the document arrives before the client makes the movement component of the player, in
  each start: the client asks for it before the main menu, and again when it comes back to
  the main menu.
- What the setting changes (behavioral proof): the client puts each melee contact that it
  finds into the third list of its input entry (4.7, 4.15). With the setting off, the flags
  of an entry of a client in the world have bit 32 (0x1020084F0 with the hook); with the
  setting on they do not have it (0x20084F0). The position field, the walk, the jump, the
  dodge, and the tool window are the same with the setting on.
- **The switch.** `SOULWARDEN_MELEE_REPORT=0` (launcher) gives the backend the option
  `--no-melee-report`: the request then gets the reply 404, the setting stays false, and a
  melee attack of a player has no effect. The backend log has one line for the condition at
  its start (`backend: the melee contact report of the client is on ...` or `is OFF ...`).
- Not tested: a document of a different scope or class with a value for the same setting
  (the order of the layers), a refresh of a layer by the message type 1639, and a different
  setting in the document.

## 3. Game server transport

### 3.1 Security

DTLS 1.2, cipher `TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384`. The client trusts one embedded
certificate authority (PEM at 0x8590460, pointer at 0x9F80D90). The hook replaces the pointer
with the local backend certificate before the transport starts (behavioral proof).

### 3.2 Carrier (GridMate variant, behavioral proof)

```text
datagram := flags:u8 version:u8 sequence:u16 message*      big-endian
            flags = 0x80 | compressed(bit 0), version = 0x01
            compressed: the messages are one LZ4 block
message  := flags:u8 size:u16 [channel:u8] [chunks:u16] [sequence:u16] [reliable:u16] data
            0x01 reliable, 0x04 chunks, 0x08 sequential, 0x10 sequential reliable,
            0x20 channel, 0x80 connecting
```

Channel 3 is the system channel. The last data byte is the message id: 1 connect request,
2 connect ack, 3 disconnect, 4 clock sync, 6 acknowledgements. Handshake payload: version
`u32 = 5`. Readers: 0xF898E0 (datagram header), 0xF77EB0 (messages).

Transport use of channels (pump at 0x6B3C250): reliable data on channel 0, unreliable data on
channel 1.

Sizes (behavioral proof): the data of one message has 1,024 bytes at most. A larger reliable
message goes in chunks: the first chunk has the chunk count, and each chunk is one message.
The client used a bundle of 1,275 bytes in two chunks (stage 1.7: the point state of a
character with 114 tree nodes). A datagram has 1,038 bytes at most: the datagram header (4),
the largest message header (10), and the largest message data. The client closes its
connection when it gets a larger datagram: the backend had the limit 1,200 before stage 1.7,
and a datagram of 1,187 bytes with two messages (the registration replies and a state bundle
of 805 bytes) made the client close the connection 28 ms after the first record, with
`ClientSDK: Unexpected client disconnect! {"transportError":2}` in the game log. With the
limit 1,038 the same record goes out in datagrams of 366, 1,004, 1,037, and 275 bytes, and
the client enters the world. The largest datagram of each earlier run had 1,037 bytes.

### 3.3 Frames (static proof and behavioral proof)

```text
client -> server := crc32:u32 size:u32 payload      payload := target:16 bytes envelope
                    (writer 0x6B0F430; CRC-32 of the payload)
server -> client := size envelope                   (parser 0x6AE44F0; no checksum, no target)
envelope := flags:u8 [8 bytes if bit 0] [8 bytes if bit 1] [extension if bit 2] message
message  := present:u8 type:size body               (readers 0x61AC8D0, 0x17B2430, 0x61ACFE0)
```

`type` is an index into `typeindex.json` in the game root. Index 0 is followed by the 16-byte
type id.

Primitive encodings: `u32`/`u64` big-endian (0x876CD0, 0x8770E0); `size` prefix code
(0x877970): `0xxxxxxx`, `10xxxxxx`+1, `110xxxxx`+2, `1110xxxx`+3, `11110xxx`+4 bytes, low bits
first; `string` = size + bytes; `bool` = 1 byte.

Each message type has an operations table: allocate, free, marshal, unmarshal. Find it from the
runtime type name `InstallRegistrationHook<T>` (tools: `tools/gh symbols T`).

### 3.4 Registration (behavioral proof)

| Message | Type index | Body |
| --- | ---: | --- |
| `REPClient::RegistrationRequestV3Msg` | 19 | `u32`, version map (6 entries: key `u32`, text), text, login token, client data, 2 identities, flag |
| `REPClient::RegistrationResponseMsg` | 3 | `result:u32` (0 = accepted), `serverTime:u64` (nanoseconds), text, text, 4 flags (4th: anti-cheat required) |

Unmarshal of the response: 0x7CD040. Handler: 0x6B6F190.

Field order of the request (writers 0x7CE860, 0x7D54A0, 0x7D5F30, 0x7D4920, 0x7D4B50,
0x7D42C0, 0x7D4540, 0x7D3F70: static proof; the backend reads the fields to the ticket in
each login, and the values agree with the login token and the hook: behavioral proof). A field
with no type is a text.

```text
login token := Signature RepAddress WorldId CharacterId PersonaId SteamAppId:u32 SteamUserId
               ChannelId TicketId text text IssueTime:u32 GenerateTime:u32 AccountAge:u32 text
               TokenVersion:u32 u8 text present:u8 [flag:u8 if present] text text text
client data := u32 u8 SteamAppId:u32 SteamId Ticket text u32 text text text text text
identity    := flags:u8, then a 16-byte id when bit 0 is set, else a text
```

- The login token has the values of `LoginQueueResponse.Token` (2.1 step 5). `IssueTime` and
  `GenerateTime` are the low 32 bits of the millisecond values of the reply. The names of the
  fields with no name here are not known (in the captures: empty texts, `u8` 0, and the
  present flag with the value 1).
- `SteamId` of the client data is the decimal text from `GetSteamID`. `Ticket` is `steam|`
  and the hexadecimal text of the ticket bytes (0x5A871A0). The first two fields of the
  client data were 1 and 1 in each capture.
- The backend reads the persona id, the two Steam ids, and the ticket, compares them with
  the account of the persona, and writes the result to the log. A Steam id that does not
  agree is not a cause for a refusal (stub authentication, 2.1).
- The backend refuses a registration whose character id the store does not have, and a
  character that is not a character of the persona id of the login token: the reply has the
  result -160, and no other message follows (3.6). The login token has a `TicketId`, which
  the gateway makes new for each login queue reply: the backend does not keep it and does
  not compare it (a login ticket is not a part of the product now).

### 3.5 Sessions of the game server

The session rules are a design of this project: no capture of an original server exists.
The statements about the client have their evidence level.

**One session for each connection.** A peer of the DTLS listener is the IPv4 address and the
UDP port of the client. Each peer that completes the DTLS handshake is one session: it has
a session id (1, 2, ... in one backend process, not used again), one carrier connection, and
one `RepSession` (`apps/backend/src/service/rep_server.h`).

**What the client sends when the user leaves the world** (behavioral proof, 3 runs: Game
Menu, "Exit to Main Menu", "Yes"). The client sends the carrier disconnect (system message
3, reason 0) less than 0.1 s after the click. It sends no DTLS close notice: the secure
session of the backend got no close in 130 s. Game log: `CGame::OnDisconnection:
Disconnected from REP`, `Update state InGame to new state Disconnected`, `CGame exiting to
main menu`.

**A second connection of one game process** (behavioral proof, 3 runs; port rule: static
proof). Play in the main menu makes a new login queue request (a new login token) and then
a new connection from the next UDP port: 27000, then 27001 (`javelin.client-port` plus a
counter of the process, modulo `javelin.client-port-range` + 1 = 51; client code 0x6B6A3ED).
The client registers again with the same character.

**Level information of a second connection** (static proof 0x6446800, and behavioral proof
in the two directions). The handler of `LevelInfoChangedMsg` in the client:

1. It does nothing when `version` of the message is equal to the version that it keeps
   (game message port -0x7F8). The value stays after a disconnect: the line `[GameMessagePort]
   Reset` of the game log does not clear it.
2. With `reload` = 1 it writes `LevelInfoChanged m_clientContextInstanceId A changing to
   levelInfo.m_clientContextInstanceId B` to the game log, and it does nothing more when A
   is equal to B. A is the instance of the last connection, also after a disconnect.
3. If not, it makes the client context (`Calling clientContext->Initialize`) and loads the
   level.

Thus the level information of each connection of one game process must have a version and
an instance that the client did not get before. Live proof of the fault: with version 1 and
instance 1 in each session (the backend before stage 0.6a), the second entry stays in
`WaitingForPlayerSpawn`, the client drops each state bundle (`skipping ReceiveStateBundle
... clientContext == nullptr`), and after 120 s it shows "Timed out while waiting for server
to spawn the player". With a new instance and version 1 the client also drops each bundle
(`received bundle for a previous ClientContext: expected=1, incoming=4`). The backend now
sends the session id as the version, and `(session id - 1) mod 255 + 1` as the instance (in
the level information, in the level block of the self identification, and in each state
bundle). Session 1 has version 1 and instance 1, as before. Live proof of the repair: game
log `m_clientContextInstanceId 1 changing to levelInfo.m_clientContextInstanceId 2`, then
`InGame`.

**The account rule** (stage DG1, section 4.66; docs/systems/instances.md). A level change in one connection gives the client a new version
and a new instance too, and the session id alone then fails: session 1 moves to instance 2, and session 2 of the same game process would
start with instance 2, the instance that the client has (research dungeons.md 2.4; the client compares only with its last values: the
branches 0x64468B1 and 0x6446B47 of 0x6446800 [static]). The game server keeps, for each account (persona id), the last version and the
last instance that it gave (`server/rep/level_contexts.h`); each new level information of that account (a world entry, a level change)
gets one more version and the next instance (1 to 255, never 0) [project rule: the backend cannot see the game process; the key is the
account]. The first level information of an account in a backend process has the values of the session id (the stored bytes of case T2).
Live proof (stage DG1): a second connection of one game process after three level changes got version 5 and instance 5, and the client
entered the world (`m_clientContextInstanceId 4 changing to levelInfo.m_clientContextInstanceId 5`).

**Session end.** A session ends one time, with one of these causes (log line `rep: session
ended, cause: CAUSE; client port N, A datagrams in, B datagrams out (C bytes) [mark]`):

| Cause text | Condition | Where the backend finds it |
| --- | --- | --- |
| `carrier close` | The client sent the carrier disconnect (the user left the world, or the client ended the connection after an error or a timeout of its own). | After each datagram: state of the carrier connection |
| `secure session closed` | DTLS close notice, DTLS read error, or a new DTLS handshake from the same port. | `on_disconnected` of the transport |
| `idle (no datagram for 30 s)` | No datagram from the client for 30 s. In one run the largest time between two datagrams of a client was 0.117 s. | Each tick, with the world clock |
| `no registration (30 s)` | No registration request 30 s after the start of the session. | Each tick, with the world clock |
| `error` | A handler of this session stopped with an exception. | Guard around the message handlers and around the tick of each session |
| `registration refused` | The session refused the registration (result -160, 3.6). | The client closed its carrier connection after the reply, or 1 s after the reply |
| `replaced by a newer session of the character` | A newer session registered the character. This session sent the message "kicked" with reason 1 (3.6). | The client closed its carrier connection after the message, or 1 s after the message |
| `character deleted` | The delete route removed the character while it was in the world. This session sent the message "kicked" with reason 2 (3.6). | The same |
| `backend stop` | The backend stops. | `RepServer::stop` |

- Each end makes the session leave the world (`World::leave`): the world writes the position
  of the character when it is not the saved position, and it removes the entity of the
  character. A session that lost its character changes nothing in the world. The end only
  marks the session: the next tick erases its objects.
- The backend sends nothing to the client at the end of a session, and it does not close
  the secure session. The refusal and the two "kicked" messages go out before the end: the
  session then gets no state and reads no message of its client, and it stays for 1 s at
  most so that the carrier can give the message to the client. Behavioral proof (one run):
  after a DTLS close notice of the backend, the client did a new DTLS handshake from the
  same port two times in 0.5 s. The DTLS listener ends a secure session with no close notice
  after 60 s with no datagram.
- A client with no input of the user leaves the world itself (behavioral proof, one run of
  stage 0.6b): 20 minutes after `InGame` the game log has `CGame::ActorContainerDisconnect`,
  `Disconnected from REP`, and `CGame exiting to main menu`. The client sends no carrier
  disconnect in this case: the backend ends the session with the cause `idle` 30 s later.
  A click on Play enters the world again.
- A client that stops with no message (live: the game process stopped with `kill -STOP`)
  got more data in each period before this stage, because each reliable message goes out
  again each 250 ms: 39,594, 114,796, 190,036, 265,952, and 341,312 bytes in the five periods
  of 10 s after the stop. With the idle limit the data stops after 30 s.
- A character is in one session at most, and the newer session wins (a design of this
  project: a session can stay with no client, thus a rule "refuse the new one" can lock a
  character out). When a session registers a character that an older session has, the world
  writes the position of the entity, gives the entity with its ids and its position to the
  new session, and tells the older session: it sends the message "kicked" with reason 1 and
  ends. The new client gets the position of the entity, not an older row.

**Log and captures.** Each log line of a session (`rep:`, `carrier:`) ends with its mark:
` [s1]` before the registration, ` [s1 4aae16c4]` after it (session id, first 8 characters
of the character id). The hexadecimal lines `rep: in`, `rep: out`, and `rep: message` are
for the first 96 packets of each session. The capture files of a session are in its own
folder with its own counter and its own limit of 32,768 files:

```text
captures/rep/s0001/NNNNN-in-LEN.bin     one clear datagram of the client
captures/rep/s0001/NNNNN-out-LEN.bin    one clear datagram of the server
captures/rep/s0001/NNNNN-msg-LEN.bin    one complete client frame (3.3)
```

### 3.6 Refusal and end messages (static proof, and behavioral proof for three values)

The client has three ways to learn that the server does not accept its session. The backend
uses two of them as product behavior from stage 0.6b on (the end of this section). The
research switch of the game server (`SOULWARDEN_RESEARCH_SESSION`, backend option
`--research-session`, off by default) sends one of them for a live check. The launcher gives
the switch only to a run with `SOULWARDEN_HARNESS=1`, and the backend refuses the option (and the
option `--dev-commands`) when its own process has no `SOULWARDEN_HARNESS=1`: it then starts no
listener. Evidence of the three checks:
`~/Games/soulwarden/work/evidence/session-model/implement/S9-*`.

**Registration result** (`RegistrationResponseMsg`, type 3). The client reads `result` as a
signed number. A value that is not 0 goes to function 0x6B70D00, which makes a text key and a
reason, and the client ends the connection (0x6B6C500). A refusal is the reply alone: a
different message before a reply with result 0 ends the connection with reason 1 (dispatcher
0x6B6F190). Result values with a text key `@mm_authresult_Denied_...` (static proof, 28
values, read from the two jump tables of 0x6B70D00): 37 `CapacityLimitReached`, -10
`ClaimConnectionTicketFailed`, -70 and -100 TTL exceeded, -80 `VersionCheckFailed`, -90
`TypeIndexMismatch`, -110 to -150 and -170 login token faults, -160 `LoginTicketInvalid`,
-200 `EasyAntiCheatFailure`, -210 `EOSIsRequired` (reason 13), -300 to -352 and -399 auth
token faults (-330 has reason 9), -900 `HubNotReady` (reason 9). All other values have
reason 8.

Live check 1, result -160 (behavioral proof, one run, switch `registration-result=-160`):

- Trace: 0x6B70D00 with `edx` = 0xFFFFFF60, then 0x6B6C500 with `edx` = 8.
- Game log: `ClientSDK: @mm_csdkerr_registration_failed: @mm_authresult_Denied_LoginTicketInvalid
  (Status -160) (0)`, `Report Error: Reason: Errors::unknown`, `shouldRetryConnect=false,
  isErrorRetryable=false`, `Connection fail popup from main menu: error 1`.
- Screen: the main menu with the popup "CONNECTION ERROR. Error Code: Could not connect to
  server.: Attempted login ticket is invalid or expired. Please retry login. (Status -160)
  (0)" and the button OK. The popup stays (45 s). The client sends no second login queue
  request and no second registration.
- Backend: the client sends the carrier disconnect 33 ms after the reply.

**Rejected** (`Javelin::ClientMessagesTrait::PlayerManagerRejectedMsg`, type 1630, body
`reason:u32 text`, reader 0x14FF630, handler 0x64549B0). It goes with the plain envelope of
the game messages (`00 01 type body`). The handler changes the wire reason to a value of the
client enumeration `Errors` (name function 0x1003620): 0 to 13 are 23 to 36 (`aoi_errors`,
`aoi_pending`, `aoi_duplicate_connection`, ...), 14 is 60, 15 is 38, 16 is 39, 17 is 42, 18
is 43, 19 is 40, 20 is 41, 21 is 44, other values are 1 (`unknown`) (static proof, jump
table 0x6454B94).

Live check 2, reason 2 (behavioral proof, one run, switch `reject=2`, frame `09 00 01 9E 19
00 00 00 02 00` after a registration reply with result 0, in place of the self
identification):

- Trace: 0x64549B0 with `edx` = 2, then the error function 0x5A86D80.
- Game log: `Report Error: Reason: Errors::aoi_duplicate_connection`, `shouldRetryConnect=false,
  isErrorRetryable=false`, `Connection fail popup from main menu: error 25`.
- Screen: the main menu with the popup "CONNECTION ERROR. Player is already connected." and
  the button OK. The popup stays (45 s), and the client does not try again.
- Backend: the client sends the carrier disconnect 50 ms after the message.

**Kicked** (`KickClientTrait::KickClientMsg`, type 271, body `reason:u32 u64 text`, reader
0x7CB980, handler 0x6446170). It goes with the plain envelope. Wire reason to `Errors`
(static proof, jump table 0x6446454): 0 is 57 `kicked`, 1 is 58 `kicked_duplicate_session`,
2 is 59 `kicked_character_deactivated`, 3 is 60 `kicked_maintenence`, 4 is 61, 5 is 62 (70
`banned_cheating` with the text `Cheating_EAC_Ban`), 6 is 63, 7 is 64, 8 is 65, 9 to 24 are
66 to 81 (`banned` ...). `KickClientTrait::KickClientFromRepMsg` (type 254) has no handler
in the client: do not use it.

Live check 3, reason 1 (behavioral proof, one run, switch `kick=1`, frame `11 00 01 8F 04
00 00 00 01`, 8 zero bytes, `00`, sent 10 s after the first camera position of a client that
is in the world):

- Trace: 0x6446170 (`rdx` points to the reason 1; probe 0x64461B4 has `edx` = 1), the error
  function 0x5A86D80 with `edx` = 0x13, and 1.1 s later the popup function 0x102D420.
- Game log: `Report Error: Reason: Errors::kicked_duplicate_session`, `Update state InGame to
  new state Disconnected`, `shouldRetryConnect=false, isErrorRetryable=false`, `Connection
  fail popup from in-game: error 58`, event `Popup_OnConnectionFail_InWorld`.
- Screen: the world, dark, with the popup "CONNECTION ERROR. Your account has connected from
  another game session." and the button OK. The popup stays (50 s), and the client does not
  try again. What the client does after OK was not tested.
- Backend: the client sends the carrier disconnect 17 ms after the message.

Conclusions:

- Each of the three ways works with the bytes above, and each one shows a text to the user.
  The client ends the connection itself in less than 0.1 s: the server does not have to
  close the session, and it must not send a DTLS close notice (3.5).
- Not tested: the results with reason 9 (-330, -900), which name a retry; the other reasons
  of the two messages; the text of each message.

**Product behavior of the backend (stage 0.6b; a design of this project).**

| Condition | What the client gets | End cause of the session (3.5) | Proof |
| --- | --- | --- | --- |
| The registration has a character id that the store does not have (also: no character id) | Reply type 3 with result -160 (`FF FF FF 60`), and no other message | `registration refused` | Bytes: test case T4. Effect in the client: live check 1 above. |
| The character is a character of a different account (the persona id of the login token is not its owner) | The same | `registration refused` | The same |
| A newer session registers the character of this session | `KickClientMsg` (type 271) with reason 1: frame `11 00 01 8F 04 00 00 00 01`, 8 zero bytes, `00` | `replaced by a newer session of the character` | Bytes: test cases T3 and E7. Effect in the client: live check 3 above. |
| The delete route removes the character of this session (`DELETE /prod/game/characters/{id}` of the account of the character, from a different client) | `KickClientMsg` with reason 2 (`kicked_character_deactivated`): the same frame with `02` | `character deleted` | Bytes: test case D1. The effect of reason 2 in the client has static proof only (the jump table above). |

- The world makes the decision (`World::enter`, `World::remove_character`), not the session:
  the store has the owner of a character, and the entity table has the session of a
  character.
- The message "rejected" (type 1630) is not product behavior: a second session of a
  character is not refused.
- The delete route runs in an HTTP thread. It gives the world one task that removes the
  entity and then deletes the row, so that no session registers the character between the
  two steps. The stop of the backend waits for each HTTP connection thread before it
  destroys the world.

## 4. World entry (behavioral proof)

The messages of 4.1 to 4.6 go from the server to the client on carrier channel 0. The input
report of 4.7 goes from the client to the server on carrier channel 1.

### 4.1 Messages

| Message | Type index | Body |
| --- | ---: | --- |
| `PlayerManagerSelfIdentificationMsg` | 1628 | identity, actor reference x3, level block, identity (reader 0x14FD530) |
| `LevelInfoChangedMsg` | 1635 | `level:text terrain:text u32 x4 gridLayer:u64 list flag instance:u8 reload:flag flag version:u64` (writer 0x1500850; gridLayer: section 4.69) |
| `ClientViewListenerTrait::ReceivePlayerSpawnPointMsg` | 1617 | no fields |
| `ReplicatedStateBundle` | 8 | `present:u8 sequence:size64 instance:u8 u8 unreliable:flag control:flag [control block] size payload` (reader 0x6B07870; the control block is in 4.2) |

- identity := `flags:u8`, then a 16-byte id when bit 0 is set, else a text.
- actor reference := `hub:u32 actor id:16 bytes actor type id:16 bytes` (reader 0x15ACC30).
  The client sends only the actor id of a reference: it is the 16-byte route of a client
  frame.
- Self identification of the backend (behavioral proof: world entry, and the routes of the
  client frames of the live runs of stage 0.6b). The ids come from the id allocator of the
  world (4.2):

  | Part | Value |
  | --- | --- |
  | identity 1, identity 2 | `01` and the actor id of the session (serial from `0x10000001`, kind `02`) |
  | reference 1 | hub 1, the actor id of the player manager of the world (serial `0x10000000`, kind `01`), type id `38A737FF-6CAD-4792-A2F9-324736E0EE02` (`Aoi::PlayerManagerActor`) |
  | reference 2 | hub 2, the actor id of the session, type id `2970E283-FCF3-4B6F-8F07-86D68BF3AE45` (`Amazon::Hub::ClientProxy`) |
  | reference 3 | hub 0 and 32 zero bytes (null) |

  Reference 1 is the address of the client messages that are not for an entity (static proof
  0x5A9E6C0). With an actor id in reference 3 the client starts its bot spawn: it sends type
  1665 to reference 1 and writes `BotUtils: Spawn type is not supported` to the game log
  (behavioral proof: the backend sent an id there before stage 0.6b; with the null
  reference the client sends no type 1665). The client did not check the type ids.
- `level` is the level name (`newworld_vitaeeterna`). `terrain` is the terrain project
  (`coatlicue/newworld_vitaeeterna`, game data folder `sharedassets/coatlicue/<name>`). The
  client starts its terrain system only when `terrain` is not empty (0x6448CD0, 0x644C310).
  With an empty text the loading screen does not end.
- `instance` is the client context instance. It must be the same in the level information and
  in each state bundle. `reload` = 1 makes the client load the level. The client does
  nothing for a level information whose `version` or `instance` is the value of its last
  connection: each session has its own values (3.5).
- The client accepts a state bundle only after the level load started. A bundle before that
  is dropped ("skipping ReceiveStateBundle ... clientContext == nullptr").
- The sequence of the reliable bundles of one connection is 1, 2, 3, ... with no gap: the
  client drops each bundle after a gap (static proof 0x644B280). Each session of the backend
  has one counter for all its bundles: the records of its own character, the records of
  other entities, and the stops.
- The client disconnects with `connection_ingame_timeout` when it gets no game message for
  30 seconds (0x6445E10, time at REP client +0x608). The backend sends a state bundle each
  200 ms (the movement state of 4.6).

### 4.2 State bundle payload

```text
payload  := record*
record   := interest:size fragments:u8 fragment*         (reader 0x6AF20D0)
fragment := slot:size type:size state                    (reader 0x6AF2340)
```

- An interest is one entity group ("GDE") of the client. Interest 1 is the player of the client
  (apply function 0x1717FC0).
- `type` is the type index of a replicated state type.
- `slot` is the index of a component in the component list of the entity group (entity group
  +0x160, apply function 0x167B2A0). Slot 0 has no component. The slot of a component is its
  field `m_replicationIndex` in the slice file (`tools/research/slice_slots.py`).
- The client gives a fragment to the client facet of the component (component +0x80, virtual
  function +0xB0). A fragment with the state type of a different component gets no error and
  has no effect: find the apply function of the facet and trace it.
- The first fragment of a new entity group is `MB::GdeMetadataReplicatedState` (type 10). It
  gives the slice asset id and the actor id. The client then instantiates the slice.
- Fragments that arrive before the slice is ready are kept and applied at instantiation.
- A record for an interest number that has no metadata in the client is dropped with no log
  line (static proof 0x1717FC0). A second record with the metadata state for a number that
  is in use is merged: the entity stays one, and the game log gets the warning
  `StateReplication: Received repeated metadata` (behavioral proof, 2 runs, for the entity of
  a different player).
- For the own player (interest 1) a second record with the metadata state is not safe
  (behavioral proof, 1 run of stage 1.3 with `tools/world touch` for the character of the
  client): the game log has the same warning, and 3 ms later the client closes its connection
  (`ClientSDK: Unexpected client disconnect! {"transportError":2}`, carrier close with reason
  2). The error is one that the client tries again: it asks for a new login token and
  registers again at once, and its new session enters the world with no action of the user.
  The backend sends the metadata state of an entity to its owner one time only.
- A record goes out in more than one bundle when its fragments are larger than one carrier
  message (4.10). The first bundle has the metadata state.
- An interest number is an index into a table of the client with 65,534 places (client
  context +0x608). The client keeps 16 bits of the number and makes no range check: use 1 to
  65,533 (static proof 0x6AF20D0, 0x1613A10).

**Ids of an entity** (static proof `~/Games/soulwarden/work/research/ids.md`, and behavioral
proof in the live runs of stage 0.6b for the actor ids `02000000 415A4F54 10000000 415A4F54`,
`04...`, and `06...`; the tag bytes of an id were `41 5A 4F 54` in that stage, and they are
`53 4F 55 4C` ("SOUL") now, with a live proof of its own, below):

```text
G               = first 8 bytes of the actor id as one little-endian number
                  (entity group id, "GDEID"; reader 0x17B3AE0)
K               = id of the root entity of the slice XOR G        (0x17898F0, 0x171E370)
instance entity = entity id in the slice file XOR K               (the root entity gets G)
target          = (instance entity + component id) mod 2^64       (facet "local id", 0x16C1020)

component call of the client := route = actor id (16 bytes), body = target:u64 entity:u64 ...
                                (big-endian numbers; `entity` is G)
```

- The root entity of `slices/player.dynamicslice` and of `slices/remoteplayer.dynamicslice`
  is `RootPlayer`, with the same id. The remote slice has the same entity ids and the same
  component ids for the components that it keeps: a component of a character has the same
  target on each client.
- Rules for an actor id of the server. The first byte must be even: an id with an odd first
  byte is an offline ("managed") id with the default configuration of the client
  (`javelin.offline-id-version` 2 and `javelin.enable-offline-capitals` true, 0x6165F20), and
  the client makes no entity group for it. Bytes 4 to 7 must not be zero (offline id of
  configuration version 1). Two entities must not have equal first 8 bytes. Two player
  entities whose ids differ only in byte 4 can get equal entity ids: ids that differ only
  in bytes 0 to 3 are safe. (This replaces the rule `(first byte & 7) == 1` of the earlier
  notes, which is the rule of a configuration that is not the default.)
- Form of the ids of the backend (a design of this project, `world/ids.h`):
  `serial * 2` as `u32` little-endian, `"SOUL"`, `kind:u8`, 3 zero bytes, `"SOUL"`. Kinds and
  serial ranges: player character `10` (1 to `0x0FFFFFFF`), player manager `01`
  (`0x10000000`), session `02` (from `0x10000001`), other entity `20` (from `0x20000000`).
  The world gives an entity its id when it makes the entity. An id is not used again in one
  backend process: a character that leaves the world and enters it again gets a new id.
- Live proof (stage 0.6b). With the actor id of serial 1 and of serial 2 in two world
  entries of one game process: the game log has `Player Actor:
  02000000415A4F5410000000415A4F54` (then `04...`) and `InGame`; the route of each of the 25
  component message types of the client is the actor id; the `entity` field is `54 4F 5A 41
  00 00 00 02` (then `...04`); and 16 of 16 targets of the capture are the targets of the
  rule (for example `335C430A8D27619F` for the player component with serial 1). Thus the
  byte order and the rule of `K` are correct for a player entity.
- Live proof with the tag `"SOUL"` (one live run after the change of the tag bytes): the game
  log has `Player Actor: 02000000534F554C10000000534F554C` and `InGame`; the entity group id
  of the character is `4C554F5300000002`; each of the 5,859 component messages of the client
  in the session has a target of the rule; the client shows a remote player (`04000000
  534F554C ...`) and the creatures (`...000040 534F554C 20000000 534F554C`); and a melee
  contact of the client names the actor id of its creature. The stored bytes of test case T2
  changed only in the tag bytes.
- The backend checks each client message against the rule (`RepSession::check_route`). The
  route must be the player manager or the actor id of an entity of the interest table of
  the session. For an entity: `entity` must be its `G`, and `target` must be a target of a
  faceted component of the player slice. The backend reads the ids of the 186 faceted
  components from `slice-ids.json` (game data, `tools/gamedata/slice_ids.sh`; with no file
  it makes no check of a target). A message that fails gets a log line and a count: it has
  no other effect now. The end of a session writes the counts (`rep: client messages of the
  session: ...`). Live result: 36,031 component calls in 20 minutes, 17,211 in a second
  session, and 1,969 in a third run, each with 0 targets that are not known, 0 with a wrong
  entity, and 0 routes that are not known. The client sent no message with the route of the
  player manager in these runs.

**Stop of an entity** (static proof `~/Games/soulwarden/work/research/entities.md`, behavioral
proof in 3 runs). A state bundle can have a control block (4.1: the second flag of the
bundle is 1):

```text
control block := stopCount:size count:size (interest:u16 big-endian) * count
                 count <= 100. The first stopCount numbers are "stop", the others are "pause".
                 stopCount must not be more than count: the client makes no check.
bundle with the stop of interest 2 and no record (sequence 8, instance 1):
                 01 08 01 00 00 01   01 01 00 02   00
```

The client removes the entity group of each stop number at once and clears its table place
(0x1733A20, 0x178EC80: one call of each for each stop in the live runs), and the body and
the name tag go away. After the stop, the same number can start a new entity (behavioral
proof). The message `Amazon::Hub::ReplicationControl` (type 9) has the same list: the
backend does not use it (not tested).

### 4.3 State formats

Common form (reader 0x17B4110, group reader 0x17B43C0):

```text
state := groupMask:u8 (one byte for each 8 groups of the type)
         for each group in the mask: ( fieldMask:u8 value* )+
         A field mask has 7 fields in bits 0 to 6. Bit 7 means that one more mask follows.
```

Movement state form (`MB::ALCReplicatedState`, reader 0x2A436B0):

```text
state := groupMask:u8
         for each group in the mask: fieldMask:size64 value*      (one bit for each field)
```

`size64` is the prefix code of `size` for 64-bit values (reader 0x87B770): N leading one bits
in the first byte give N more bytes; `0xFF` is followed by 8 bytes.

`tools/st NAME_REGEX` prints the groups, the field order, and the field readers of a state
type. The index of a field is its position in the registration sequence of its group. Some
types keep a group number in a variable of the image (the vitals state); the tool then prints
the start value of the variable and the address to read in the live client.

Field value encodings used by the backend:

| Field | Encoding |
| --- | --- |
| Asset id | 16 bytes, `sub:u32` (0x17B3670) |
| Actor reference (`GdeRef`) | 16 bytes (0x17B3AE0) |
| Identity text (`characterId`) | `flags:u8` (0 = text), text (0x34CA540) |
| Text | size, bytes (0x17B3750) |
| Flag, small number | 1 byte (0x17B3CB0, 0x279E210) |
| Time anchor | `u64` (0x2A43500) |
| World position | `x:f32 y:f32` big-endian, `height:u16` = (z + 100) / 1100 * 65535 (0x2A433D0) |
| Number with a fraction | `f32` big-endian (0x2A42F30, 0x87A270) |
| Tick rate | 16-bit float, big-endian (0x2A42F80, 0x87AE90) |
| 16-bit number | `u16` big-endian (0x2A42E10) |
| 32-bit number, name CRC | `u32` big-endian (0x17B3620, 0x32D9460) |
| Rotation, look direction | compressed unit quaternion (0x2A43380, 0x2A434B0, decoder 0x87A8D0) |

Compressed unit quaternion: one header byte, then one byte for each component that is not the
largest one and not zero. Header: bit 0 = sign of the largest component, bits 1 to 4 = the
component x, y, z, w is zero, bits 5 and 6 = index of the largest component. A component byte
`b` is `b * 0.0055459356 - 0.70710677`. The largest component is the square root of the
remainder. Thus `0x6E` is (0, 0, 0, 1) and `0x3A` is (0, 1, 0, 0), each with no more bytes.

### 4.4 Player slice (`slices/player.dynamicslice`)

Asset id `A660EEEB-EBC7-5CB7-BE6B-AB11EB831731`, sub id 2 (from `assetcatalog.catalog` in
`Engine.pak`; `slices/remoteplayer.dynamicslice` has the same id and sub id 100). The entity
group has 84 component slots.

| Slot | Component | State type | Type index |
| ---: | --- | --- | ---: |
| 0 | (metadata) | `MB::GdeMetadataReplicatedState` | 10 |
| 9 | Player | `MB::PlayerComponentReplicatedState` | 3935 |
| 12 | Appearance | `MB::PlayerAppearanceComponentReplicatedState` | 1195 |
| 16 | Movement (action list, "ALC") | `MB::ALCReplicatedState` | 11 |
| 27 | Attribute | `MB::AttributeComponentReplicatedState` | 129 |
| 41 | Vitals | `MB::VitalsComponentReplicatedState` | 15 |
| 46 | Ability (the ability bars) | `MB::AbilityComponentReplicatedState` | 185 |
| 60 | Currency (the coin) | `MB::CurrencyComponentReplicatedState` | 3786 |
| 61 | Container (the bag) | `MB::ContainerComponentReplicatedState` | 1755 |
| 62 | Paperdoll (the equipment) | `MB::PaperdollComponentReplicatedState` | 3183 |
| 65 | CategoricalProgression (weapon mastery, trade skills) | `MB::CategoricalProgressionReplicatedState` | 911 |
| 67 | ProgressionPoint (the weapon trees) | `MB::ProgressionPointReplicatedState` | 3681 |
| 70 | Progression (level, experience) | `MB::ProgressionComponentReplicatedState` | 899 |

All 13 rows have behavioral proof (a trace of the apply function of the facet, or the
effect in the game). Slot 32 is the magic component, not the vitals component. The rows 60
and 61 are in 4.10, the row 62 is in 4.11, the rows 27 and 70 are in 4.12, and the rows 46,
65, and 67 are in 4.13.

`tools/research/gde_components.py` reads the slot list from the live client, and
`tools/research/slot_names.py` gives the class name of each slot.
`tools/research/component_states.py` gives candidate state types for the slots: its output
was wrong for the vitals state.

Fields that the client must get to enter the world:

| State | Field | Why |
| --- | --- | --- |
| Metadata | `AssetId`, `GdeRef` | Instantiate the player slice. |
| Player | `characterId` (group 2, field 0) | Equal to the character id of the login makes the entity the local player (0x6929DF0). The client state changes to `InGame`. |
| Player | `characterName` (group 2, field 1) | Player name. |
| Player | `playerConnected` (group 2, field 3) | The loading screen waits for a connected local player (0x102EA20). |
| Movement | `worldPosAbs` (group 0, field 27) | Spawn position. World streaming starts at this position. |
| Movement | `teleportAndMigrationId` (group 0, field 31) | Low 4 bits count teleports. |
| Movement | `timeAnchor` (group 0, field 40) | The client ignores a movement state without it (0x485A550). |

Fields that the client must get before the player can move and look correct (4.6 gives the
reasons for the movement fields):

| State | Field | Why |
| --- | --- | --- |
| Movement | `rotation` (group 0, field 11), `lookDir` (group 0, field 12) | The client does not initialize these two values. |
| Movement | `dataBits` (group 0, field 41), `moreDataBits` (group 0, field 42) | Movement permissions of the server. |
| Vitals | `HealthAmount`, `HealthMax`, `maxHealth`, and the same for stamina and mana (4.5) | Health on the HUD. |
| Vitals | `vitalsId` (group 0, field 8) | Row of the vitals tables. |
| Appearance | `playerGender`, `playerRace` (group 0, fields 0 and 1), and fields 2 to 11 | The client reads the appearance only when it has the gender and the race (handler 0x683BA40). Without them the body is white. |

### 4.5 Vitals and appearance (behavioral proof)

`MB::VitalsComponentReplicatedState` has 2 groups (constructor 0x671E040). The group number
of the health and stamina fields is in two variables of the image (0xA563290, 0xA563298: both
0 in the live client), the mana fields are in the group that the constructor makes (1).

```text
group 0:  0 HealthAmount:f32      1 StaminaAmount:f32   2 vitalsData   3 healthChangeFlags
          4 HealthMax:f32         5 HealthTickRate:f16  6 StaminaMax:f32
          7 StaminaTickRate:f16   8 vitalsId:u32        9 vitalsCategoryId:u32
         10 vitalsLevel:u32      11 invulnerability    12 displayImmuneWhenInvulnerable
         13 maxHealth:u16
group 1:  0 ManaAmount:f32        1, 2 afflictions      3 ManaMax:f32   4 ManaTickRate:f16
```

- The facet (apply function 0x683E670, then 0x67A7560) makes the stat `Health`, `Stamina`, or
  `Mana` when it gets one of the amount, maximum, or tick rate fields of that stat. Without
  the stats the game log gets `VitalsComponent: Could not find stat` on each frame.
- The HUD shows `maxHealth` (16 bits, getter 0x67FCDC0). The client stores it only when the
  state object also has `HealthMax`.
- `vitalsId` is the CRC-32 of a row name of the level variant vitals tables (lookup
  0x5C423D0). The row of a player is `player` (0x98197A65; the key is in the table of the
  live client). Without it the client has no regeneration rates
  (`Could not find stat {"functionName":"GetStatRegenRate"}`).
- The backend sends the maximum health that follows from the level and from the constitution
  of the character (4.12: 856 for a new character, behavioral proof on the HUD), stamina 100
  of 100, mana 100 of 100, and tick rates 0. With no progression tables it sends health 1000
  of 1000. The game log has no stat warning.
- **The immune mark of a creature** (stage 3.4e; static proof of the fields, behavioral proof
  of what the client does). `invulnerability` and `displayImmuneWhenInvulnerable` are one
  byte each; the facet keeps them at +0x284 and +0x285 (apply function 0x683E670). A
  creature on its way back (the reset, 4.17) has the two fields 1, and 0 again at its anchor:
  the first vitals state of a session has the two values, and a later state has the two
  fields when the mark changed after the last state of the session. Live (runs `c2` and
  `pt3`): the client makes no melee contact with a creature that has `invulnerability` 1 in
  a direct attack (2 of 2 attacks in the check with a raw fragment, and 4 sword attacks at
  1.5 m on a Grey Wolf on its way back, against 3 hits in 3 attacks at a wolf with no mark at
  the same distance): no claim, no hit mark, no damage number. One swing at the wolf with no
  mark also gave one contact on the immune wolf beside it: the backend refused that claim
  (cause "the creature is on its way back (immune)") and nothing changed. The name plate
  shows no mark, and `displayImmuneWhenInvulnerable` alone changed nothing that the check
  saw. The message 1924 (`DisplayImmuneText` of the damage receiver facet, reader 0x6572380)
  showed no text with three bodies (the damage receiver facet of the own character or of the
  creature as the target): the backend does not send it.

`MB::ManaComponentReplicatedState` (type 1652, slot 47 of the player slice, the component
`Mana` of the entity "Stats"; constructor 0x67088C0, apply 0x683B740; behavioral proof, user
test 1) has 1 group with 4 fields, each a 32-bit float, big-endian (reader 0x2A42F30):

```text
group 0:  0 current   1 maximum   2 regeneration delay   3 regeneration rate
fragment of 100 of 100:  2f b4 19 | 01 0f | 42 c8 00 00  42 c8 00 00  00 00 00 00  00 00 00 00
```

- The mana meter of the HUD (the blue bar below the crosshair, `lyshineui/hud2024/vitals/
  manameter`; layout `ManaMeter` at x 0, y -382, 174 by 18) reads `ManaPercent` from this
  component, not from the mana fields of the vitals state. Its start value is -1, and it has
  the opacity 0 only at 100 percent (`opacityAtFull`). Before user test 1 the backend sent no
  mana state: the bar showed in each picture of each stage (open problem 10 of stage 3.4c).
- Live proof with `tools/world fragment` (slot 47, type 1652): 50 of 100 shows half of the
  bar, 100 of 100 hides it. The backend sends the state to the owner in its first record and
  at each change of the mana, with the values of `Entity::vitals` (the same values as group 1
  of the vitals state) and the two rates 0: the client changes no value itself. The maximum
  is the mana of the progression tables (`baseMana`) when the tables have it. The remote
  player slice has no mana component: an observer gets no fragment.
- **The maximum of the client** (stage 3.5b.2; live proof of the final run, static proof of
  the two functions). `ManaPercent` is the current value of the state divided by the maximum
  of the client (0x691F190: the object at component +0x140, function +0x20), not by field 1.
  The client maximum is its stat MaxMana (0x31; 0x676B310 also reads the stat 0x30, the
  regeneration). At a change of the stat the client scales the current value by the new
  maximum over the old one (0x676B310): a full bar stays full, and the meter shows the change
  only at the next mana state. The client computes the stat from the maximum of the **first**
  mana state (field 1) and its own MaxMana parts: the threshold abilities of its attributes
  (Focus 100: `Foc_Bonus_100_1`, MaxMana 0.1, ManaRate 0.2), the perks, the effects. Later
  values of field 1 change nothing (fragments 110 of 100 and 55 of 110 or 100: the same bar).
  Proof: a first state with the maximum 100 gave the client the maximum 110 (100 of 100: the
  bar at 91 percent; 110 of 110 hides it); a first state with 110 gave 121 (110 of 110: the
  bar at 91 percent; 121 of 121 hides it; 105 and 115 probes). ManaMax of the vitals state
  (group 1, field 3) changed nothing in the probes: the backend sends the base there too.
- The backend (stage 3.5b.2, `world/progress.cpp`, `server/rep/states/mana_state.cpp`): the
  current value has the parts (100 x (1 + 0.1) = 110 of 110 with Focus 100 or more), field 1
  and ManaMax of the vitals state have the base maximum (`Vitals::manaBase`, 100), and the
  regeneration rate is the base rate times 1 plus the ManaRate parts (2 x 1.2 = 2.4). Live:
  full mana hides the meter; a Divine Embrace (25) shows 85 of 110; the meter is hidden again
  at the full value (`~/Games/soulwarden/work/evidence/loot-bags/implement/final/`,
  `mana-proof-sheet.png`, `mana-spend-regen.mp4`). Test case MN1.
- **The prediction of the client** (stage 2.2: the open point of stage 3.5b.2; static proof, the analysis
  `~/Games/soulwarden/work/evidence/world-time/implement/mana/MANA-ANALYSIS.md`; live proof). The Mana component has two copies
  of its four values: the predicted copy A (component +0xB0) and the copy of the server B (+0xC8). The mana state writes only B
  and the meter one time (apply 0x683B740). The regeneration (0x676B8E0, in each movement step through 0x676B310) changes only A,
  and the meter follows A only when A changes. The client copies B into A only at a movement state of the server whose id is the
  number of a client input (0x486D660): the backend never sends one (4.6). Thus with the state alone the meter stood still during
  the regeneration (stage 3.5b.2). The message that sets A:

  ```text
  ManaComponentClientFacet_ClientForceSyncServerState (type 4289, reader 0x656DF80, handler 0x656DE10 -> 0x67A6940):
    target:u64 entity:u64        the Mana facet: entity "Stats" of the player slice, component 98C89E858C53376B (slice-ids.json)
    current:f32 maximum:f32 delay:f32 rate:f32      big-endian; maximum: the base maximum; delay: seconds; rate: a factor
  ```

  The client gains GetStatRegenRate(2) x stat 0x30 x rate in each second after the delay. Live: with `tools/world message` 50,
  100, 0, 1.0 the meter filled at 2.0 a second; with the factor 1.2 at 2.37 a second, and it hid at the full value 25 s later (the
  rate of the server with Focus 100: 2 x 1.2). Thus `rate` is the factor of the server (1 + the ManaRate parts). Message 4291
  (ClientSyncServerState) does nothing in this build. The backend sends 4289 to the owner after each mana state that is not in the
  first record (a spend, a gain, the full value, a change of the maximum; `server/rep/mana_messages.h`): live, a spend of 50 (a status
  of the data) showed 60 of 110, and the meter filled to the full value in 21 s and hid. Test case MN2.

`MB::PlayerAppearanceComponentReplicatedState` has 1 group with 12 fields of one byte each
(indexes into the character creation databases of the client: gender, race, skin tone,
hairstyle, facial hair, hair color, facial hair color, eye color, face mark, scar, tattoo,
tattoo color). For a character that the client created, the backend converts the names of
its creation data to these indexes (2.2.5). Result: the character has the body type, the
skin tone, and the hair of the creation screen. For a character with no creation data (a
seeded character), or with no appearance tables, the backend sends the default appearance:
gender 0 (male), race 0, skin tone 3, hairstyle 1, and 0 for the other fields. Without this
fragment the mesh and the default skins load, and the body is white (no skin material).
The clothing and the weapons of a character are its equipment: the paperdoll state (4.11).

### 4.6 Movement of the local player (behavioral proof)

The client moves its character itself (the client facet of the movement component, tick
0x486C740, 30 Hz) and sends its input to the server (4.7). With the position report of the
game hook, each input report also has the position of the character (4.7). The server must do
these things:

- **Send a movement state at least each 2 seconds.** The client keeps the time of the last
  accepted movement state (facet +0xAA8). When it is older than
  `javelin.alc-server-timeout-for-input` (2000 ms in the live client, variable at 0x9F20EE0),
  the client clears the actions of each new input (0x2A231D0). The backend sends a movement
  state with a new `timeAnchor` each 200 ms (the heartbeat state). With it the function
  0x2A231D0 is not called (trace) and the inputs keep their actions.
- **Send an id that is never the number of an input of the client** (the id rule, below).
  `idAbs` and `idRel` are, for the original server, the number of the last input that the
  server applied. The client compares the server position with its own state for that
  input and corrects itself (0x486D660). The backend has no movement simulation: its state
  must find no input.
- **Send the movement permissions.** The client copies 4 bits of the movement state into
  each input (0x2A2D200), together with the permissions from its status effects. The actions
  of the character test them (`IfCanNav` 0x2BFB780, `IfCanRun` 0x2BFB7F0, `IfCanSprint`
  0x2BFB820, `IfCanDodge` 0x2BFB750). Without "can navigate" the character stays in its idle
  action: the keys give actions in the input, but the position does not change.

  | Field | Bit | Permission |
  | --- | --- | --- |
  | `dataBits` | 0x40 | can sprint |
  | `dataBits` | 0x80 | can run |
  | `moreDataBits` | 0x01 | can navigate |
  | `moreDataBits` | 0x02 | can dodge |
  | `dataBits` | 0x01 | the entity is a camera lock target (stage 3.3b-M2b, 4.34) |
  | `dataBits` | 0x02 | the entity is a homing target (stage 3.3b-M2b, 4.34) |

  `dataBits` bit 0x20 is the 20 Hz mode. `moreDataBits` bits 0x04 and 0x08 set two more bits
  of the input (function not known). The backend sends `dataBits` 0xC0 and `moreDataBits`
  0x03.
- **Send `rotation` and `lookDir` in the spawn state.** The client does not initialize these
  two values of the state object: a memory read of the server state (facet +0x430, look
  direction at +0x440, rotation at +0x470) showed bytes of old text. 2 of 8 runs without the
  two fields failed. One time the loading screen did not end: the yaw of the camera was not a
  number, and the main thread stayed in the loop at 0x104E790 (read with a debugger). One
  time the camera pointed down from above and the ground check below needed 2 minutes. The
  random rotation is the probable cause (no trace proves the link). The backend sends rotation
  `0x6E` (the character looks north) and look direction `0x3A`; the memory read then shows
  (0, 0, 0, 1) and (0, 1, 0, 0) (1 run).
- **Spawn near the ground.** After the loading screen the client looks for collision below
  the position of the server state (query from terrain height + 0.5 m down, end of the tick
  0x486C740) and sets facet +0x1690. Before that the character does not fall and does not
  move. A spawn 160 m above the ground gives a fall of 6 seconds. A spawn 0.3 m to 1.7 m
  above the ground gives a short drop.
- **The spawn height is the saved height plus 0.5 m** (`world::kSpawnLift`), also for a saved
  position that is exact. Behavioral proof (stage 2.3a, live check 10 of `position.md`: 36
  world entries at three places: a street, a raised floor of a porch 1.7 m above the street,
  and the stairs of that porch). The value is the height of the character at rest after the
  world entry, minus its height at rest before:

  | Spawn height above the exact height | Street | Raised floor | Stairs | What the client does |
  | --- | --- | --- | --- | --- |
  | 0.5 m | -0.016 m | -0.039 m | -0.025 m | The character falls to the ground. Each of 3 entries gives the same height. |
  | 0.1 m | +0.063 m | -0.013 m | +0.097 m | At two places the character does not fall: it stays above the ground (ground flag 1) until it moves. A saved height would be higher after each entry. |
  | 0.0 m | -0.005 m | -0.004 m | -0.002 m | The character stays at the height of the state. No fall through the ground in 9 entries. |

  With 0.5 m and no change of the row between the entries (the product behavior), four
  entries in sequence gave the heights 138.201, 138.195, 138.194, and 138.199 m at the street.
  At the raised floor three entries gave 139.503 m, and the fourth one ended 0.39 m higher,
  beside the wall of the house (the cause was not examined: the fall of 0.5 m moves the
  character 0.015 m to 0.02 m in the horizontal plane each time). A spawn height with no lift
  is a candidate for an exact position only: it needs a mark in the row that the position
  is exact, because a position of the camera estimate needs the 0.5 m.

A field of a state keeps its value in the client until the server sends it again. Thus the
heartbeat state each 200 ms has only the time anchor and the two id fields, and the other
fields come one time with the spawn.

**The id rule of the movement state** (static proof for the client code; behavioral proof for
the rule, stage 2.3a).

- The input number of the client has 16 bits (facet +0x1622, increment at 0x486C903). It
  starts at 1 for each world entry and is 0 again after 65,536 simulation steps: 36.4 minutes
  at 30 steps each second.
- The client keeps its last inputs and predicted states in two rings (facet +0x380 and
  +0x3B0). For each server state it looks for the predicted state with the same id, with an
  exact compare of the 16 bits (0x485A460; the id of a state is at state +8). The id of a
  server state is `idAbs` (1 byte, the high 8 bits, reader 0x2A42DA0) and `idRel` (1 byte, the
  low 8 bits, reader 0x2A42D40), put together at 0x2A2ACD0. A state with no id fields has the
  id 0 until the server sends one.
- Found: the client stores "server position minus predicted position" and moves its
  character by it (0x486EF60, reason text `ApplyCorrection`), or it does a rollback and a
  replay (0x486B280, 0x486E170). Not found: it does nothing.
- The fault of a backend with no id: for 2 s after the wrap of the input number, the rings
  have a predicted state with the id 0. Each heartbeat state then goes to "found", with the
  position of the last `worldPosAbs`, which is the spawn position of the session: the client
  pulls its character to the spawn position.
- The rule. `S` is the newest input number that the session got from its client (0 before
  the first input report). Each heartbeat state has

  ```text
  id = 0x8000   when S is in 0xC000..0xFFFF or in 0x0000..0x3FFF
  id = 0x0000   when S is in 0x4000..0xBFFF
  (the same: id = ((S + 0x4000) & 0x8000) ^ 0x8000)
  heartbeat state with the id 0x8000: 01 f9 00 00 80 00 40  00  80  <timeAnchor: 8 bytes>
       01 = group 0; f9 00 00 80 00 40 = field mask with the bits 0 (idRel), 25 (idAbs), 40 (timeAnchor)
  ```

  The id is 0x4000 (16,384 steps, 9.1 minutes) or more from `S` for each of the 65,536 values
  of `S` (test case PR7), thus it is not the number of one of the 60 states of a ring. The
  id changes 2 times in 36.4 minutes. A session starts with the id 0x8000. The full movement
  state of the owner (the spawn) has no id fields. `S` and the id are values of the session:
  they describe the ring of one client (`movement_state_id` in
  `server/rep/states/movement_state.cpp`). An observer gets no id.
- Live results (stage 2.3a; the input number was written in the client memory with the
  harness, `tools/hx 'write FACET+0x1622 f0ff'`, after a walk of 20 m from the spawn; the id of
  the last server state is at facet +0x430 +8, and the id of the predicted state at facet
  +0xAE0 +8):

  | Run | Write | Result |
  | --- | --- | --- |
  | Rule on | 0xFFF0 (the number is 0 after 0.5 s) | The client has the state id 0x8000 before and after the wrap. Position change in 6 s: 0.000 m. |
  | Rule on | 0x3FF0 | The state id of the client changes from 0x8000 to 0x0000 when its input number is 0x4000. Position change in 6 s: 0.000 m. |
  | Rule on | 0xBFF0 | The state id of the client changes from 0x0000 to 0x8000 when its input number is 0xC000. Position change in 6 s: 0.000 m. |
  | Rule off (a backend build with no id fields: the state id is 0) | 0xFFF0 | 0.8 s after the write, with the input number 0x0009, the character is at the spawn position of the session: a jump of 20.36 m with no input. It stays there. |

  With the rule, the session start (the id 0x8000 from the first heartbeat state), a walk, a
  jump, and a dodge work as before, and the trace point 0x486E170 (the replay of a rollback)
  has no hit in a session with the three writes. In the run with the rule off the trace point
  has no hit too: the client moved its character with no replay. A session of 40 minutes was
  not done.

Results with these fields (memory read of the predicted state, facet +0xAE0, position at
+0x460): W for 3 seconds moves the character 16.5 m, A and D move it to the side at the same
speed, the space key gives a jump of 1.2 m, and a pointer movement turns the camera and the
direction of W. A session of 3.5 minutes with movement had no disconnect.

The appearance state is not necessary for movement (a run without it: W for 2 seconds moved
the character 10.8 m). The vitals state and the appearance state do not make the character
move without the permissions. A run with the permissions and without the vitals state was
not done.

### 4.7 Input report of the client, and the position of the character (static proof, checked against captures; behavioral proof for the position field)

`Javelin::ClientMessages::ActionListComponentServerFacet_ReceiveInputFromClient`, type index
1589, carrier channel 1 (unreliable), 30 each second.

```text
body   := component:u64 entity:u64 header count:u8 entry*        (readers 0x4867C70, 0x2A2E490)
header := u8 u8 u8 u8 size64 size size u8 [flag:u8, size if set] u64 u8 size size size u8
          pairs:size (size size)* size64 size64 size size        (reader 0x2A2E6B0)
entry  := mask:3 bytes (little-endian) sequence:u16
          [flags:size64 if mask bit 0] [u8 if bit 1: permissions] [u8 if bit 2] [u8 if bit 3]
          [u16 if bit 4: 0 to 10; 10 in a fall, near 0 on the ground]
          actions:u8 (size64 for each pair of action words)
          axes:4 bytes
          [f32 if bit 5: step time] [yaw:f32 pitch:f32 if bit 6] [camera position if bit 7]
          target                                                  (reader 0x2A25D90)
          [interaction block if mask bit 8 (4.35)]                (reader 0x35EBC30)
          [size size size if flags bit 0: the action report]
          bit := 9
          [flags bit 11: position if mask bit 9; bit := 10]
          [flags bit 8: position if mask bit `bit`; 16 bytes and u64 if mask bit `bit`+1; bit += 2]
          [u8 if mask bit `bit`: the value of entry +0x380]; bit += 1
          [that value (of this entry, or of the entry before it) is not 0:
           position if mask bit `bit`; 32 bytes if mask bit `bit`+1; bit += 2]
          [flags bit 14: 8 bytes if mask bit `bit`; bit += 1]
          count:size hit*                the hits of a projectile with no flight (list 1)
          count:size shot*               the shots (list 2)
          count:size contact*            the melee contacts (list 3)
          [flags bit 15: position if mask bit `bit`: THE POSITION OF THE CHARACTER; bit += 1]
          [half float x3 if mask bit `bit`: the velocity]; bit += 1
          [u64 if flags bit 16] [u8 if flags bit 18]
          [u8 if mask bit `bit`] [u32 if mask bit `bit`+1]
          [half float if flags bit 0 and mask bit `bit`+2]
target := kind:u8 [sub:u8 position u16 u16 u16 if the kind is not 3]      (reader 0x416EB00)
position := x:f32 y:f32 height:u16      height in meters = -100 + value / 65535 * 1100
object := present:u8 [16 bytes u64 u64 | u8 u8 u64 u32 | text u32 u32 | f32 flag | text text
          text text | f32 flag | 12 bytes and 12 bytes if that flag is 1 | flag u32 u8 u32 |
          u32 u32 u32 u32 | size64 u32 size text | u32 u8 flag | u32 text u32 u64 | u32 flag]
          a text is size and that number of bytes; a flag is one byte, 0 or 1
hit     := 3 f32 (origin) 3 f32 (point) reference(facet) reference(group) u64 u32 u32 u32 u32
           104 bytes                                              (readers 0x2A1A310, 0x2A195D0)
shot    := origin:3 f32 direction:3 f32 offset:3 f32 point:3 f32 (the hit near the muzzle), 16 bytes,
           u32, reference(group) (the shooter), reference(facet) (the receiver near the muzzle),
           reference(group), token:u64, u64, f32, f32, charge:f32, ammunition:u32, attack:u32, row:u32,
           flag, flag, category:u8, flag      192 bytes          (readers 0x2A1A0D0, 0x2A1A590; 4.27)
contact := provider:u32 reference(group) reference(facet) point:3 f32 normal:3 f32
           direction:3 f32          96 bytes                      (readers 0x2A19E60, 0x2A18490)
reference(group) := actor id:16 bytes G:u64            reference(facet) := the same and target:u64
```

- The entries are newest first. Each entry is a difference from the entry before it in the
  message, and the first one from a default entry. A live client sends an input in up to 5
  reports: a report has the newest entry and the 4 entries before it (a report in the world
  has 5 entries; the loss of a datagram loses no input). The items of the three lists are
  not a difference: an entry has only its own items. `tools/research/input_packet.py`
  decodes all fields of a capture, to the last byte of each report (stage 3.2a: each report
  of 5 runs decoded, 5,927 and 5,741 reports in the two longest ones; `--contacts` prints the
  entries with a list item).
- The keyboard gives action bits, not axes: the four axes are 0 with W, A, S, D held. The
  axes are the analog sticks.
- The position of mask bit 7 is the position of the **camera**, not of the character (same
  encoding as the world position of 4.3). The position of the character is a different field
  of the entry, which the client sends only with flag bit 15 (below). No other client message
  has the position of the character.
- The mask bits after bit 8 have no fixed number: a flag that adds a field takes its mask
  bits also when the field is not in the entry. With the flags of an entry of a client with
  the hook (0x20084F0) the position of the character has mask bit 10.
- Target data (a kind that is not 3) has 17 bytes after the kind. Behavioral proof (1 report
  of stage 2.3a, a step up the stairs of a porch): kind 0, sub kind 1, a world position 0.7 m
  in front of the character and 0.5 m above it, and the values 0, 19879, 0. The entry decodes
  with no byte left, and its position of the character is on the path. The backend log has
  one line for the first such report of a session.
- `yaw` turns the camera to the left from north (0 = north, the direction of +y). A positive
  `pitch` is down (reader 0x87B4A0; live check with 5 camera directions).
- The camera is behind the character in the direction of the camera (yaw and pitch), 0.5 m
  to the right of the character in the horizontal plane, and 1.65 m above its feet on the
  vertical axis of the world (behavioral proof: memory read of the character position
  against the camera of the input entry, with a pitch of 0, of 0.13 and 0.29 down, and of
  0.37 up: the difference was 0.01 m or less). The distance behind is 4.0 m at the start of
  each session. It is not a constant: an obstacle behind the camera makes it smaller (0.3 m
  to 3.2 m in live runs with a wall or a bush behind the camera), and the pointer wheel
  changes it. Thus the camera gives a line that has the character on it (the camera axis),
  not a point.
- The pointer wheel (behavioral proof: the camera of the input entries in 7 sessions): one
  step moves the camera 1.0 m along its axis, between 2.0 m and 8.0 m behind the character.
  The camera is at its new distance 0.4 s after the last step. A camera at 2.0 m goes to
  2.2 m when the character moves, and stays there (6 walks). The next steps of the wheel
  start from 2.2 m (3.2 m and 4.2 m in live runs).
- The action words of an entry (19 words of 32 bits in the world; bit 0 is the lowest bit of
  the first word) have one bit for each action name of the input profile of the client. The
  bit of a name is its position in the list of all names of the default profile and its
  includes (596 names of `libs/config/defaultprofile.xml`), in the sequence of the CRC-32
  values of the names (static proof: the list builder 0x6501F30, the writer 0x651C180;
  research file `combat-actions.md`). One key gives the bits of each name that the profile
  binds to it. Each entry has its own words (they are not a difference). The bits of the
  attack inputs and of the weapon keys are in 4.15. Bits in the captures (behavioral proof;
  the same in each session):

  | Input | Bits |
  | --- | --- |
  | No input, after the loading screen | 475, 497, 506, 569 at all times |
  | W | 14, 43, 416, 542 while the key is down (43 for one more input), and 133, 434 in the first input. 416 and 542 are not in the oldest captures. |
  | Pointer movement (camera turn) | 7, 107, 439 |
  | One step of the wheel, camera nearer | 27, 237, 392, 394, 530 for one input |
  | One step of the wheel, camera away | 51, 103, 219, 495, 510 for one input |

  An entry with action bits does not prove that the character moves. The bits of A, S, D, and
  the space key were not looked up. The backend reads bit 14 (forward key), and bits 27 and
  51 (wheel) of the newest entry of each report, and the bits of the attack inputs and of
  the weapon keys of each entry (4.15).
- Bit 222 (`character_collided`) is on while the character touches a creature (behavioral
  proof: on in the steps before a contact of an attack that moves the character into a
  wolf, and while a wolf plays its bite at the character).

**The position of the character: the position field** (behavioral proof, stage 2.3a; research
file `~/Games/soulwarden/work/research/position.md`; evidence
`~/Games/soulwarden/work/evidence/exact-position/implement/`).

- The client fills and sends entry +0xF0 only when bit 15 (0x8000) of the entry flags is set.
  The fill function of an entry (0x2A236C0; one caller, the tick of the local player, at
  0x486CA2E) sets the bit from the value "within a plot" of the housing component: it calls
  method +0x6F0 of the movement component interface (0x483BDE0) at 0x2A242C1 and writes the
  result to bit 15 (0x2A242C7 to 0x2A242DD). After the physics step of the same tick, the
  function 0x2A24700 copies the position of the predicted state to entry +0xF0 when the bit
  is set (0x2A2480D), and the entry writer sends it (0x2A25ACD). (Static proof.)
- **The game hook sets the bit** (`apps/game-hook/src/client/hooks/movement`): it replaces
  the 6 bytes of the call at 0x2A242C1 (`FF 90 F0 06 00 00`) with `B0 01 90 90 90 90`
  (`mov al, 1` and four `nop`). The code after the call reads only `al`. The hook compares
  the 37 bytes from 0x2A242BB with the code of the supported build first, and it changes
  nothing when they are different (one line in the hook log: `position report: OFF for this
  start of the game: CAUSE`). `SOULWARDEN_EXACT_POSITION=0` turns the change off, and
  `SOULWARDEN_EXACT_POSITION_FAULT=code` (only with `SOULWARDEN_HARNESS=1`) makes the hook compare with
  one wrong byte, for a test of a client build that is not the supported one. The
  backend sends no housing state: the value "within a plot" of the client stays 0, thus the
  camera, the compass, and the HUD of the client do not change, and the rollback and the
  correction of the client work as before (they read the housing value, not the flag).
- Live result: each input report with a camera position has the flags 0x20084F0 (or a value
  with more or fewer of the other bits: 0x20084F1, 0x84F0, 0x2008470, 0x1020084F0, ...; all
  have bit 15), a mask with bit 10, and the position of the character: 17,629 of 17,629
  reports with a camera in the sessions of one run, 1,317 of 1,317 in a second run, and 2,100
  of 2,100 in a run with the products of `dist` (`tools/research/input_packet.py
  DATA_DIR/captures --summary`). The reports of the loading screen (flags 0x1000) have no
  camera and no position. The first report after the loading screen has the spawn position
  (the character did not fall yet).
- The value is the position of the predicted state of the client (facet +0xAE0, position at
  +0x460). `x` and `y` are the 32-bit floats of the client. The height has a step of
  1100 / 65535 = 0.0168 m, and the client cuts it off: the value on the wire is 0 m to
  0.017 m below the height of the client.

  | Place (character at rest) | Client memory | Backend (entity) | Difference |
  | --- | --- | --- | --- |
  | Spawn | 8880.000 4250.008 138.217 | 8880.000 4250.008 138.212 | 0.000 m, height +0.005 m |
  | After a walk of 3 s (16.7 m) | 8863.302 4250.292 137.864 | 8863.302 4250.292 137.859 | 0.001 m, height +0.005 m |
  | After a jump | 8863.302 4250.293 137.836 | 8863.302 4250.293 137.826 | 0.000 m, height +0.010 m |
  | Against a wall, the camera looks at the wall | 8861.120 4241.802 139.541 | 8861.120 4241.802 139.538 | 0.000 m, height +0.003 m |
  | The same place, the wall is behind the camera (camera 0.03 m behind the character) | 8861.120 4241.802 139.541 | 8861.120 4241.802 139.538 | 0.000 m, height +0.003 m |

- A step with no physics step gives an entry with the flag and with no position field (the
  value of the entry is then the value of the default entry; static proof: no newest entry of
  the live runs had the flag with no field). This is not an error: the entity keeps its
  position.
- The object of mask bit 8 is the interaction block (4.35): live proof of stage 2.5a (a block of
  155 bytes at the key E of a lore page; an older entry with no bit 8 has the block of the entry
  before it). The fields of a value of entry +0x380 that is not 0 have static proof only: no
  entry of a live run had one of them. The first two lists have static proof only too (no
  ranged weapon in stage 3.2a). The third list has behavioral proof
  (4.15): an entry with a contact item has the position of the character too, and it
  decodes with no byte left.

**The reader of the backend** (`read_client_input` in `server/rep/client_input.cpp`) reads each
entry of a report to its end (the first one is a difference from the default entry, thus its
flags are 0 when it has no flags field; each later one has the flags and the value of entry
+0x380 of the entry before it when it does not send them). The newest entry gives the camera
and the position of the character. Each entry gives its attack data (`InputStep`): the three
attack inputs, the weapon key, the item counts of the first two lists, and the melee contacts.
A body that does not parse to the camera position gives no input. An entry that the reader
cannot read to its end (a body that ends, a list count that is larger than the body, a flag
of the object that is not 0 or 1) gives no position and no attack data, and the reader reads
no entry after it. This applies also to an entry that ends after its position field: the
reader reads the field before it finds the end, and it then gives no position (case HT12;
before stage 3.4c such an entry moved the character). The reader makes no list from a count: it compares the count with the
bytes that the body has (a count of 100,000 in a short body is refused).

**The check of a position report** (a design of this project: the client has no copy of the
check of the original server). The session gives each position to the world
(`World::report_position`, `world/position_report.h`). The world finds the entity from the
session (rule MP-5), checks the report, and keeps an accepted position in the entity: the
position index and the save rule are the same for each source of a position. A refused report
changes no value of the world, and the client gets no reply. The values of the client for the
check are in the session (`world::PositionTrack`: the last input number, the accepted
positions of the time window, the counts). The check, in this sequence:

1. **Sequence.** The input number is 1 to 0x7FFF above the last one of the session (16 bits,
   with the wrap). A report with a number that is not newer is not used (an old or a second
   datagram of the unreliable channel) and is not counted as refused. When each report of 2 s
   has a number that is not newer, the input numbers of the client started again, and the
   session takes the new sequence.
2. **Numbers.** `x`, `y`, and the height are finite. The height is not one of the two limit
   values of its 16 bits (0x0000 and 0xFFFF: the real height is then outside -100 m to
   1000 m). The position is in the limits of the world (`valid_position`).
3. **Speed.**
   - Peak: the horizontal distance from the last accepted position is not more than
     `kPeakSpeed` (20 m/s) times the time between the two reports.
   - Height: the rise from the last accepted position is not more than `kRiseSpeed` (20 m/s)
     times that time, and the fall is not more than `kFallSpeed` (120 m/s) times that time.
   - Height of more than one report: the rises of the accepted reports have one budget of
     time, and the falls have a second one (review of stage 3.4c, finding C2). An accepted
     report takes the time of its rise at `kRiseSpeed` from the first budget, and the time
     of its fall at `kFallSpeed` from the second one. A report that needs more time than its
     budget has is refused. Each budget is for a sum, not for the difference of two heights
     (review of stage 3.4a: with the difference of two heights, reports that went 4 m up and
     4 m down in turns passed with 399 m of rise at one clock value).
   - The world clock fills each budget, one time: a report gives each budget the time of
     the world clock from the last accepted report. The first accepted report of a session,
     and the first one after a travel, gives each budget 0.25 s (`kClockSlack`). Thus the
     rises of a session are never more than 20 m/s times the time of the world clock from
     its first accepted report, plus 5 m, for each sequence of input numbers. The falls are
     never more than 120 m/s times that time, plus 30 m. The rule before this one compared
     the totals of a window of 1 s with the client time of the window, or with its time of
     the world clock when that was longer. A client with slow input numbers got its rise
     for the time of the world clock, and then a second rise for the same time when its
     input numbers went on at one clock value: 437 m in 15 s (case PR12).
   - The limit of a budget is the time of one window (1 s), or the time that the client has
     in store when that is more. The client time of a report is the time of its input
     number from the first accepted report (1/30 s for each number), with two limits from
     the world clock: it is not more than the time of the world clock from that report plus
     0.25 s, and it is not more than 5 s (`kReportBacklog`) behind that value. The time in
     store is the time of the world clock plus 0.25 s, minus the client time of the last
     accepted report, and 5.25 s at most. Thus the time of a report is not the time at
     which its datagram came. Reports that come late and together (a delay of the network,
     a client that made no step for some frames) have their budget, because the time of the
     world clock before them is in store: five jumps of 1.2 m whose 120 reports come in
     12 ms after a delay of 4 s pass (case PR11; the rule with the time of arrival refused
     16 of them). A client cannot make more time with large input numbers: its client time
     stops at the time of the world clock plus 0.25 s, and each input number that it sends
     takes its time out of the store.
   - What a client with changed input numbers gets from this rule: 20 m/s of rise for the
     time of the world clock, one time. In one moment (one value of the world clock) it
     gets the rise of one window (20 m) at most when it sent its reports in time, and the
     rise of 5.25 s (105 m) at most when its input numbers were slower than the world clock
     for 5 s or more, or when it sent no report in that time. This is time that it did not
     use before: a rise in that time makes it smaller. The limit of the fall is 6 times
     these values.
   - A limit that stays: the horizontal window rule below has the time of arrival. After a
     delay of the network, the reports of a character that went more than 4 m in that time
     are refused, and the entity gets its position again some seconds later (the window
     rule permits 8 m/s, and the character goes 5.5 m/s).
   - The first report of a session has the height of the entity as its reference: 5 m up or
     down, plus the rise or the fall of the time from the first report.
   - Window: the horizontal distance from the oldest accepted position of the last 1 s (or
     from the last accepted one when it is older) is not more than `kWindowSpeed` (8 m/s)
     times the time between the two reports, plus `kWindowMargin` (2 m). The margin thus
     applies one time for a window, not one time for each report (30 reports each second).
   - The first report of a session: 5 m or less from the position that the entity has (the
     spawn position), or the window rule from the time of the first report.
   - After a travel of the character (`World::travel`, 4.16) the check starts again as at
     the start of a session: no accepted report, and the place of the travel is the
     reference of the next report, for the horizontal distance and for the height (5 m, or
     the window rule from the time of the first report after the travel). The client makes
     some reports before it gets the new position: they are not near the place of the
     travel, the world does not use them (verdict `beforeTravel`, not in the count of the
     refused reports), and the entity keeps the place of the travel.
   - The camera estimate after a travel (review of stage 3.4c, finding C1). The session
     keeps the place of the travel until its client confirms it: an accepted position
     report, or a camera estimate that is near the place. Until then the session does not
     follow a camera whose estimate is more than 15 m from the place in the horizontal
     plane, or more than 15 m above or below it. The time alone does not end this rule. It
     adds only the distance of a movement with the limits of this check (8 m/s in the
     horizontal plane, 20 m/s up, 120 m/s down), from the first camera after the travel:
     as with a position report, a client gets a different place only after the time of a
     walk to it. The rule before this one ended 2 s after the travel and compared only the
     horizontal distance: one report with no position field and with a camera at the old
     place, 2.1 s after a travel of 2 km, put the entity back, and the position report
     after it was accepted there (case TV6). After the client confirmed the place, the
     camera estimate has no speed check, as before a travel.
   - The time between two reports is the time of their input numbers (1/30 s for each
     step), but not less than the time of the world clock (a client with a slow picture
     makes longer steps) and not more than that time plus 0.25 s (reports that come
     together after a delay; a client gets no more time from a large input number).
   - A refusal is not permanent: the permitted distance becomes larger with the time, thus a
     client that stops is accepted again when its distance is in the limit.

The limits come from one live session of a character on foot with chest and leg armor and
a sword (stage 2.3a, criterion P7: a spawn 150 m above a street, a sprint of 23 m with a
dodge in it, two more dodges, a jump at rest, and two jumps in a sprint; 1,317 reports, none
refused, with the limits below and with the first limits). The session log has the values at
its end (`rep: position reports of the session: ...`):

| Value | Largest in the session | Limit |
| --- | --- | --- |
| Horizontal distance for one tick | 0.303 m (9.1 m/s, in a dodge; a sprint has 0.183 m) | 0.667 m (`kPeakSpeed` 20 m/s) |
| Horizontal distance for one window of 1 s | 7.15 m and 7.50 m (two sessions; a dodge in a sprint; a sprint has 5.5 m) | 10 m (`kWindowSpeed` 8 m/s and `kWindowMargin` 2 m) |
| Rise for one tick | 0.201 m (6.0 m/s, a jump; 0.218 m in a different session) | 0.667 m (`kRiseSpeed` 20 m/s) |
| Fall for one tick | 1.796 m (53.9 m/s, at the end of a fall of 150 m in 5.3 s) | 4 m (`kFallSpeed` 120 m/s: a fall has no largest speed in the client, and 120 m/s is the speed after 730 m) |

The check finds a position that is too far from the last one. It cannot find a position in a
wall or below the ground: that needs the collision data of the world. A character on a mount
needs a larger limit (the mount actions have 10 m/s and more; system 5.7).

One live refusal: in the run with the id rule off (4.6) the client moved its character 20.36 m
in one tick. The world refused the report (`rep: position report refused (too fast): input 44,
...; the entity keeps its position ...`) and accepted the reports again when the window speed
permitted the distance (2.3 s later, with the character at rest).

**The two position sources of a session.** A session uses the position field when its client
sends it (log line `rep: position source: the position field of the input reports, the exact
position`). A report with no field keeps the last position. When no report had the field for
1 s (a client with no hook change, the switch `SOULWARDEN_EXACT_POSITION=0`, a different client
build, or entries that the reader cannot read), the session uses the camera estimate (log line
`rep: position source: the camera estimate`, with the cause). A change of the source in a
session gives one log line (16 lines at most). The log line of each second names the source:
`rep: input N camera X Y Z look YAW PITCH position X Y Z (client report)` or `(camera
estimate)`.

**The camera estimate** (the fallback; `RepSession::track_position`). The spawn position is
exact. The backend keeps the known position while the character is at rest, and follows an
estimate from the camera while the character moves:

- **At rest.** The camera axis goes through the known position: the horizontal distance of
  the position from the axis is 0.75 m at most, and the position is not more than 0.75 m
  behind the camera and not more than 3 m farther in front of it than the camera distance.
  A camera turn, an obstacle behind the camera, and a jump do not change the position.
- **The wheel at rest.** The backend takes the movement of the camera along its axis, from
  the first wheel input to 0.7 s after the last one, as the change of the camera distance
  (log line `rep: camera distance ...`). The position does not change. The backend cannot
  measure a wheel input while the character moves: it then uses 4.0 m again.
- **Movement.** The axis leaves the known position. Or: the forward key is down, the camera
  moves, and no obstacle is behind the camera (the camera is not more than 0.5 m nearer to
  the known position than the camera distance). An obstacle holds the camera while the
  character goes away from it, thus the second rule does not apply with an obstacle. The
  position then follows the point of the camera axis at the camera distance (2.2 m at
  least, 4.0 m at most) until the camera is at rest for 0.5 s.
- **The limit of 4.0 m.** An obstacle makes the camera nearer, never farther. An estimate
  with a distance that is too large is a position in front of the character, which can be
  in a wall. An estimate with a distance that is too small is a position between the camera
  and the character.

The backend writes the position to the store each 5 seconds (during a movement only a
position that is 0.5 m or more from the saved position) and when the session ends. The next
login uses it as the spawn position, 0.5 m higher. This is the same for the two sources.

Results of the camera estimate (memory read of the character position against the row,
stage 0.4):

- A character that did not move kept its row with a wall 0.3 m behind the camera, with
  camera turns to the side, up, and down (camera not farther than 4.0 m), with a jump, and
  with wheel inputs (nearer and away, 9 inputs in 4 sessions).
- After a walk with free space behind the camera at the end, and the camera not farther
  than 4.0 m, the row was 0.03 m or less from the character in the horizontal plane, and
  0.13 m or less in height: 14 walks of 2 m to 17 m with the camera at 4.0 m (one of them
  a walk straight forward of 2.2 m, one with the camera turned down, one with the camera
  turned up), and 2 walks with the camera at 2.2 m and at 3.2 m.
- After a walk with the camera at 8.0 m, and after a walk with a wheel input in it (camera
  at 7.0 m), the row was behind the character by the difference to 4.0 m (4.0 m and 3.0 m).
- The backend code with the input reports of the session of an earlier check (a replay at
  the speed of the client) gives the row `8862.29 4243.18` after a walk with the camera at
  2.2 m. The memory read of that session was `8862.29 4243.18`, and its row was 1.7 m in
  front of the character.

The limits of the camera estimate are in section 5.

### 4.8 Client checks on the way to the world

- State `WaitingForPlayerSpawn` to `InGame` (0x644A070): level load flag and a local player in
  the player registry (0x1026080, set through 0x6928D80).
- Loading screen watchdog (0x100FA20, "Timed out waiting for world data" after 45 s): needs
  the terrain start flag (game +0x8C8), a connected local player, and the world streaming
  manager in an idle state (0x676BE70) for 30 frames.
- The world streaming manager (update 0x68A55F0) ignores an observer at the map origin.

### 4.9 Entity model of the backend, owner view and observer view, remote player

The model is a design of this project (roadmap, rules MP-1 to MP-8). The statements about
the client have their evidence level. Research files:
`~/Games/soulwarden/work/research/entities.md`, `ids.md`, `multiplayer-foundation.md`. Evidence of
the live runs: `~/Games/soulwarden/work/evidence/world-model/implement/`.

**Entity table.** The world object (`apps/backend/src/world/world.h`) has one entity for
each character that is in the world, with the entity group id as its key and an index by
the character id. An entity has the actor id, the character id and its account, the session
that plays it (0: none), the position, and the data of each component state (name,
appearance, vitals, movement permissions, action) with the world revision of its last
change. It has no wire bytes.

- A session enters with a character (`World::enter`): the world reads the rows of the
  character one time and makes the entity. The session reads the name, the spawn, the
  appearance, and the vitals from the entity.
- The position is world state. The session of the owner gives the world each position that
  its client reports (`World::report_position`), and the world checks it before it keeps it
  (4.7). For a client that sends no position, the session estimates the position from the
  camera and gives the estimate to the world (`World::set_position`). The world keeps the
  position in a position index (a grid of cells of 64 m) and writes the row: each 5 s when it
  moved (the rule of 4.7), and when the session leaves. A position that is not saved goes to
  the row 5 s after the last write also when the client sends no input.
- A session that ends leaves the world (`World::leave`): the world writes the position and
  removes the entity. An entity is in the world only while a session plays it, or while a
  development command holds it (below).
- One path for a change (rule MP-2): a system changes the data of an entity and marks the
  component state (`World::touch`). The replication step of each session that has the entity
  in its interest table sends the state in its view. `World::report_position` and
  `World::set_position` do not mark the movement state: the client of the owner moves its
  character itself, and its own position (or the estimate of the backend) must not come back
  to it as a new position. Thus an observer gets the position of an entity with its record
  only: the movement of a remote player is not in the backend (system 2.3, later stages).

**The session as an observer.** A session (`RepSession`) has transport state (the bundle
sequence, the client context instance), the values of the input of its client (the newest
input number, the values for the check of its position reports, and the state of the camera
estimate), and an interest table: entity to interest number and the world revision of its
last record. Its own
character has the number 1. Each other entity gets the lowest free number from 2. A number
is free again only after its stop went out. Each tick, the session sends one bundle for each
entity whose component states changed after its last record (the full record at first), and
the heartbeat state of the movement component (a new time anchor and the id of the rule
of 4.6) for its own character each 200 ms.

**Interest.** With the option `--observe-players` (launcher variable
`SOULWARDEN_OBSERVE_PLAYERS=1`; off by default; `tools/dev-run.sh` turns it on), a session
compares the entities near its character with its table each 500 ms. An entity that is 100 m
or less away (horizontal distance) comes into the table and gets its record. An entity that
is more than 120 m away, or that is not in the world any more, goes out and gets a stop
(4.2). The 100 m are `m_sliceSpawnRadius` of the Aoi component of the player slice (game
data; its meaning is a guess); the 120 m are a decision of this project. The scan starts at
the first input report with a camera: the client then has its local player entity, which it
needs before the record of a different player (static proof 0x1747C60). With the option off,
a client gets only its own character. The option stays off for a normal start: the movement
of a remote player for an observer is not in the backend. Only the dash of an ability goes to
the observers (stage 3.3b-S4: the state `motion`, 4.26).

**Owner view and observer view (rule MP-3).** One encoder for each component state
(`apps/backend/src/server/rep/states/`) makes the bytes for one of two views: the owner (the
session that plays the character) and an observer (each other session).

| State (slot, type) | Owner view | Observer view | Evidence for the observer view |
| --- | --- | --- | --- |
| Metadata (0, 10) | `AssetId` of the player slice with sub id 2, `GdeRef` | The same. The client changes the asset to the remote player slice (sub id 100) itself. | Behavioral: entity group +0x100 is 100 for the entity of a different player and 2 for the own player (memory read, 3 runs) |
| Player (9, 3935) | Group 2: `characterId`, `characterName`, `playerConnected`. Group 1 (22 fields of the account and the session: `loginMatchId`, `accountIsLocked`, `sessionStartTimePoint`, ...) is for the owner only; the backend sends only field 6 `sessionStartWallClockTimePoint` and field 7 `sessionStartTimePoint` (stage 3.3b-S1, the wall clock pair: 4.24). | Group 2: the same three fields | Behavioral: player facet +0x318 is 2 (remote) for a character id that is not the character id of the login, and 1 for the own player; the name tag shows `characterName` |
| Movement (16, 11) | `worldPosAbs`, `rotation`, `lookDir`, `teleportAndMigrationId`, `timeAnchor`, `dataBits`, `moreDataBits`; then `timeAnchor`, `idRel`, and `idAbs` each 200 ms (the heartbeat state, 4.6) | `worldPosAbs`, `rotation`, `lookDir`, `teleportAndMigrationId`, `timeAnchor`, and the action of layer 0: `slayerStateId` (13), `slayerStateIdStarted` (14), `slayerSequenceId` (15), `slayerScriptLayers` (47). No `dataBits` and `moreDataBits` (the movement permissions for the input of the owner). One state, no later time anchor. | Behavioral (below) |
| Vitals (41, 15) | Group 0 (health, stamina, `vitalsId`, `maxHealth`) and group 1 (mana) | Group 0 only: the remote player slice has no mana component | Static (slice data); the record is accepted by the client |
| Appearance (12, 1195) | The 12 fields | The same | Behavioral: the body of the remote player has the appearance |
| Progression (70, 899) | Group 0: `level`, `bonusLevel`. Group 1: `experiencePoints`, `restedExp` (4.12) | Group 0 only: `level`, `bonusLevel` | Behavioral: the name tag of a remote player shows the level of the fragment ("30 Remote") |
| Attribute (27, 129) | `attributes`, `attributeBonuses`, `placingBonuses`, `persistentAttributeData` (4.12) | No fragment | Static (slice data): the remote player slice has no facet for the component |
| CategoricalProgression (65, 911), ProgressionPoint (67, 3681), Ability (46, 185) | The lists of the masteries and the trade skills, the pools and the nodes of the weapon trees, the ability bars (4.13) | No fragment | Static (slice data) |
| Currency (60, 3786) | `currency` (4.10) | No fragment | Static (slice data): the remote player slice has no facet for the component |
| Container (61, 1755) | `Container` (4.10): the full vector at first, then a journal with the slots that changed | No fragment | Static (slice data) |
| Paperdoll (62, 3183) | `isLocalPlayer`, each item with its slot and its durability (group 1, and `visiblePaperdollSlots` of group 2), `sheatheMap`, `activeMap` (4.11) | Group 2 only: `visiblePaperdollSlots` and `visibleItemVisualData` for the slots with a look (0 to 4, 24 to 29), `sheatheMap`, `activeMap`. No fragment for an entity with no such item. | Behavioral: a remote character shows its chest armor, leg armor, and sword, and a change of them (stage 1.4) |

An encoder of a state for the owner only has the mark `ownerOnly`: a session does not call
it for the observer view. An encoder of a state that can be larger than one carrier message
gives a list of fragments (`PartsEncoder`), which the session sends in their sequence, each
one in a record of its own (4.10). The paperdoll encoder is such an encoder with the two
views (4.11).

A state of a slot that the remote player slice does not have (45 of the 83 slots: inventory,
currency, objectives, attributes, cooldowns, ...) is an owner state: an observer must not get
it.

**Record of a remote player that stands** (behavioral proof, 3 live runs of stage 0.6b; the
bytes are in test case V1). Interest 2, 179 bytes, with the example values of the research
(actor id `60 x8 61 x8`, character id `00000000-0000-4000-8000-000000000002`, name `Remote`,
position 8882.0 4252.0 138.5, time anchor 1,000,000,000):

```text
02 05                                              interest 2, 5 fragments
00 0a  01 03                                       metadata: group 0, fields 0 and 1
       a6 60 ee eb eb c7 5c b7 be 6b ab 11 eb 83 17 31 00 00 00 02      AssetId, sub id 2
       60 60 60 60 60 60 60 60 61 61 61 61 61 61 61 61                  GdeRef
09 9f 3d  04 0b                                    player: group 2, fields 0, 1, 3
       00 24 <36 bytes of the character id> 06 52 65 6d 6f 74 65 01
0c ab 12  01 ff 00 00 03 01 00 00 00 1f 00 00 00 00 00                   appearance
10 0b  01 fc 00 7c 00 44 80 40                     movement: group 0, fields 11 to 15, 27, 31, 40, 47
       6e 3a                                       rotation, lookDir
       1f  00 01  b9 10                            slayerStateId 30 + 1, started 1, slayerSequenceId 1080 + 1
       46 0a c8 00 45 84 e0 00 37 81               worldPosAbs
       01                                          teleportAndMigrationId
       00 00 00 00 3b 9a ca 00                     timeAnchor
       04                                          slayerScriptLayers
29 0f  01 f3 44 7a 00 00 42 c8 00 00 44 7a 00 00 00 00 42 c8 00 00
       43 00 00 98 19 7a 65 03 e8                  vitals, group 0
```

The backend sends the fragments in the sequence 0, 9, 16, 41, 12: the client sorts them by
their slot (static proof 0x15CE270). A remote player with equipment has a sixth fragment, the
observer view of the paperdoll state (62, 3183; 4.11).

Results of the live checks of `entities.md` (section 11 of that file has the method of each
check):

| Check | Result | Proof |
| --- | --- | --- |
| L1, a second entity group starts | Pass. `0x178DB00` has one call for each record with a metadata state. | Trace: 2 calls after the own record and one remote record; 8 calls after 7 remote records |
| L2, the client takes the remote slice | Pass, with the sub id 2 from the server: the asset of the new group has the sub id 100, and the game log names `slices/remoteplayer.dynamicslice`. | Memory read of group +0xF0 and +0x100 |
| L3, the entity is a remote player | Pass. Player facet +0x318 is 2 for the new entity and 1 for the own player. The client state stays `InGame`. | Memory read |
| L4, the body and the name tag show | **Fail with the record of the research (172 bytes), pass with the action fields.** With the five states of the research the client has the entity at its position (root transform 8882.00 4252.00 138.12, a shown state time), and it draws no body and no name tag. The body and the name tag show at once when the movement state has `slayerSequenceId` of layer 0 (value 1080, the sequence of the own character at rest) and `slayerScriptLayers` (4). Neither field alone is sufficient, and `slayerStateId` and `slayerStateIdStarted` are not necessary (7 variants in one run, with the development command `tools/world fragment`). No effect: a paperdoll state with `loadoutSwapIncrement`, a stealth state with `m_isStealthed` 0, a name tag state with `replicatedHideLevel` 0, `dataBits` and `moreDataBits`. | Screenshots `live-check-L4-*`, `W4-*` |
| L5, the character stays with no later state | Pass. The body and the name tag are at the same place 63 s after the last state of the entity. | Screenshot `live-check-L5-*` |
| L6, the show time | Pass. Movement facet +0x1DA8 (the time of the state that the client shows) is not 0, and it is equal to the time of the newest ring state (+0x670): 187,864,134,322 microseconds, a value of the monotonic clock of the computer, which the backend writes into the time anchor. Thus the client shows a state with the anchor of the backend clock: no other anchor rule is necessary. | Memory read (one run) |
| L7, stop removes the entity | Pass. One call of `0x1733A20` and of `0x178EC80` for each stop; the body is gone in the next screenshot; the session continues. | Trace, screenshots `live-check-L7-*`, `W4-3-*` |
| L8, the same number again | Pass. After the stop, a new record with interest 2 starts a new entity group. | Trace, screenshot `W4-4-*` |
| L12, sub id 100 from the server | Not done: L2 passed, thus the observer constant is 2 (the decision rule of the research). | |
| L14, configuration of the asset change | Pass. The byte at image 0x9E8FAEC is 0, and the pointer at streamer +0xC0 points to the entity id of the local player (`544F5A4100000002`). | Memory read |
| L15, clock of the remote state | Not necessary (L6 passed). | |
| L17, remote character and the ground | The client does not move a remote character to the ground: an entity 2 m above the ground stays in the air. A position 0.3 m above the ground goes to the ground (138.50 became 138.12). Thus the position of a remote character must be exact (system 2.3). | Screenshot `live-check-L17-*`, memory read |
| L18, a full record a second time | Pass. One body and one name tag at the same place; the game log gets the warning `StateReplication: Received repeated metadata`. | Screenshot `W4-5-*`, game log |
| L9, L10, L11, L13, L16 | Not done (the `ReplicationControl` form, a sequence gap, a creature, an odd first byte, a stop while the slice loads): they are not necessary for a remote player that stands. They belong to system 2.4. | |

More facts of the live runs:

- The game log has 26 lines `Slice: Couldn't get Transform component on the slice!` for each
  instance of the remote player slice (function 0x17880B0, which looks for the root entity:
  one line for each entity of the slice with no transform component). They have no effect.
- The numbers 30 (state), 1080 (sequence), and 4 (layers) are ids of the action list of the
  player in the supported client build. They are the values of layer 0 of the own character
  at rest (memory read of the predicted state of the movement facet: `30/1/1080`; layer 1
  has the state 44 and no sequence). The backend sends them as constants for each character
  (`world::ActionState`) until the action system (system 3.1) gives each character its
  action. A remote character thus stands in its idle animation at all times, but in the dash
  of an ability: from stage 3.3b-S4 the dash of Charge gives the state 7 with the sequence 59,
  then 2312 (the swing), the rotation of the body in the direction of the dash, and the place
  at each accepted report of the client, at most each 50 ms (live L7-D, proof P5b; 4.26).
- Wire form of the action fields (static proof `combat-actions.md`, behavioral proof for the
  four fields above): `slayerStateId` and `slayerSequenceId` are numbers in the `size` code
  with the value plus 1 (0 is "none"), `slayerStateIdStarted` is `u16`, `slayerScriptLayers`
  is `u8`.
- The client calls the player function 0x6769980 for the player component of each player,
  also of a remote one (return address 0x6876D32). Only the call from the player registry
  (return address 0x6928E1A) is for the local player: `tools/research/player_position.py`
  uses that one.

**Development commands** (`tools/world`; routes `/v1/dev/world` of the backend, only with
the option `--dev-commands`, which needs `SOULWARDEN_HARNESS=1`). `spawn` puts a character of the
database into the world as an entity with no session at a set position, `remove` takes it
out, `touch` sends its full record again, and `fragment` gives an entity one state fragment
with bytes of the caller (a live check of a state type that has no encoder). A session of
the character takes an entity that has no session.

### 4.10 Items and coin: container state, currency state, item requests

Research files: `~/Games/soulwarden/work/research/inventory.md` (static proof, checked by a second
agent). Evidence of the live runs of stage 1.3:
`~/Games/soulwarden/work/evidence/inventory/implement/`. Each statement below has its evidence
level. "Behavioral proof" is a live run of stage 1.3 with the client build of this document.

**Components** (slots of `slices/player.dynamicslice`, entity "Inventory"):

| Slot | Component | State type | Type index | Evidence |
| ---: | --- | --- | ---: | --- |
| 60 | Currency (the coin) | `MB::CurrencyComponentReplicatedState` | 3786 | behavioral proof |
| 61 | Container (the bag) | `MB::ContainerComponentReplicatedState` | 1755 | behavioral proof |

The two states have the common state form (4.3). `slices/remoteplayer.dynamicslice` has no
facet for these components: the two states are for the owner only, and an observer gets no
fragment of them (game data; the backend sends none, test case B6).

**Item index list of the client** (static proof for the rule, behavioral proof for the
result). An item on the wire is not an id. It is an index into a list that the client makes
from its data tables at its start (`ItemDataManager::CacheAllItemDataTables` 0x4428B00):

```text
entry 0 = no item
for each data table of the type "MasterItemDefinitions", in the byte order of the table name
of the file header (not the file name; upper-case letters and "_" are before lower-case
letters):
    for each row in file order, not a row with ItemType "HousingItem":
        the next entry = CRC-32 of the ItemID text, with the letters A to Z in lower case
then the tables "HouseItems" and "HouseItemsMTX" (type "VariationData"), in this sequence:
    for each row in file order, not the row "DEFAULTS", not a row with ExcludeFromGame:
        the next entry = CRC-32 of the HouseItemID text
```

- The supported build has 43 item tables (45,533 rows) and 2 house tables (1,082 and 16 rows
  that get an entry). The list has 46,632 entries.
- Behavioral proof: `tools/research/item_index.py` read the list from the memory of the live
  client (the pointer at image +0xA327838, then the pointer at that address, then the begin
  and the end of the list at manager +0x3C8 and +0x3D0). Each of the 46,632 entries is equal
  to the entry that `tools/gamedata/items.sh` computes from the tables. The client has the
  list from the main menu on.
- Examples: `1hSwordT1` has the index 5278 (CRC `0x98EDDB41`), `OreT1` 2956 (`0x48E7CA95`),
  `PotionHealthT1` 4880.
- A wrong index gives no log line: the slot is empty, or it has a different item.
- The backend reads the list from the game data file `item-catalog.json`
  (`apps/backend/src/gamedata/item_catalog.h`). The database has the `ItemID` text of an item,
  and the backend finds the index at each load of a bag.

**Container state** (type 1755; constructor 0x48EAB40; apply function of the facet
0x48D5EC0). One group with 5 fields. The backend sends field 0 only.

| Field | Name | Value | Evidence |
| ---: | --- | --- | --- |
| 0 | `Container` | replicated vector of items (below; reader 0x40E8F30) | behavioral proof |
| 1 | `Item Class` | `count:size`, then `count` times `u64` | static proof, not sent |
| 2 | `Bonus Max Encumbrance` | `u32` | static proof, not sent |
| 3 | `Can Transfer items` | 1 byte | static proof, not sent |
| 4 | `Container Was Emptied` | 1 byte | static proof, not sent |

```text
Container := changes:size
             changes == 0:  version count:size item*count         the full vector
             changes  > 0:  group*                                 the journal
group     := mask:u8 entry*         one group for each 8 entries; bit N is for entry N
entry     := index:size64 version [item when the mask bit is 1]
version   := has:u8 [sequence:size64 when has is 1]               (reader 0x61ACCA0)
```

- One element is one slot of the bag. The length of the vector is the slot count. The client
  reads 500 elements at most (static proof 0x48EA9C0).
- The vector of the client is empty at the start: the first value must be the full vector.
- A journal entry with the mask bit 1 sets the element at `index`. When `index` is the
  length of the vector, the client adds the element at the end. Behavioral proof for the
  two cases, for a journal with 1 entry, and for a journal with 16 entries in two groups.
- A journal entry with the mask bit 0 erases an element and moves the elements after it
  (static proof). The backend does not use it: a slot that loses its item gets the empty
  element (behavioral proof: the slot is empty, and the weight of the bag goes down).
- The backend sends `has = 0` in each version. The client then applies each entry
  (behavioral proof). The sequence rule of the client for `has = 1` is in `inventory.md` 3
  (static proof only).
- The client compares each element with its slot and makes a new slot object only for an
  element that is different (static proof 0x48E4130, 0x5C2DEE0). The compare has the bound
  flag: an item whose definition has `BindOnPickup` is bound in the client, thus the backend
  sets flag bit 0 for such an item. Without the bit the client made the slot object again at
  each update (behavioral proof: the item pointer of the slot changed).

**One item** (reader 0x5CE8C40, writer 0x5C734A0; static proof for the layout, behavioral
proof for the values with a mark):

```text
item   := packed:u64 durability:size chrono:size gearScore:size perk:size x5 dye:4 bytes
          instance:16 bytes flags:u8 [appearance:u32 skinDye:4 bytes]
packed := ((((quantity * 64 + action) * 502 + slotCode) << 20) + itemIndex) * 8001 + maxDurability
```

| Value | Meaning | Evidence |
| --- | --- | --- |
| `quantity` | size of the stack; 0 is an empty slot | behavioral proof (number on the item cell) |
| `action` | the cause of the change, 0 to 63 (below) | behavioral proof (slot +0x20 in the client memory) |
| `slotCode` | the slot number 0 to 500, or 501 for "no slot" | static proof; the backend sends the slot number |
| `itemIndex` | index in the item index list, 20 bits | behavioral proof (icon, item id CRC at item +0x40) |
| `maxDurability`, `durability` | 0 to 8000 | behavioral proof (item +0x0C, +0x08) |
| `chrono` | order counter of the item in the container | behavioral proof (item +0x14); its effect is not known |
| `gearScore` | | behavioral proof (item +0x3E) |
| `perk` x5 | index in the perk list of the client, 0 is no perk | live proof I1 and I2 of stage 1.3b: the backend sends the perks of the instance (4.23) |
| `dye`, `skinDye` | | static proof; the backend sends 0 |
| `instance` | 16 bytes that name the stack; all zero is "no instance" | behavioral proof (item +0x220) |
| `flags` | bit 0 bound to the player, bit 1 locked | behavioral proof for bit 0 (item +0x160) |
| `appearance` | transmog appearance id | static proof; the backend sends 0 |

- The last two values (8 bytes) are there only when the client setting
  `javelin.enable-transmog` is true. Behavioral proof: the cached setting at image
  +0x9F30298 has the ready flag 1 and the value 1 after the client read its first item. An
  item thus has 45 bytes when each `size` value is below 128. A wrong choice makes the read
  of the bundle fail.
- The empty element: quantity 0, action 0x11, slot code 501, item index 0, and zero in each
  other value: `00 00 44 F0 B0 B0 00 00` and 37 zero bytes (behavioral proof).
- The server must keep `maxDurability` at 8000 or less and the quantity at 10,000 or less.
- `action` is the value `ActionCausingSync` of the client: 0 ItemAdded, 1 ItemGathered,
  2 ItemRemoved, 6 ContainerTransfer, 0x11 the start value of a slot in the client (the load
  of a container), 28 LockStateChanged (static proof; the full list is in `inventory.md`
  4.1). The backend sends 0x11 in each element of a full vector, and the cause of the change
  in a journal entry.
- The client computes the weight of the bag itself (behavioral proof: the sword with the
  table weight 30 and 10 ore with the weight 2 show as 5.0 of 200.0).

**Currency state** (type 3786; constructor 0x37FF0E0; apply function of the facet 0x3820570).
One group with one field: `currency`, `u64`, the coin in hundredths. Behavioral proof: the
amount 12345 shows as `123.45` in the bag window, and the currency component of the client
has 12345 at +0x1A0. The client keeps `min(amount, limit)`; the limit of the supported build
is 100,000,000 (`javelin.wallet-cap-player`, static proof). The backend refuses a larger
amount.

**Fragments and bundles.** A bag can be larger than one carrier message (1,024 data bytes,
3.2), and no live run has a state bundle that the carrier sent in chunks. Thus the backend
keeps each bundle of its states below that size:

- A session with no record of the entity gets the full vector. When the vector is larger
  than one fragment can be (900 bytes of state), the first fragment has the first elements,
  and each next fragment is a journal that adds elements at the end.
- After that the session gets one journal entry for each slot that changed after its last
  record (each slot of an entity has the world revision of its last change).
- The session puts the fragments of a record into bundles in their sequence. A bundle ends
  before a fragment that makes its payload larger than 960 bytes, and before a second
  fragment for one slot: the client sorts the fragments of a record by their slot (0x15CE270),
  thus two fragments of one slot have no sure sequence in one record. A fragment that is
  larger than 960 bytes goes in a bundle of its own (the carrier sends it in chunks), and a
  fragment that makes a bundle larger than the limit of the client (256,000 bytes of
  payload, static proof 0x646C2B0) does not go out.
- Behavioral proof: a bag of 28 items in the first record of a session (3 bundles: 198, 882,
  and 442 bytes of payload), and 25 new stacks for a session that is in the world (2 bundles).
- The first record of the own character has, after the five fragments of 4.4: the
  progression fragment, the attribute fragment (4.12), the paperdoll fragment (4.11), the
  currency fragment, the container fragment, the two list states of the skills (slots 65 and
  67, 4.13), and the ability state (slot 46) when the character has an ability on a bar.
  Behavioral proof: 12 fragments in one bundle of 930 bytes for a character with equipment
  and no skill, and a second bundle of 850 bytes with the ability state.

**The level of the player and the bag window** (behavioral proof). The script of an item
cell (`lyshineui/slices/itemlayout`, function `SetLayout`) asks if the player meets the
requirements of the item: it compares the data value `Hud.LocalPlayer.Progression.Level`
with the level that the item needs (`lyshineui/_common/itemcommon`,
`GetPlayerMeetsRequirements`). A client that got no progression state has no such value:
the script stops with the error "attempt to compare nil with number" for each item, and
the game log has no line for it (the script error function 0x4A65DA0 writes none; a trace
of that function shows the text). The result on the screen: each item cell is a wide white
cell, it has no tooltip and no action menu, and the inventory screen has no equipment
panel. With the state `MB::ProgressionComponentReplicatedState` (slot 70, type 899; static
proof `progression.md`) the screen is complete. Thus the backend sends this state from stage
1.3 on. From stage 1.5 on it has the stored level and experience of the character (4.12):

```text
state := 03                    groups 0 and 1
         03 level:u32 bonusLevel:u32            level is the number of the screen minus 1
         03 experiencePoints:u32 restedExp:u32
```

The level system (experience, a stored level, the attributes, the view of an observer) is
system 1.5.

**Requests of the client** (static proof for each layout: the field writer and the field
reader of the message class; `inventory.md` 9; behavioral proof for the two rows of the
table: one capture of each request in stage 1.3). Each one is a component call: its body
starts with `target:u64 entity:u64` (4.2). The client has no reply message for a request
that the server did: the new container state is the answer.

| Type | Name | Body after `target entity` | The backend |
| ---: | --- | --- | --- |
| 1838 | `ItemDropComponentServerFacet_DropInventoryItems` (writer 0x65AF090) | `count:size` (501 at most), then for each entry `slot:u32 instance:16 bytes u32 quantity:u32` | stage 3.5b.2: puts the quantities into a new Discarded bag at the feet of the character (4.42); with no loot container in the interaction data it removes them (stage 1.3). Live check L7 of stage 3.5b.2: the drag of a stack to the area "Drop items" gives count 1, the slot, the instance, `FFFFFFFF`, the quantity |
| 826 | `PaperdollDropItem` (stage 3.5b.2, live check L7) | `slot:u32 quantity:u32` (24 bytes with the call): the paperdoll slot and the quantity | puts the item of the paperdoll slot into a new Discarded bag (4.42), and refreshes the gear; with no answer the client keeps the item |
| 2549 | `ItemRepairComponentServerFacet_RequestSetLockState` (writer 0x2E8E120) | `containerType:u32 slot:u32 instance:16 bytes quantity:u32 locked:u8` | sets or clears flag bit 1 (container type 1, the bag) |

- The backend finds the character from the session. The `entity` field must be the entity
  group id of the entity of the session, each entry must name the item instance of its slot,
  and each quantity must be 1 to the quantity of the stack. A request that is not correct
  changes nothing, and the log has the cause. A drop request with one wrong entry is refused
  as a whole.
- The lock of an item (flag bit 1). The backend refuses a drop entry for an item with the
  lock, and thus the full drop request: the lock protects an item from a request that takes
  it away from the character. The action menu of a locked item in the client has no entry
  "Drop" and no entry "Salvage" (behavioral proof, stage 1.4); the request of a client that
  is not its screen gets the same rule. The lock does not stop an equip (4.11). A lock
  request with a quantity above 10,000 (the limit of an item location in the client) is
  refused; the client sends the quantity 0.
- The second `u32` of a drop entry is a slot value whose meaning is not known (static
  proof). The client sent `FFFFFFFF` (-1) there (behavioral proof).
- Captures of the live client. Drop of a stack of 10 from slot 1 (45 bytes; the target is the
  facet of a component of the slice entity "RootPlayer"): `01`, `00000001`, the 16 bytes of
  the instance id, `FFFFFFFF`, `0000000A`. Lock of the item of slot 0 (45 bytes; the target
  is the facet of a component of the slice entity "Crafting"): `00000001`, `00000000`, the
  16 bytes of the instance id, `00000000`, `01`. The quantity of a lock request is 0.
- How the user makes these requests in the bag window (behavioral proof): a drag of an item
  cell to the area "Drop items" at the right of the window sends the drop request for the
  full stack (a drag to the character at the left is an equip try); a click on an item cell
  opens the action menu (Equip, Lock, Drop, Link to Chat, Salvage), and its entry "Lock"
  sends the lock request. The item cell then shows a lock mark.
- The client has no request to move, to swap, to stack, or to split an item inside one
  container (static proof: the 23 message types of the container, item drop, and player
  trade components, `inventory.md` 13.1). Its bag window sorts the items by their class
  itself (behavioral proof: the sections "Weapons", "Smelting", "Utilities"). The split
  window of the client only selects the quantity of a drop or of a transfer. The backend has
  these four operations for the development commands and the tool window.
- Not handled in stage 1.3: use (2917), salvage (1733, 6188), repair (3003), and the transfer
  to a different container (2801, 2942).

**Model of the backend** (a design of this project). The bag of a character is a vector of
slots in its entity (`world::Bag`), as the vector of the client: it does not become shorter
while the character is in the world, a slot that loses its item stays as an empty slot, and
a new stack gets the first empty slot. The inventory system (`apps/backend/src/world/inventory.h`)
makes each change on a copy, writes all rows of the change in one transaction, and then
changes the entity and marks the component state. The table `items` has one row for each
item instance with the character id as its owner key (instance id, container, slot, item id
text, quantity, gear score, durability, flags, order counter), and the table `currencies`
has the coin (schema version 5). The container of a row is 1 for the bag and 2 for the
paperdoll (4.11). An item whose id the catalog does not have keeps its row and is the empty
element for the client.

**Development commands** (`tools/world`; routes `/v1/dev/items` of the backend, with
`--dev-commands`): `items`, `give`, `take`, `coin`, `move`, `split`, `lock`. Each command
names a character. A command for a character that is not in the world changes its rows.

**The weight of the bag for the client** (stage U3; game data and live proof of the run `u3-after5`, the inventory screen; no
client code address): the client counts the items of the bag (not the equipment, not the quick slots). The weight of a weapon or
an armor item is the WeightOverride of its stats row (WeaponItemDefinitions or ArmorItemDefinitions, column WeaponID = ItemStatsRef
of the master row) cut to whole tenths, when it is above 0; else the Weight of the master row. Live: a great axe (80, override 80)
and a rapier (40, override 25) show 10.5; a light helm (20, override 15.09) adds 1.5; medium gloves (30, override 26.76) add 2.6.
The limit 200.0 (EncumbranceBaseMax 2000) and 1.5 x the limit: the client shows "Encumbered" at 200.2 (not at 200.0) and
"Overencumbered" at 300.2; it then sends the presses of an ability and a dodge, and plays nothing for them. Client code 0x48D7170
and 0x48D1830 test the weight against the limit (research Q4). The rules of the server: [Items and equipment](systems/items-and-equipment.md).

### 4.11 Equipment: paperdoll state, equipment requests, visible gear

Research files: `~/Games/soulwarden/work/research/equipment.md` (static proof, checked by a second
agent). Evidence of the live runs of stage 1.4:
`~/Games/soulwarden/work/evidence/equipment/implement/`. Each statement below has its evidence
level. "Behavioral proof" is a live run of stage 1.4 with the client build of this document.

**Component.** The paperdoll (the equipment) is slot 62 of `slices/player.dynamicslice`
(entity "Inventory") and of `slices/remoteplayer.dynamicslice`. Its state type is
`MB::PaperdollComponentReplicatedState`, type index 3183 (constructor 0x670D610; apply
function of the facet 0x683B800; behavioral proof: a trace of the apply function and the
effect on the screen). The state has the common state form (4.3) with three groups. Group 0
has no field.

| Group | Field | Name | Value | Who gets it | Evidence |
| ---: | ---: | --- | --- | --- | --- |
| 1 | 0 | `visibleDurability` | list of `u32` | owner | behavioral proof |
| 1 | 1 | `visibleFullItemData` | list of item | owner | behavioral proof |
| 1 | 2 | `nonVisibleFullItemData` | list of item | owner | behavioral proof |
| 1 | 3 | `nonVisiblePaperdollSlots` | list of `u32` | owner | behavioral proof |
| 1 | 4 | `nonVisibleDurability` | list of `u32` | owner | behavioral proof |
| 1 | 5 | `bonusEquipLoad` | `u16` | owner | static proof, not sent |
| 1 | 6 | `isLocalPlayer` | 1 byte | owner | behavioral proof |
| 1 | 7 to 11 | `isLoadoutPanelOpen`, the loaded ammunition of two weapons, `loadouts`, `hideSkins` | | owner | static proof, not sent |
| 2 | 0 | `sheatheMap` | 7 bytes | each client | behavioral proof |
| 2 | 1 | `itemSlotsAttachmentStatus` | 7 bytes | each client | static proof, not sent |
| 2 | 2 | `activeMap` | 7 bytes | each client | behavioral proof |
| 2 | 3 | `visiblePaperdollSlots` | list of `u32` | each client | behavioral proof |
| 2 | 4 | `visibleItemVisualData` | list of item look | observer | behavioral proof |
| 2 | 5 | `loadoutSwapIncrement` | 1 byte | each client | static proof, not sent |

```text
list      := 00 version count:size element*count      the full form (readers 0x2F9BC20, 0x2D97FA0,
                                                       0x40E8F30, 0x6950CD0); count 0x2000000 at most
version   := has:u8 [sequence:size64 when has is 1]   the backend sends has = 0
item      := the element of a container (4.10; reader 0x5CE8C40), with the slot code 501
item look := itemIndex:size flags:u8                  (reader 0x5CE9070, writer 0x5C736D0)
             [value:size when flag bit 0]             a 16-bit value; its meaning is not known
             [dye:4 bytes when flag bit 1]
             [appearance:u32 skinDye:4 bytes]         when `javelin.enable-transmog` is true (4.10)
map       := 7 bytes; bit N is for slot N: byte N / 8, bit N mod 8 (reader 0x6951740)
```

A list also has a journal form (a first number that is not 0), as the vector of a container.
The backend does not use it for this state.

**The lists are changes, not a full state** (static proof 0x68382D0 and 0x6839160; behavioral
proof). The lists of one fragment are parallel: entry N is one slot number with its item (and
its durability). The client handles each entry as a change of that slot:

- An entry with an item that is not the item of the slot puts the item into the slot. An
  entry that is equal to the content of the slot does nothing.
- An entry with no item (quantity 0, or the item index 0) takes the item out of the slot.
- A slot that has no entry keeps its item. Behavioral proof: a fragment with only the leg
  armor added it to a character with a chest armor and a sword, and a fragment with only an
  empty chest entry removed only the chest armor.
- The client does nothing, and writes no log line, when the lists of a group do not have the
  same length (static proof).
- The client of the owner does the entries with the cause 5 (`ActionCausingSync`: item
  unequipped) first. Thus the backend sends an entry with no item with the cause 5
  (`00 00 16 F9 98 30 00 00` and 37 zero bytes), and an item leaves its old slot before it
  is in a new one (a swap of two weapon slots, behavioral proof). A new item has the cause 4
  (item equipped), and an item of a first record the cause 0x11.
- A first record in more than one part (stage 1.3b, live I12): a later part with entries of a
  list group (the cause 0x11) took the items of the same group of an earlier part out of their
  slots. Thus the backend keeps each group whole in one part when it fits (the slots that do
  not show on the body first), and each part of the owner has `isLocalPlayer` (4.23).

**Owner view** (behavioral proof). The client reads the item lists only when the state has
`isLocalPlayer` = 1 (static proof 0x6771AB0). The client uses the two list groups in the same
way: the visible group is `visiblePaperdollSlots` with `visibleFullItemData` and
`visibleDurability`, and the other group is `nonVisiblePaperdollSlots` with
`nonVisibleFullItemData` and `nonVisibleDurability`. The backend puts the slots whose item
shows on the body (0 to 4, 24 to 29) into the visible group and each other slot into the other
group, and it sends `isLocalPlayer` with each group of fragments. The first fragment of the
research for a chest armor and a sword (135 state bytes; item index 1323 and 5278):

```text
06                                   groups 1 and 2
43                                   group 1: fields 0, 1, 6
   00 00 02 00 00 00 FA 00 00 03 E8     visibleDurability: 250, 1000
   00 00 02 <item, 46 bytes> <item, 46 bytes>      visibleFullItemData
   01                                   isLocalPlayer
0D                                   group 2: fields 0, 2, 3
   00 00 00 01 00 00 00                 sheatheMap: slot 24
   00 00 00 01 00 00 00                 activeMap: slot 24
   00 00 02 00 00 00 01 00 00 00 18     visiblePaperdollSlots: 1, 24
item (the chest armor): 00 01 3A 16 89 51 85 E5  BA 03  00  3C  00 00 00 00 00  00 00 00 00
                        <16 bytes of the instance id>  00  00 00 00 00  00 00 00 00
```

A character with no equipment gets `02 40 01` (only `isLocalPlayer`) in its first record.

**Observer view** (behavioral proof: a second character with a chest armor, leg armor, and a
sword showed its gear to the client of an observer, in its first record and after a change).
A client that has no `isLocalPlayer` = 1 for the entity reads `visiblePaperdollSlots` with
`visibleItemVisualData` (static proof 0x6771AB0, 0x6839160): the two lists must have the same
length. It makes an item from each look with a new instance id, the quantity 1, and the
durability 1 (0x5C23250). The observer gets only the slots with a look, the two maps, and no
field of group 1. The state for the same character (52 bytes):

```text
04                                   group 2
1D                                   fields 0, 2, 3, 4
   00 00 00 01 00 00 00                 sheatheMap: slot 24
   00 00 00 01 00 00 00                 activeMap: slot 24
   00 00 02 00 00 00 01 00 00 00 18     visiblePaperdollSlots: 1, 24
   00 00 02                             visibleItemVisualData: 2 entries
      AB 14  00  00 00 00 00  00 00 00 00     item index 1323, flags 0, appearance 0, skin dye 0
      9E 52  00  00 00 00 00  00 00 00 00     item index 5278
```

An update that takes the chest armor off is `04 18 00 00 01 00 00 00 01 00 00 01 00 00 00 00
00 00 00 00 00 00` (slot 1 with the item index 0). An observer gets no fragment for an entity
that has no item with a look, and no fragment for a change of a slot with no look.

**Slots** (`Javelin::PaperdollSlotTypes`, registration 0x5D19E40; static proof, and behavioral
proof for the slots 1, 3, 5, 14, 24, 25): 0 head, 1 chest, 2 hands, 3 legs, 4 feet, 5 amulet,
6 token (the earring), 7 ring, 8 paint, 9 the hand slot of a gathered object, 10 and 11 two
old weapon slots, 12 arrows, 13 cartridges, 14 to 19 quick slots, 20 backpack, 24 to 26 weapon
1 to 3, 27 to 29 the off hand item of weapon 1 to 3, 30 to 32 bags, 33 logging axe, 34 sickle,
35 skinning knife, 36 pickaxe, 37 fishing pole, 38 azoth staff, 39 to 43 instruments, 44 a
throwable object, 45 heart rune, 46 to 49 mount items. The slot database of the player
(`sharedassets/genericassets/paperdoll.paperdolldb`, game data) has 47 rows: it has no row for
21, 22, and 23. The values 50 and 51 are "number of slots" and "no slot".

**Which slot takes which item** (static proof: the check of the client before an equip
request, 0x679C060, and the names of the item class numbers, 0x5CB4770). A slot takes an item
whose class text (the column `ItemClass` of the item tables, tags with "+" between them) has
the tag of the slot: `EquippableHead` (0), `EquippableChest` (1), `EquippableHands` (2),
`EquippableLegs` (3), `EquippableFeet` (4), `EquippableAmulet` (5), `EquippableToken` (6),
`EquippableRing` (7), `EquippablePaint` (8), `Consumable` or `Blueprint` (14 to 19),
`EquippableBackpack` (20), `EquippableMainHand` or `EquippableTwoHand` (24 to 26),
`EquippableOffHand` (27 to 29), `Bag` (30 to 32; the name of class number 119 is from the
item tables), `LoggingAxe` (33), `Sickle` (34), `SkinningKnife` (35), `Pickaxe` (36),
`FishingPole` (37), `AzothStaff` (38), `InstrumentFlute`, `InstrumentGuitar`,
`InstrumentMandolin`, `InstrumentUprightBass`, `InstrumentDrums` (39 to 43), `ThrowableObject`
(44), `HeartGem` (45), `MountBuff1` (46), `MountBuff2` (47), `MountAttachment` (48, 49). The
backend has this table (`world/item.cpp`). It has no rule for the ammunition slots (the rule
of the client needs the ammunition type of an item) and for the slots 9, 10, and 11: these
slots take no item. The client has more checks that the backend does not have (for example
two weapons of one kind): the client does not send such a request.

**Requests of the client** (static proof for each layout: the writer and the reader of the
message class; behavioral proof: captures of the live client in stage 1.4). Each one is a
component call of the paperdoll facet: `target:u64 entity:u64`, then the fields (4.2). The
target of the paperdoll facet was the value of the id rule of 4.2 (`0C47D9835971E8A3` for the
actor id with the serial 1; slice entity "Inventory", component id `A4682AF2564E7814`).

| Type | Name | Fields after `target entity` | Bytes | Client code |
| ---: | --- | --- | ---: | --- |
| 3895 | `PaperdollComponentServerFacet_ServerEquipItem` | `bagSlot:u32 paperdollSlot:u32 quantity:u32 instance:16 bytes flag:u8` | 45 | writer 0x65AA180, send 0x667FD90, caller 0x67A5C50 |
| 3896 | `PaperdollComponentServerFacet_ServerUnequipItem` | `paperdollSlot:u32 bagSlot:u32 quantity:u32 flag:u8` | 29 | writer 0x6562810, send 0x6680180, caller 0x67A7990 |
| 3900 | `PaperdollComponentServerFacet_ServerSwapItems` | `slot:u32 slot:u32 instance:16 bytes instance:16 bytes count:u8` | 57 | writer 0x65875F0, send 0x667FF80, caller 0x693B580 |
| 3626 | `PaperdollComponentClientFacet_OnEquipItemFail` (server to client) | `reason:u32` | 20 | reader 0x26D52A0, handler 0x687BB60 |

- Ranges of the readers of the client (static proof): a bag slot is a signed number from -1
  to 501, a slot of the paperdoll is 0 to 51, a quantity is 0 to 10,000, the two flags are 0
  or 1, and the last byte of a swap is 0 to 30. The backend refuses a body outside them.
- Equip, captures: the entry "Equip" of the action menu of a sword in bag slot 0 sent
  `00000000 00000018 00000001 <instance id> 00` (the client selects the slot: 24); leg armor
  in bag slot 3 sent the slot 3; a stack of 3 potions in bag slot 2 sent the slot 14 and the
  quantity 3. The instance id is the id of the item in the bag slot. The flag was 0.
- Unequip, capture: the entry "Unequip" of the action menu of the equipped leg armor sent
  `00000003 FFFFFFFF 00000001 01`. The bag slot -1 is "the first free slot".
- Swap, captures: a drag of weapon cell 1 to weapon cell 2 sent `00000019 00000018 <16 zero
  bytes> <instance id> 00`: the slot of the drop first, then the slot that the item came
  from, then the instance id of the item of each slot (zero for a slot with no item).
- The client has no reply message for a request that the server did: the new paperdoll state
  and the new container state are the answer (static proof; behavioral proof: the item is
  in its slot and not in the bag, with no second copy).
- A request that the server refuses gets type 3626. The body has the facet target and the
  entity of the request (the target of a client facet is the target of the server facet of
  the same component, static proof 0x16BCBA0) and a reason that the handler of the client
  does not read: it shows its 50 slots again (static proof 0x687BB60).
- The client makes its own checks before a request (the class of the item for the slot, its
  local player flag at facet +0x338, which was 1 in the live client). It sends no request for
  an action that its screen does not offer.
- Not handled: the sheathe request (3899), `ServerUseItem` (663), `UnequipAllItems` (1253),
  the loadouts, and the transfer of an item of a different container into the paperdoll. The
  drop of an equipped item (826) is handled since stage 3.5b.2.

**What the backend does with a request.** It finds the character from the session. The
`entity` field must be the entity group id of the entity of the session: if not, the request
changes nothing and gets no reply. Equip: the bag slot must have the item instance that the
request names, the quantity must be the quantity of the stack (the full stack moves), and
the slot must take the class of the item; an item that is in the slot goes into the bag slot
of the new one. Unequip: the slot must have an item, and the bag slot must be empty (or -1).
Swap: each instance id must be the item of its slot, and each item must be an item for its
new slot. A request that is not correct changes nothing, and the client gets type 3626 with
the reason 0. The lock of an item (4.10) does not stop an equip: the equip check of the
client has no lock test (static proof 0x679C7A0), and the action menu of a locked item has
the entry "Equip" and no entry "Drop" or "Salvage" (behavioral proof).

**Visible gear** (behavioral proof for the result; static proof and game data for the path).
The server sends no mesh name. The client finds the look of an item from its item index: the
columns `ArmorAppearanceM` and `ArmorAppearanceF` of the item tables give a row of the table
`ArmorAppearanceDefinitions` (skins and materials), and `WeaponAppearanceOverride` gives a
row of `WeaponAppearanceDefinitions` (the mesh). Client code: 0x693CF60 sets the slot,
0x68492B0 loads the assets, and its callback 0x6740460 calls 0x68531C0 (the look of a slot),
which calls 0x6851BA0 for an armor skin (a trace of each function, one hit for each item).

- Armor: a chest armor and leg armor show on the body of the own character and of a remote
  character, in the world and on the inventory screen.
- Weapons: `activeMap` has the bit of the active weapon slot, and `sheatheMap` the bit of each
  weapon that is in its sheath. With the two bits of slot 24, a sword hangs on the hip of
  the character, for the own character at the world entry and on the inventory screen, and
  for a remote character. The weapon display of the HUD shows the active weapon.
- After an equip of a weapon in a session, the client of the owner shows its own character
  with the weapon in the hand, in the stance of the weapon (the action sequence of layer 0
  changes from 1080 to 4064 for a sword), with the sheathe bit set or clear. The client
  selects this action itself. The weapon keys and the sheathe key reach the server only in
  the input report (the client sent no message of type 3899 or 3902 for them).
- The backend has no draw and no sheathe: each weapon has its sheathe bit, the active weapon
  is the first of the slots 24, 25, 26 that has an item, and its off hand slot is active
  with it.
- The inventory screen shows the equip load and the gear score of the equipment: the client
  computes them from the items (the gear score of each item is in its wire form).

**Model of the backend** (a design of this project). The equipment of a character is a map
from the slot number to a slot in its entity (`world::Equipment`). A slot that loses its item
stays as an empty slot while the character is in the world, with the world revision of its
change: a session that has an older record gets its entry with no item. An equipped item is
a row of the table `items` with the container 2 (`eItemContainerType`: the paperdoll) and
the slot number of the paperdoll: an item that moves between the bag and the paperdoll keeps
its row and its instance id, and the move is one transaction. The inventory system
(`world/inventory.h`) has equip, unequip, and the swap of two slots. A session with no record
of an entity gets each slot that has an item; after that it gets the slots that changed. The
entries of a fragment have 900 state bytes at most: a character with an item in 28 slots goes
out in more than one bundle (test case G8).

**The items of a new character** (game data; behavioral proof). The table of the type
`BackstoryDefinition` (`javelindata_backstorydata.datasheet`) has one row for each archetype
of the character creation (`Archetype_Soldier`, ...): the column `InventoryItem` is a list
`ItemID:quantity`. An entry with the quantity 0 (the head armor and the weapons of the
archetype) is an item that only the character creation screen shows. The create route gives
a new character each entry with a quantity: an item with a slot of its own that is free goes
into that slot (the four armor pieces, the first weapon `1hSwordT2_FTUE`), each other item
into the bag. `tools/gamedata/items.sh` writes the lists into the item catalog (member
`backstories`). The `equipment` entries of the creation data (2.2.5) have only appearance
ids for the menu: the backend does not use them.

**Development commands** (`tools/world`; routes `/v1/dev/items/equip` and
`/v1/dev/items/unequip`): `equip CHARACTER BAGSLOT [SLOT]`, `unequip CHARACTER SLOT
[BAGSLOT]`, `equipment CHARACTER`, and `items CHARACTER` also lists the equipment.

### 4.12 Level, experience, attributes: progression state, attribute state, attribute requests

Research file: `~/Games/soulwarden/work/research/progression.md` (static proof, checked by two
agents). Evidence of the live runs of stage 1.5:
`~/Games/soulwarden/work/evidence/progression/implement/`. Each statement below has its evidence
level. "Behavioral proof" is a live run of stage 1.5 with the client build of this document.

**Progression state.** The level and the experience are in the component Progression: slot 70
of `slices/player.dynamicslice` (entity "GameEvents") and of
`slices/remoteplayer.dynamicslice`. The state type is
`MB::ProgressionComponentReplicatedState`, type index 899 (constructor 0x3CDF390; apply
function of the facet 0x3CF15D0; behavioral proof: a trace of the apply function and the
effect on the screen). The state has the common state form (4.3) with two groups, and each
field is a 32-bit number.

| Group | Field | Name | Value | Who gets it | Evidence |
| ---: | ---: | --- | --- | --- | --- |
| 0 | 0 | `level` | the level of the screen minus 1 (the row index of the level table) | each client | behavioral proof |
| 0 | 1 | `bonusLevel` | 0 | each client | static proof; sent as 0 |
| 1 | 0 | `experiencePoints` | the total experience of the character | owner | behavioral proof |
| 1 | 1 | `restedExp` | 0 | owner | static proof; sent as 0 |

- The client does not compute the level from the experience: the server sends the two values
  (static proof). The client reads the start and the end of the level from its own level
  table (`XPLevels`, column `XPToLevel`: the total experience at which a level starts) and
  shows the part of the level that is done (0x3CFD790; behavioral proof: 90,100 experience
  at level 20 fills 48 % of the bar of the character window).
- The client has no message for a level-up. Its facet shows the level notice ("LEVEL 20", the
  entry "Assign Points", an effect on the character) when it gets a `level` that is larger
  than its last one (0x3CFE120; behavioral proof, for a change of 19 levels and of 1 level).
  The first `level` of a facet gives no notice (behavioral proof: a world entry with the
  level 21 shows none; the backend sends the level in the first record of each session).
- The HUD shows `level + bonusLevel + 1` below the health bar (behavioral proof).
- The largest level. `GetMaxLevel` of the client (0x3CED4B0) is the value `MaxDisplayLevel`
  of the expansion row of the player minus 1. Behavioral proof: with the level 65 the
  character window shows "Rise of the Angry Earth needed to advance", thus the client has
  the expansion row `None` (60) with this backend. The backend uses the value of that row
  (game data) as the largest level of a player.
- The character window shows the text "N XP to Level M" at its lower border. The client
  writes this text when the window opens. While the window is open, the bar follows each new
  state, and the text is one state behind (behavioral proof). This is the script of the
  client: the state of the backend is the same in the two cases.

**Observer view** (behavioral proof). The name tag of a remote player shows its level before
its name: "1 Remote" with no progression fragment, and "30 Remote" after a fragment with the
level 29. Thus an observer gets group 0 of the state. The experience is not a value for a
different player: an observer gets no field of group 1.

**Attribute state.** The attributes are in the component Attribute: slot 27 of the player
slice (entity "RootPlayer"). The remote player slice has no such component: the state is for
the owner only. The state type is `MB::AttributeComponentReplicatedState`, type index 129
(constructor 0x276C250; apply function of the facet 0x2788BE0; behavioral proof). One group
with four fields:

```text
0 attributes               count:size (type:u32 value:u32)*count     (reader 0x279E2B0)
1 attributeBonuses         container of type:u32 value:u32           (reader 0x279D9B0)
2 placingBonuses           container of u32, 5 elements at most      (reader 0x279DF40)
3 persistentAttributeData  unspent:u32 ignoreBackstory:u8 hasRespec:u8
                           count:size (type:u32 points:u32)*count behavior:u32   (reader 0x279E360)
container := 00 version count:size element*count      the full form; version := has:u8
             [sequence:size64 when has is 1]. The backend sends has = 0 and no element.
```

- An attribute is a number (`CharacterAttributeType`): constitution 0, focus 1, strength 2,
  dexterity 3, intelligence 4 (static proof 0x27967D3; behavioral proof: the request of the
  client for 3 points of strength has the type 2).
- The value of an attribute has the start value in it: 5 (the first row of each attribute
  table of the game) plus the points that the character spent. `attributes` has all five
  types: the client writes the values of a type to its screen only when the map has it
  (static proof 0x279C470).
- `unspent` is the number of free points. The client does not compute it: the server sends
  it (behavioral proof: the character window shows "102 POINTS AVAILABLE" for the value 102).
  The entries of the spent points have only the types with points.
- `behavior` is 5 (the client selects the attribute of the placing bonuses itself). The two
  flag bytes are 0 or 1: a different value makes the read of the fragment fail (static
  proof). The backend sends 0 for the two: the client does not read `ignoreBackstory`, and
  `hasRespec` is a reset with no cost, which the level table of the game gives at no level.
- The two bonus containers are in each fragment of the backend, with no element. A container
  in the full form is a change for the client in each fragment (static proof: the merge
  functions 0x278B490 and 0x278D590 do not compare the content), thus the facet copies all
  four fields each time, and the values of the server replace the values that the client
  computed itself when it sent a request. Behavioral proof: after a spend in the window, the
  values show with a green mark until the next attribute state.
- The equipment panel of the inventory screen and the character window show the attribute
  values of this state (behavioral proof: STR 8, CON 7 after a spend; with no attribute
  state the panel showed 31.4 for each attribute).

**The requests of the client for its attributes** (behavioral proof for the two layouts and
for the reaction of the client). Each one is a call of the Attribute component (the body
starts with the facet target and the entity group id, 4.2). The client changes its own
values when it sends a request, and it has no reply message: the next attribute state is the
answer, also when the server refused the request.

```text
1502  AttributeComponentServerFacet_RequestSpendAttributePoints   writer 0x2747D80, reader 0x274F8D0
      target:u64 entity:u64 types:size (type:u32)* points:size (points:u32)*
      Each list has 5 elements at most. A number of points is 0 to 700: the points to add,
      not the new value. Live body for 2 points of constitution and 3 of strength (34 bytes):
      <target> <entity> 02 00000000 00000002 02 00000002 00000003
2409  AttributeComponentServerFacet_RequestRefundSpentPoints       reader 0x259E1E0
      target:u64 entity:u64                                         (16 bytes)
```

- The window sends a spend only for points that the character has (the buttons "+" and
  "Assign"). The button "Respec" is on only after an attribute state of the server that has
  spent points. Its popup shows the cost ("Cost: 0.00. Respecs are free until level 60."),
  and the client sends the reset after "Yes" when the cost is 0 and the client has a currency
  state (`hasRespec` was 0).
- Checks of the backend (the rule of the client for its own values, 0x2778130 and 0x279AC70,
  and rule MP-5): a request for more points than the free points of the character changes
  nothing; a request with one type two times changes nothing; an attribute does not go above
  the largest value of the attribute tables (the points above it stay free). After a request
  that the backend refused, the session gets the attribute state again.
- The cost of a reset is the column `AttributeRespecCost` of the level table (0 below level
  60, in the unit of the coin amount). The backend takes it from the coin of the character in
  the transaction that writes the points, and it refuses a reset when the coin is less.
- Not handled: `SetPlacingBonusBehavior` (type 6397) and the debug request for a level (type
  3342), which the screens of the client do not send.

**Health** (game data; the sum is a rule of this project). The maximum health of a player is
`HealthBaseMax` of the row `Player` of the vitals table (856), plus the column `Health` of the
level table (50 for each level above the first), plus the column `Health` of the constitution
table for the value of the constitution (0 for the value 5, 40 for 6, 80 for 7). The game data
has the three values; the code that adds them was in the original server. One part has
behavioral proof: the character window shows the health of the server (1,000 before this
stage) with the change that a selection of 2 points of constitution gives as "1000 > 1080",
which is the difference of the constitution table. The backend sends the new `HealthMax` and
`maxHealth` (4.5) to each observer of the character when the level or the constitution
changes, and the health keeps its part of the maximum (the backend has no damage yet).

**Model of the backend** (a design of this project). The level, the experience, and the
spent points of each attribute are in the entity of the character (`world::Progression`,
`world::Attributes`) and in one row of the table `character_progress` (schema version 6),
with the character id as the key. The free points are computed: the sum of the column
`AttributePoints` of the levels 2 to the level of the character, minus the spent points. The
progress system (`world/progress.h`) has the functions: set the level, give experience (the
level goes up while the experience is at the start of the next level or above it), set an
attribute, spend, and reset. A level with fewer points than the character spent makes all
its points free again. The character list of the gateway has the level of the row in the
menu document of each character (2.2.5). A system that gives experience later (a kill, a quest) calls the
experience function.

**Game data.** `tools/gamedata/progression.sh` writes `progression-tables.json`: the level
table (70 levels), the largest level and the largest trade skill level of a player with no
expansion, the health of each value of the constitution, the base vitals of a player, and the
tables of 4.13. With no file the backend sends the stored values of a character (no free
point, health 1,000), and it refuses a command or a request that needs a table.

**Development commands** (`tools/world`; routes `/v1/dev/progress`): `progress CHARACTER`,
`level CHARACTER LEVEL`, `xp CHARACTER AMOUNT`, `attribute CHARACTER NAME VALUE`.

### 4.13 Weapon mastery, ability bar, trade skills: three skill states, their requests

Research file: `~/Games/soulwarden/work/research/skills.md` (static proof, checked by a second
agent). Evidence of the live runs of stage 1.6:
`~/Games/soulwarden/work/evidence/progression/implement/`. "Behavioral proof" is a live run of
stage 1.6 with the client build of this document.

**Components.** Three components of the player slice have the skills of a character. The
remote player slice has no one of them: the three states are for the owner only. Each state
has the common state form with one group.

| Slot | Component (entity) | State type | Type index | Constructor | Apply function of the facet | Content |
| ---: | --- | --- | ---: | --- | --- | --- |
| 65 | CategoricalProgression (GameEvents) | `MB::CategoricalProgressionReplicatedState` | 911 | 0x3CA3CA0 | 0x3CB7170, then 0x3CC0D50 | rank and experience of each weapon mastery and trade skill |
| 67 | ProgressionPoint (GameEvents) | `MB::ProgressionPointReplicatedState` | 3681 | 0x3CDFA30 | 0x3CF1880 | the free mastery points of each weapon and the nodes of its tree; stage QG1: the free tokens of each territory pool and its cards (4.59) |
| 46 | Ability (Stats) | `MB::AbilityComponentReplicatedState` | 185 | 0x66EA770 | 0x6839390 | the three active abilities of each weapon |

All three rows have behavioral proof (a trace of the apply function, and the screens of the
client).

**Ids.** An id on the wire is the CRC-32 of the lower-case id text of the game table (game
data and behavioral proof): the sword mastery `SwordAbilityTable` is `E8EC5716`, the trade
skill `Mining` is `25DD6A56`, the node `Ability_Sword_Bash` is `6C2E4693`. The id of a weapon
is the id of its mastery (slot 65), of its point pool (slot 67), and of its ability table
(slot 46). The id of a node is the id of its ability.

**List fields.** Each field of the slots 65 and 67 is a list in the full form:

```text
list  := 00 stamp count:size element*count      (readers 0x3779060, 0x2F9A420, 0x365E0D0,
                                                 0x2F9BEF0, 0x3D01040)
stamp := 01 sequence:size64                     the backend sends the world revision of the change
```

**Slot 65** has three parallel lists: `progressionIds` (field 0, `u32`), `ranks` (field 1,
`u16`), and `points` (field 2, `u64`). The facet uses them only when they have the same
length, and it makes its map again from each fragment (static proof): a fragment has each
entry of the character. An id that is not in the lists has the rank 0. The backend sends no
entry for a rank 0 with no experience.

- Weapon mastery: the screen shows the rank plus 1 (level 1 to 20). `points` is the
  experience in the rank, not the total: the screen shows it against the column
  `MaximumInfluence` of the rank table (behavioral proof: rank 4 with 60 experience shows
  "Level 5 / 20" and "95 to next level").
- Trade skill: the screen shows the rank. The largest rank of a client with no expansion is
  200 (the window shows "Trade Skill Level Cap: 200"), and the riding skill, whose rank
  table needs the expansion, shows 0 for each rank (behavioral proof).
- A larger rank in a state after the first one makes the client show a notice ("SWORD AND
  SHIELD 6" with "Assign Points", and the text "+100 Sword and Shield Mastery" for the
  experience; behavioral proof). The client does not change a rank itself.

**Slot 67** has four lists: `poolIds` (field 0, `u32`), `poolCounts` (field 1, `u16`: the free
points of each pool), `poolCategories` (field 2, one byte: 1 is a pool of a player), and
`pointEntries` (field 3, `poolId:u32 pointId:u32 level:u16`). The pool lists have 50 elements
at most, and the entry list 1,000.

- The client removes a pool that is not in the lists: a fragment has each pool. The backend
  has one pool for each weapon of the game data (16), also with no point: the client
  permits a selection in a tree only when it has the pool of the weapon with free points.
- The list `pointEntries` of a fragment replaces the list of the client: the tree window
  shows only the nodes of the last fragment that the client used (behavioral proof, stage
  1.7). The state merge of the client uses a list only when its stamp is larger than the
  stamp of the list that it has (static proof, merge function 0x3CF3960). The three live
  results with the character Soulwarden: (a) the backend of stage 1.6 cut 76 nodes into two
  fragments with one stamp: the client did not use the second fragment, and the sword tree
  showed 16 of 19 nodes; (b) with a larger stamp for the second fragment the client used it,
  and the trees showed only its 3 nodes (the sword 3 of 19, the bow 0 of 19); (c) with one
  fragment that has each pool and each node (114 nodes, a bundle of 1,275 bytes) each tree
  showed its 19 nodes. Thus the backend never cuts this state: one fragment has each pool and
  each node of the character. The statement "a node that is not in a fragment keeps its
  level" of the research is not correct for what the client shows.
- A node with the level 0 goes away (static proof): a node that a character lost goes out one
  time with the level 0. A fragment with no node makes the client give the selected levels of
  each pool of the fragment back itself (static proof 0x3CFBA80).
- Behavioral proof: the mastery screen shows the level and the free points of the sword
  ("2"), the tree shows the two nodes of a fragment as open, and the memory of the client has
  the pool `E8EC5716` with the category 1.

**Slot 46**, field 0 (`persistentAbilityData`, reader 0x6951790):

```text
tables:size ( tableId:u32 slots:size ( abilityId:u32 button:u32 )* )*      3 slots at most
versions:size ( tableId:u32 version:u32 )*
```

- The client finds the bar of a weapon by the index of its ability table in the list of its
  ability data manager, not by `tableId` (static proof 0x67EA1D0, 0x690F460). The list has
  the 29 tables of the type `AbilityData`, in a sequence that the game files do not give (the
  sequence of the asset catalog). Behavioral proof: the list in the memory of the live client
  was equal in two runs, and the request of the client for the sword has the index 27. The
  backend has the sequence as a constant of the supported client build
  (`gamedata::kAbilityTableOrder`):

  | Index | Table | Index | Table | Index | Table |
  | ---: | --- | ---: | --- | ---: | --- |
  | 0 | Flail `2915FB5D` | 10 | `2EE6A42E` | 20 | Rapier `81575CBB` |
  | 1 | Blunderbuss `6020B33F` | 11 | `05E2FC0C` | 21 | `32733382` |
  | 2 | Life staff `6E4FD87A` | 12 | `AE40FE40` | 22 | `2A11B501` |
  | 3 | Hatchet `AF42A7F2` | 13 | War hammer `F94C464F` | 23 | `1C6AEA69` |
  | 4 | Fire staff `5553E699` | 14 | `0C20C4DE` | 24 | `630D9982` |
  | 5 | Spear `7C2BB2C5` | 15 | Void gauntlet `AC457988` | 25 | Bow `C7EA8FF8` |
  | 6 | Greatsword `180B9A9F` | 16 | Dagger `572562B8` | 26 | `359CB911` |
  | 7 | `7A04F0CE` | 17 | `9E461EEF` | 27 | Sword `E8EC5716` |
  | 8 | `B8C4187D` | 18 | Musket `1C79C0C7` | 28 | `FF59AEF3` |
  | 9 | Ice gauntlet `4A366E22` | 19 | Great axe `E7AAC3FB` | | |

  The 13 tables with no name here are tables of creatures, perks, and items.
- A fragment has one element for each of the 29 tables, and each element has three places.
  The facet copies the list of the state on each fragment, and the client reads the places
  of an element with no size check (static proof 0x6839390, 0x6931310): a shorter list makes
  it read memory that is not a bar.
- The client makes this list itself at its world entry, with an empty bar for each table
  (0x6870BB0 with the argument 0; behavioral proof: 29 elements before the first fragment).
  Thus the backend sends no ability state in the first record of a character that has no
  ability on a bar (the state has 845 bytes), and it sends the full state for each change.
- `button` is the input number of the ability key of a place. The client writes the numbers
  0x192, 0x73, and 0x8B into its own list (behavioral proof: a memory read), and the backend
  sends these numbers. The HUD was the same with the numbers 0, 1, and 2.
- The state merge of the client (0x6860A30, compare function 0x66277F0) gives the field to
  the facet only when a table id, an ability, or a button number is different from the last
  field of the server (static proof). After a bar request that the backend refused, the bars
  of the server are the same as before, and the client has its own bar. The backend then
  sends the state two times to a session that had the bars: first with the place numbers 0,
  1, 2 as the button numbers, then with the numbers of the client. Each one is different
  from the field before it, thus the facet applies the bars of the server (test case K3 with
  a model of the merge; no live proof of a refusal).
- The backend sends no table version. The bars are correct without one (behavioral proof).
- The HUD shows the bar of the weapon in the hand next to the weapon (behavioral proof: the
  two abilities of the sword tree after the fragment).

**The requests of the client for its trees and its bars** (behavioral proof for each layout:
the body in a capture or in the log of the backend, and the reaction of the client).

```text
1112  ProgressionPointComponentServerFacet_RequestAssignPoolPoints    writer 0x3CD7B30, reader 0x3CD9530
      target:u64 entity:u64 poolId:u32 pointId:u32 count:u32          (28 bytes; count 4,096 at most)
2701  AbilityComponentServerFacet_RequestChangeMappedAbility          writer 0x65A5580, reader 0x65A6270
      target:u64 entity:u64 tableIndex:u16 slot:u32 abilityId:u32     (26 bytes; slot 0 to 2)
2857  AbilityComponentServerFacet_RequestApplyAbilityPoints           reader 0x259E1E0
      target:u64 entity:u64                                           (16 bytes)
186   AbilityComponentServerFacet_RequestMasteryTreeRespec            writer 0x65B2EE0, reader 0x65B3740
      target:u64 entity:u64 tableIndex:u16 flag:u8                    (19 bytes; the screen sends the flag 0)
2858  AbilityComponentClientFacet_MasteryTreeRespecResponse           reader 0x6568F80, handler 0x6855CA0
      target:u64 entity:u64 success:u8 tableIndex:u32                 (server to client, 21 bytes)
```

- The button "Assign" of a tree sends, for each node of the selection, one request 1112
  (count 1), then one request 2701 when the node is an active ability and the bar has an
  empty place (the first empty place), then one request 2857. The client changes its own
  tree and bar when it sends them, and it has no reply message: the states of the slots 67
  and 46 are the answer. The mastery screen shows a new ability on its bar only after the
  state of slot 46 (behavioral proof).
- The button "Respec" of a tree shows a popup with the cost in the currency "Azoth" (0 below
  rank 10) and sends the request 186. The client does not change its tree: it waits for the
  message 2858, and it sends no second reset before it gets one with a table index of 0 or
  more (static proof 0x69107F0). The target of the message is the target of the request
  (behavioral proof: one hit of the handler with the success 1 and the index 27, and no line
  "facet target ID couldn't be found" in the game log). The pool changes only with the next
  state of slot 67.
- The conditions of a node (the check of the client before it sends a point, 0x6849F10; the
  numbers are in `playerbaseattributes.pbadb` of the game): the node that the points table
  names for it (`RequiredProgressionPointId`) is there; a node below the first row needs one
  point in the row before it in its tree (for the rows 1 to 4); a node of the last row of a
  tree needs 10 points in that tree.
- Checks of the backend (rule MP-5): the pool is a weapon of the game data; the node is a
  node of the tree of that weapon; the character does not have it; one point for one node;
  the weapon has a free point; the conditions of the node are met. For a bar: the table index
  is a weapon; the ability is an active ability of the tree of that weapon, and the character
  has its node; the ability leaves the place that it had. A reset is done only when its cost
  is 0: the backend does not have the currency "Azoth", and it then sends the answer with the
  success 0. After a request that the backend refused, the session gets the state again (a
  node that the character does not have with the level 0).

**Model of the backend** (a design of this project). The skills of a character are in its
entity (`world::SkillSet`: the ranks, the nodes of each pool, the bars) and in the tables
`progressions`, `ability_nodes`, and `ability_bars` (schema version 6), with the character id
as the owner key and the id on the wire as the id of a row (the column `name` has the id text
for a reader). The skill system (`world/skills.h`) writes all rows of a change in one
transaction. A weapon has one mastery point for each rank (19 points at the largest level): the game
data has no value for this, and it is a rule of this project. The free points are
the rank minus the spent points. The experience of a rank is the column `MaximumInfluence`
of the rank table: experience that reaches it gives the next rank, and the rest stays. A
weapon that gets a rank with fewer points than the character spent has its tree reset. The
points state is one fragment for each number of nodes (the client replaces its node list
with the list of a fragment): a character with each weapon at the largest rank has 304 nodes
and a state of 3.2 kilobytes, which the session sends in a bundle of its own and the carrier
in chunks (3.2). A tree reset at a rank that has no cost row in the game data is refused, as
a reset with a cost.

**Development commands** (`tools/world`; routes `/v1/dev/progress`): `mastery CHARACTER
WEAPON LEVEL [EXPERIENCE]`, `tradeskill CHARACTER SKILL LEVEL [EXPERIENCE]`, `skillxp
CHARACTER NAME AMOUNT`, and `progress CHARACTER` lists the weapons with their nodes and bars
and the trade skills.

### 4.14 Creatures: the record, the states, the action of a creature, the spawn data, the rules of the project

Research files: `~/Games/soulwarden/work/research/creatures.md` and `enemies-start-area.md` (static
proof and game data, checked by a second agent). Evidence of the live runs of stage 3.4a:
`~/Games/soulwarden/work/evidence/creatures/implement/`. A statement with the mark "project
rule" is a decision of this project, not a contract of the client.

**Creature data** (game data, `tools/gamedata/creatures.sh`, file `creatures.json`). The
chain of one spawn point in the game files:

| Step | File | What it gives |
| --- | --- | --- |
| 1 | `regions/REGION/capitals/LAYER/LAYER.metadata` (object stream), list `AiSpawnLocations` | Position, CRC of the vitals row and of the category row, level, the flags `SpawnedByCoatlicue`, `IsAlias`, `IsOverride`, `IsEncounter` |
| 2 | `LAYER.capitals.json` of the same layer | The placement at that position (0.06 m or less): its id, its rotation, and the spawner slice (`coatgen/.../pointspawner_*.dynamicslice`) |
| 3 | The spawner slice | `m_aliasAsset` (or `m_sliceAsset`), `m_minRespawnCooldown`, `m_maxRespawnCooldown` |
| 4 | The alias asset, and `assetcatalog.catalog` | One or more slices with a percent value; the catalog gives the path of an asset id |
| 5 | The slice of the creature | The entities and components: `m_replicationIndex` is the slot of a component; the Aoi component has `m_sliceSpawnRadius`; the action list component names the grid file (`m_actionGridNew`) and the controller file (`m_controllerDefinition`) |
| 6 | The vitals tables (types `VitalsLevelVariantData`, `VitalsBaseData`, `VitalsCategoryData`, `VitalsLevelData`, `VitalsModifierData`) | The base row of a vitals row (family, creature type, `HealthMod`), the health of a level (`BaseMaxHealth`), the health modifier of a creature type (`CategoryHealthMod`) |

- Which entries are a spawn point (project rule): the entry has `SpawnedByCoatlicue` and not
  `IsOverride` and not `IsEncounter` (a usual spawn point, not an entry of an event); the
  base row of its vitals row has a family (a creature, not a siege object); its layer has a
  placement at its position; a slice of its alias has no schedule and has a movement
  component and a vitals component; and the client shows that slice correctly (the picture
  check below). The entries of one placement are one spawn point: the metadata has one entry
  for each slice of an alias with more than one slice (a left arm and a right arm, a man and
  a woman).
- The area of the file is the whole world (stage 3.4c): each region of the game copy that has
  a layer with spawn data. `--radius METERS` (with `--center X Y`, or around the start place
  `8880 4250`) selects a circle for a smaller file. Counts of the supported build:

  | Step | Count |
  | --- | --- |
  | Entries in the layers of 25 regions | 115,937 |
  | Usual entries (`SpawnedByCoatlicue`, not `IsOverride`, not `IsEncounter`) | 37,327 |
  | With a creature family | 36,574 |
  | Left out: no creature family (334 siege weapons, 244 barrels, 104 training dummies, totems and other objects) | 753 |
  | Left out: no placement at the position in the layer (215 objects of a trap or a puzzle, about 122 creatures of Outpost Rush, creatures of the catacombs, and creatures whose placement is a part of a larger slice) | 499 |
  | Left out: no slice with a movement component and a vitals component (web traps, cocoons, nests: objects that can take damage) | 291 |
  | Left out: the client does not show the slice correctly (50 slices, the picture check below) | 1,658 |
  | Not a point of its own: a second entry of one placement | 1,286 |
  | Spawn points of the file | 32,840 |
  | Kinds | 3,848 |
  | Slices | 503 |

  With each slice (`--all-slices`) the file has 34,381 spawn points, 4,028 kinds, and 553
  slices: the 50 slices that the client does not show keep 1,541 spawn points out. The 800 m
  around the start place (`--radius 800`, the area of stage 3.4a) have 4,423 entries, 1,092
  usual ones, and 1,076 with a creature family, as the research counted.
- An entry with no category row (stage 3.4c). 608 usual entries have the category id 0 (257),
  or an id that the category table of this build (8,283 rows) does not have (351): animals,
  and creatures of the newer areas. The client then shows the name of the vitals row on the
  name plate (live check: a creature of such an entry has its body, its name, its level,
  and its health bar). Such an entry is a spawn point (585 of them are in the file), and its
  kind has no category name. 5 entries have a base row with no creature type, which has no
  row in the modifier table: the health modifier is then 1.
- What can come in later. The entries with no placement need a reader for the slices of each
  placement of a layer (the creature is a part of a larger slice). The entries of an event
  and of an encounter (`IsOverride`, `IsEncounter`), and a slice with a schedule, need the
  event system. The 50 slices need the cause of their picture (below). The objects with no
  creature family and with no movement component are not creatures: they are work for the
  systems of the siege weapons and of the objects that can take damage.
- A kind is one vitals row with one category row and one alias. Its id for a command is the
  name of the vitals row; when more than one kind has the row, the kinds after the one with
  the most spawn points get a number (`Risen_L-R_Arm_6:2`).
- The health of a creature (project rule; the order of the original server is not known):
  `BaseMaxHealth` of its level times `HealthMod` of its base row times `CategoryHealthMod`
  of its creature type. Examples: Chalky Rabbit level 1: 337.5 x 1 x 0.05 = 16.875; Grey
  Wolf level 18: 1690.876; Fallow Scavenger (Risen) level 18: 1690.876 x 0.9 = 1521.788.
- The level of a spawn point is the level of its metadata entry, not the number in the name
  of its row (`Wolf_Grey_7` at level 18).
- The file of stage 3.4c (format 1) is one JSON document of 8.8 MB: the regions, the level
  table, the slices, the kinds, and the spawn points (each one a list of 9 values: the id of
  its placement, the index of its kind, its level, its position, its direction, and its two
  respawn values). From stage 3.4b the tool writes format 2 (9.4 MB): it also has the
  behavior data of 4.17 (the members `behavior`, `tags`, and `attacks` of a slice, `damage`
  of a kind, the damage and the gear score of each level, and the terrain height as value
  10 of a spawn point). The backend reads the two formats: the creatures of a file of
  format 1 stand at their spawn points. The values of this paragraph are from stage 3.4c
  (format 1). The tool needs 60 s with an Oodle library (32,875 pak entries). The
  backend loads the file in 0.17 s to 0.20 s, as a whole or not at all: its start needs
  0.41 s to 0.46 s with the file, and 0.22 s to 0.28 s with the file of stage 3.4a or with
  no file. Memory of the backend process (resident, `--no-rep-listener`, each other game
  data file loaded): 37.9 MB with no creature file, 39.3 MB with the file of stage 3.4a
  (1,029 spawn points), and 57.7 MB with the file of the whole world. The parse of the
  document needs 94 MB at its peak (66 MB with no creature file: the item catalog): the
  backend gives the free memory back to the system after the load of the game data
  (`malloc_trim`; without it the process kept 99 MB). A binary form or a file for each region is not necessary at this size
  (decision of stage 3.4c).
- **The patrol routes and the wander values** (stage 3.4e; static proof from the slices of the
  game copy, live proof of the four waypoints of the start wolf). From stage 3.4e the tool
  writes format 3 (12.4 MB): the member `routes` of the document, and value 11 of a spawn
  point (the index of its route, or `null`). The chain in the game files:

  | Step | Where | What it gives |
  | --- | --- | --- |
  | 1 | The spawner slice of the placement (step 3 above) | The field `m_patrolPathEntity`: the entity of the patrol in the same slice (no entity: no route) |
  | 2 | That entity: the component `AIPatrol` (type `FA7FA996`) | `m_waypointIds` (the entities of the waypoints, in their sequence) and, in its server facet, `m_pathTravesalPolicy` (0 a loop, 1 back and forth, 2 at random, 3 not defined) |
  | 3 | Each waypoint entity: the component `AIWaypoint` (type `3D1526BF`) and the transform | `m_taskName` (a text, can be empty), `m_minDuration` and `m_maxDuration` (seconds), `m_matchAlignment` (a flag); `m_worldTM` relative to the root of the slice |
  | 4 | The placement of the spawner (step 2 above) | The world place of a waypoint: the matrix of the placement (its rotation as a quaternion, and its position) times the matrix of the waypoint. The direction is `atan2(m[4], m[0])` of the result (degrees) |

  A route in the file is `[policy, [[x, y, z, yaw, minWait, maxWait, task, align], ...]]`.
  The behavior of a slice has two new members: `ambient` (the speed with no target: the
  blackboard value `AmbientNavMaxSpeed` of the slice, or the largest speed of the navigation
  configuration `Wander` or `AmbientLeash` of its Pathing component) and `wander` (`[on,
  nearest, farthest]`: the blackboard value `Enable_Wander`, and `WanderDistanceMin`,
  `WanderDistanceMax` in meters). A slice with no `Enable_Wander` does not wander (project
  rule). Counts of the supported build:

  | Value | Count |
  | --- | --- |
  | Spawn points with a route (of 32,840) | 19,814 (3,253 kinds) |
  | Policies: a loop, back and forth, at random | 13,773, 4,834, 1,207 |
  | Routes with 1, 2, 3, 4, 5, more waypoints | 9,310, 1,647, 5,444, 2,275, 430, 708 (20 waypoints at most) |
  | Routes left out (the spawn point has no route): a waypoint with no `AIWaypoint` component or no transform; a patrol with no waypoint; a waypoint more than 200 m from its spawn point | 357, 155, 2 |
  | Waypoints with a task, and the tasks that are an action of the action table of the slice | 24,895, 23 |
  | Slices with `Enable_Wander` true, false, not in the slice | 70, 0, 431 |
  | Kinds that wander; kinds with no route and no wander | 465; 576 |
  | Spawn points with no route whose kind wanders | 168 |
  | Slices with the action `Move` (the walk) and `Move_Combat` (the run) | 501 and 501 of 503 |

  The most usual tasks are `POI_LookAround` (2,687), `POI_Graze_LOOP` (2,020),
  `POI_Broken_Standing_Weapon_Out_LOOP` (1,457), and `POI_Sit` (1,224): they are names of
  the animation databases of the creatures (an ambient action with a tag), not names of the
  action table, thus the backend plays only the 23 tasks whose name is an action of the
  table. The start wolf (`8815.25 4319.13`) has a loop of four waypoints:
  `8812.751 4319.135` (50 degrees, 20 s to 30 s, `POI_Sit`), `8817.751 4319.135` (270
  degrees, 10 s to 15 s, `POI_Eat`), `8815.251 4321.635` (45 degrees, 30 s to 40 s,
  `POI_Lay`), `8815.251 4316.635` (180 degrees, 30 s to 40 s, `POI_Sleep`). The backend
  refuses a file whose routes do not fit together, as a whole: a route index outside the
  list, a route with no waypoint or with more than 64, a waypoint more than 200 m from its
  spawn point, a value that is not finite, a wait that is not "both values negative" or "0
  to 3,600 s with the second value not less than the first one", a task of more than 128
  characters, an ambient speed or a wander distance outside its range, and a nearest wander
  distance above the farthest one. A file of format 2 works as before: the creatures have
  no route and do not wander.
- **The picture check of the slices** (stage 3.4c, `tools/research/creature_tour.py`;
  evidence `~/Games/soulwarden/work/evidence/world-spawn/implement/tour`). The tour makes one
  creature of each slice 5 m in front of the character on the street of the start place
  (`prepare` adds one kind `Tour_NNNN` for each slice to a file with each slice), takes one
  picture 5 s later, and removes the creature. `sheets` puts 25 pictures on one sheet. A
  person then looks at each sheet. Result for the 553 slices of the world: 476 show a body
  and a name plate with the record of this section and no action; 77 do not.
- **The spawn action.** The action list component of a creature slice has the field
  `m_spawnAction` (the name of an action of its action table: `Spawn`). The field is also in
  the slices that show with no action, thus the field alone does not say which slices need
  it. For 27 of the 77 slices the client shows
  the body and the name plate only after the server started that action: the movement state
  of the creature has the action as a scope record (as for the death action, below) from the
  creation of the creature. Live proof: an Ancient Guardian Reaver shows a blue light and no
  name plate with no action; with the action `Spawn` it has the name plate after 1 s, the
  body stands up, and it stands after 4 s (`W3-1` to `W3-4`); an observer that comes later
  gets the same record and sees the body at once (`W3-5`, `W3-6`: the Reaver at its spawn
  point after a travel). The 27 slices: the Ancient Guardian javelineer (3 slices), reaver,
  and spearman (2); the Anubian guardian brute, reaver, and spearman; the dryad prowler (2)
  and the swamp dryad prowler; the ice dryad frostfang (2); the dune phantom huntress; the
  dynasty maiden (2); the sand and the sulfur elemental shaman; the gold skeleton reaver; the
  undead javelineer; the female damned bowman; the damned longswordsman commander; and 5
  slices of world events. Two of them (the dryad prowler and the dune phantom huntress) show
  the body only 5 s to 15 s after the record: their spawn action is longer. The list is the
  member `spawnAction` of `tools/gamedata/creature_slices.json`: the tool writes the member
  `spawnAction` of these slices, and the backend starts the action for each creature of such
  a slice. They bring 875 spawn points in. The backend does not start the action for the 476
  slices that show with no action: that would change a picture that was checked.
- **Slices that the client does not show** (the member `notShown` of the same file, 50
  slices, 1,541 spawn points). With the spawn action and a wait of 15 s:

  | What the picture shows | Slices |
  | --- | --- |
  | No body and no name plate | `upyr_guardian`, `upyr_mindthief` (2), `upyr_shade`, `upyr_thrall_meatbag_purple`, `upyr_thrall_meatbag_red`; `hercyne_boar` (2), `hercyne_corvid` (2), `hercyne_reindeer` (3); `inquisitor_warden` (2); the purple and the green warden of the catacombs; `sulfurlizard`, `sulfurdragon`, `armoreddragon`, `chameleon`; `season_02_sulfur_elemental_heavy_n1`, `season_02_herikai`, `season_04_frost_king`; `evil_knight_heavy`; `corruptionheavy_dt6`; `halloweenplaguer` (its action table has no action `Spawn` with a fragment) |
  | An effect, no body, no name plate | `corruption_swarmer` (7 slices) and `worldevent_ai_corrupted_swarmer_10223` (a red effect); `upyr_thrall_meatbag` and its green slice (white smoke); `inquisitor_flamekeeper`, `anubianguardian_horus` (2) (a flame); `lost_undead_naga` (a blue light); `dynastyheavy` (2), `corruptionheavy_dt5`, `worldevent_ai_corruption_heavy_10223`, `sulfur_elemental_heavy`, `sand_elemental_heavy`, the dark heavy boss of the catacombs (a weak light) |
  | A body and no name plate | `underworld_catacombs_skeleton_dark_club`, `underworld_catacombs_skeleton_dark_2h_sword` (the slice has no marker component: the marker slot is 0) |

  The largest groups: `sulfurlizard` (423 spawn points), `corruption_swarmer` (266, and 36
  of its other slices), the four meatbag slices (199), `chameleon` (172), the hercyne
  animals (93), `armoreddragon` (71). The cause is not known: the record has the same four
  states as the record of a slice that shows. A slice goes
  from one list to the other only with a picture of the live client.
  Stage QG5c1 [live: a new tour of the 50 slices with the record of stage QG5c1, no spawn action, a wait of 8 s;
  `~/Games/soulwarden/work/evidence/quests/qg5c1/implement/shots/tour/`]: 14 slices show a body and a name plate now and leave the list
  (`anubianguardian_horus`, `worldevent_ai_anubiuan_horus_10306`, `corruptionheavy_dt5`, `corruptionheavy_dt6`,
  `worldevent_ai_corruption_heavy_10223`, `dynastyheavy`, `dynastyheavy_towerbeam_08qp08_elite`, `underworld_catacombs_boss_dark_heavy`,
  `sulfur_elemental_heavy`, `season_02_sulfur_elemental_heavy_n1`, `sand_elemental_heavy`, `evil_knight_heavy`, `season_02_herikai`,
  `season_04_frost_king`); 36 stay. The record changed after the tours of stage 3.4c (the idle record, the status state, the target bits,
  the DLC of stage 1.1b); which change made them show is not proved.
- **Stationary targets** (stage QG5c1; [Creatures](systems/creatures.md)) [data: the slots of the templates; live I4 of stage QG5c1]: a
  slice with a PositionInTheWorld component and no ActionList component (a totem, a nest; the list `targets` of
  `tools/gamedata/creature_slices.json`). Its record is the metadata state (slot 0, the asset of the slice, sub 2), the PositionInTheWorld
  state at its slot (type 13: `01 03 x:f32 y:f32 height:u16 rotation`; the rotation of the yaw of the spawn point), the vitals state, the
  marker state, and the status state at their slots; no movement state. The client shows the body and the name plate, a hit moves the
  health bar, and the death plays a burst; the client removes the body after the DelayedEvent `DestroyDelay` (2.5 s) of the slice.

  | Slice | Vitals | DamageReceiver | PositionInTheWorld | Marker | StatusEffects |
  | --- | --- | --- | --- | --- | --- |
  | `totems/totem_pirate_cursed`, `wex_levelvariants_elite/totem_pirate_cursed_siren_05wc11`, `shatteredmountain/interact/corruptedtotem/totem_corrupted` | 1 | 2 | 3 | 5 | 7 |
  | `pois/brimstone/totem_scorpion_nest_sm` | 1 | 2 | 5 | 8 | 4 |
- The tool stops with status 1 and one message, and writes no file, when a file that a slice
  or an animation file names is not in the pak files or is not correct XML (the grid file,
  the controller file, the fragment file, a file that a fragment file imports, an action
  list). Two cases are usual and give no stop: a slice that names no grid file has no action
  table, and a grid file with no action list has an empty table. No fragment file of the
  creature controllers of the game copy imports a file.
- The backend refuses a file whose parts do not fit together, as a whole (the world then has
  no creature): a kind or a spawn point with a level that the level table does not have; a
  movement, vitals, or marker slot that is not the slot of that component (state type 11,
  15, 5399) in the slot list of its slice; a slice with the null asset id; a kind whose
  health is 0 or not finite for a level of the table; a member `counts` with a wrong form.

**The record of a creature** (behavioral proof: three kinds by command and at their spawn
points, stage 3.4a). The first creature of the live run at its spawn point, a Grey Wolf
(interest 2, 132 bytes; the actor id has the form of 4.2 with the kind 20):

```text
02 04                                              interest 2, 4 fragments
00 0a  01 03                                       metadata: group 0, fields 0 and 1
       33 4d fc 9f ce 8e 5c 71 a7 6d 8e d6 12 b0 60 2b 00 00 00 02      AssetId of the slice, sub id 2
       08 00 00 40 41 5a 4f 54 20 00 00 00 41 5a 4f 54                  GdeRef
05 0b  01 fc 00 0c 00 44 80 03                     movement (slot 5 of this slice): fields 11, 12, 27, 31, 40, 41, 42
       66 ff                                       rotation: a turn of 90 degrees about the vertical axis
       3a                                          lookDir
       46 09 bd 01 45 86 f9 14 37 e1               worldPosAbs 8815.25 4319.13 140.11
       01                                          teleportAndMigrationId
       00 00 00 32 9f fc 08 33                     timeAnchor
       00 00                                       dataBits, moreDataBits
09 0f  01 bd 44 d3 5c 08                           vitals (slot 9): HealthAmount 1690.876
       00 x 18                                     vitalsData: not dead
       01                                          healthChangeFlags
       44 d3 5c 08  00 00                          HealthMax, HealthTickRate
       7e a1 99 d7 84  4f 97 b6 a8  00 00 00 12    vitalsId, vitalsCategoryId, vitalsLevel 18
       00 00  06 9a                                invulnerability, displayImmuneWhenInvulnerable, maxHealth 1690
0f 97 54  01 01 01                                 marker (slot 15, type 5399): enabled
```

| State (type) | Slot | Fields of the record | Evidence |
| --- | --- | --- | --- |
| Metadata (10) | 0 | `AssetId`: the asset id of the slice with the sub id of the asset catalog (2 for each slice of the file); `GdeRef` | Behavioral: the client makes the slice (the body of the kind shows) |
| Movement, `MB::ALCReplicatedState` (11) | Slot of the action list component | `rotation`, `lookDir`, `worldPosAbs`, `teleportAndMigrationId`, `timeAnchor`, `dataBits`, `moreDataBits` (both 0; from stage 3.3b-M2b `dataBits` 0x03, the target bits of 4.34) | Behavioral: the creature stands at the position, turned as the rotation says, and it plays its idle animation |
| Vitals (15) | Slot of the vitals component | Group 0: `HealthAmount`, `vitalsData`, `healthChangeFlags`, `HealthMax`, `HealthTickRate`, `vitalsId`, `vitalsCategoryId`, `vitalsLevel`, `invulnerability`, `displayImmuneWhenInvulnerable`, `maxHealth` | Behavioral: the name plate with the name of the category row, the level, and the health bar |
| Marker, `MB::MarkerComponentReplicatedState` (5399) | Slot of the marker component | `enabled` (1 byte) | Behavioral for the record with it (the name plate shows); a record with no marker state was not tested |

The slots of the three checked slices: rabbit (`rabbit_snowshoe_8`) movement 2, vitals 5,
marker 17; Grey Wolf (`wolf_grey_5`) 5, 9, 15; Risen (`risen`) 5, 10, 15.

Live results (stage 3.4a; the live checks A1 to A6 of the research):

- **No action field is necessary for the idle pose.** With the record above, the rabbit, the
  wolf, and the Risen play their idle animation (the ears of the rabbit and the head of the
  wolf move between screenshots). The research expected a body in its bind pose. A creature
  has no script layers: its action list selects the action `Idle_No_Task` of its grid file
  itself. Do not send the state ids of a player (4.9) for a creature.
- **The name plate.** The name is the name of the category row (`vitalsCategoryId`):
  "Chalky Rabbit" for the category `Rabbit_Snowshoe_8`, not "White Rabbit" of the vitals row;
  "Fallow Scavenger" for the Risen. The plate shows at 6 m with no crosshair on the creature
  (the marker of the slice has the near range 6 m). The plates of the wolf and of the Risen
  have the level mark (18). The plate of the rabbit (creature type Critter, level 1) has no
  level mark.
- **The position is exact.** The client does not move a creature to the ground. The height
  of a spawn point is the height of the game data: one Risen point is on the wood floor of
  a hut, 0.87 m above the terrain, and the Risen stands on the floor.
- **The rotation.** A turn about the vertical axis by the angle `a` is the quaternion (0, 0,
  sin(a/2), cos(a/2)) in the compressed form of 4.3. The angle 0 looks north, 90 looks west,
  270 looks east (screenshots of a wolf with 270 on a street to the west of the camera).
- **A health change needs bit 0 of `healthChangeFlags`.** A fragment with only `HealthAmount`,
  and one with `healthChangeFlags` 0, change the value of the client and not the bar of the
  name plate (3 tries: one half, a raw fragment, the health 0). The client gives a health
  change to the listeners of the vitals component only when bit 0 of the flags is 1 (static
  proof: facet +0x218, test at 0x689FDC0 after the apply function 0x67A7560). With the bit,
  the bar shows the part at once. Bit 1 is for the health gain number of the local player:
  the backend does not set it. A health change of a creature (29 bytes for interest 2):

  ```text
  02 01  09 0f  01 0d  43 fa 00 00  00 x 18  01      HealthAmount 500, vitalsData, healthChangeFlags 1
  ```
- **The dead mark.** Bit 0 of the flags byte of `vitalsData` (4.5; the 18 bytes are the flags,
  two times of 8 bytes, and a count). With it the client plays the death effect of the slice
  (blue particles), gives the body the death material (white), and takes the name plate away
  some seconds later. The body does not fall: it keeps its idle pose. The client removes the
  body itself some minutes later (the slice has a delayed event of 300 s; in two runs the
  body was gone 5 minutes after the mark). The death of the player is not a part of this.
- **The stop.** A stop of the interest number (4.2) removes the body and the name plate at
  once. A new creature can use the number again.
- The game log has no line for a creature record, and no stat warning: a creature needs no
  stamina field and no mana field.

**The action of a creature** (static proof `enemies-start-area.md`, section "Action
indexes"; behavioral proof for the death action of the Grey Wolf and for the two index
rules). A creature slice has `m_useSlayerScript` false: its movement state has no script
layers. An action that the server starts is a scope record:

```text
group 0 of the movement state:
  32 scopeData        size code: a mask of the scopes that have a record (bit 0: scope 0, "FullBody")
  33 scopeInfoBlob    size code: byte count; then one record for each scope of the mask:
                        start count:u16   fragment index + 1 (size code)   action index + 1 (size code)
                        playback speed (16-bit float, 0x3C00 is 1.0)   option:u8
                        [slave fragment index + 1 (size code), only for an action with ReplicateSlaveFragment]
  29 scopeTimeBlobEx  size code: byte count (0), then the times of the scopes 2 and more.
                      The client reads the scope fields only when the state has this field.
  34 globalFragTags   12 bytes: the tag state of the fragments
  28, 38, 49, 50      one byte each, the low byte first: the time of the action of scope 0, in ticks of 1/30 s

the death of the Grey Wolf (interest 3, 90 bytes: the movement state and the vitals state):
03 02
05 0b  01 fe 00 18 00 b8 47 07 06                  fields 11, 12, 27, 28, 29, 31, 32, 33, 34, 38, 40, 41, 42, 49, 50
       66 25  3a  46 0a ac 00 45 84 d7 44 37 70    rotation, lookDir, worldPosAbs
       00  00                                      time byte 0, scopeTimeBlobEx with the count 0
       01  01                                      teleportAndMigrationId, scopeData: scope 0
       07  00 01  05  1c  3c 00  00                scopeInfoBlob: start count 1, fragment 4 + 1, action 27 + 1, speed 1.0, option 0
       00 x 12                                     globalFragTags
       00  00 00 00 32 a6 89 83 9c  00 00  00 00   time byte 1, timeAnchor, dataBits, moreDataBits, time bytes 2 and 3
09 0f  01 0d  00 00 00 00  01 00 x 17  01          vitals: HealthAmount 0, vitalsData with the dead mark, healthChangeFlags 1
```

- Result: the wolf falls on its side and stays on the ground with the death material
  (screenshots 0.8 s, 3 s, and 8 s after the kill). The research had no live proof for a
  scope record.
- **The client keeps the scope fields** (behavioral proof, user test 1). A state that does not
  have the fields 29, 32, 33, and 34 leaves the scope record of an earlier state in the state
  of the client, and the client starts the action of that record again each time that it
  applies a later state after the end of the action: a Grey Wolf that runs after its bite
  plays the bite again and again while it moves, and a Reaver that walks plays its spawn
  action (its light and its rise from the ground) at each step. A record that comes again in
  each state with a new time makes the body jump to that time each 200 ms: the user saw the
  creatures shake. The rule of the backend (`creature_scope`, `Known::scope`): a session keeps
  the start count of the record that it sent last for each creature (0 for the fields with no
  record), and a movement state has the scope fields only when that changes. The fields with
  no record are `29 00`, `32 00`, `33 00`, and the 12 tag bytes of the slice: the client then
  ends the kept action and plays its idle action. The first record of a creature with no
  action has these fields too (147 bytes instead of 132 for the record of case CR2).
- **The locomotion action** (behavioral proof, user test 1). The client plays no walk and no run
  for a creature whose position changes with no action: the creature slides in its pose. The
  action `Move_Combat` of the action table of the slice (410 of the 412 slices that fight
  have it, with a fragment; the two others are tendrils that do not move) is a blend of the
  locomotion of the slice that the client selects from the speed of the creature: a Grey
  Wolf gallops at its run speed and stands at the speed 0, and a Reaver takes steps. A
  creature on its way has this action from its first step (`Creature::moving`), in one record
  with one start count for the whole way; an attack replaces it, and the arrival at the home
  ends it (the fields with no record).
- **The walk of a roam** (stage 3.4e; behavioral proof, check `C1`). Out of a fight a
  creature walks with the action `Move` of its action table (`CreatureSlice::walkAction`; 501
  of the 503 slices of the file have it), with the ambient speed of its slice: a Grey Wolf at
  1.75 m/s walks with its head up and its legs move with the speed. With `Move_Combat` at the
  same speed it walks low, in the stance of a fight. A patrol and a wander use `Move` (one
  record for each walk to a waypoint or to a place of a wander, and the fields with no
  record at the waypoint); the way to a target and the way back use `Move_Combat`. A new
  record goes out only when the action changes (a walk that becomes a run). Corrections of
  the review of the corrections of user test 1: a step ends a reaction that plays also for
  a slice with no locomotion action (finding F7, case RU4: before, the reaction stayed in
  the state of the client); the death of a creature with no death action ends the kept
  locomotion record of each session (F2, case RU2: before, the death marked only the vitals
  state, and a session that had the run kept it, while the world had no action).
- The backend sends the full movement state with the scope fields (the position and the
  rotation again). The time bytes are the time from the start of the action to the state: a
  session that observes a dead creature later sends the time that passed, so that its
  client shows the end of the action.
- The spawn action of a slice (stage 3.4c, the list above) is the same scope record, with the
  index of the action `Spawn` and of its fragment, in the first movement state of the
  creature. Its time is 0 in the first state of each session (user test 1). Before, it was the
  time from the creation of the creature, and a session that observed the creature some
  seconds later showed only the light of the action and no body: an Ancient Guardian
  Spearman 80 s after its creation (a light that flickers, 65 to 163 pixels in cycles of 0.3 s
  to 0.5 s), the Reaver of the Tomb of the Old Warrior after a far travel, and "Edmand the
  Fisher" (an undead javelineer) at 4.6 m. With the time 0 the body shows after the light,
  also for a session that comes later. From stage 3.4b the spawn action is a value of the creature for its whole time in the world
  (`Creature::spawnAction`), not the action that plays: the first movement state of each
  session for the creature has it, and an action with an end (the reaction to a hit, an
  attack, the action of a command) does not take it away (4.17). A dead creature has its
  death action in the first state of a later session; a dead creature of a slice with no
  death action has its spawn action there, so that its body stays for a later observer,
  also when it died while its reaction played (finding C4 of the review of stage 3.4c, case
  WS4). The action command with no time (`tools/world creature action NAME`) starts an
  action with no end: the first state of a later session then has that action for a slice
  with no spawn action.
- **The action that plays after a first record with the spawn action** (finding F3 of the review
  of the corrections of user test 1, case RU3; `kSpawnLead` in `rep_session.cpp`). A first
  record of a creature of such a slice has the spawn action with the time 0 (finding F9, case
  RU10: the wire time is 0, and `CreatureAction::started` keeps the time of the creation). When
  the creature plays a different action at that time (a walk of its patrol, an attack, the
  run of a fight), the session sends that action 1.5 s after the first record: the client
  shows the body first, and then the action. Before, the session sent only the spawn record:
  with no later change of the place or of the direction, the observer missed the whole
  action. A later state of the creature in the 1.5 s (a step of a walk: a patrol sends one
  each 200 ms) has the position and no scope fields (`PartsRequest::keepScope`): the client
  keeps the spawn action, and the action that plays follows at the end of the 1.5 s (finding
  F3 of the review of stage 3.4e, case RP3; before, the first step of the walk sent the walk
  at once and the lead was gone).
- **The index of an action** is its position in the action table that the client makes for
  the slice (builder 0x4840D90; the table is at movement component +0xB58, one action has
  0x1A0 bytes: name +0x90, index +0x130, fragment name +0x138, fragment index +0x164). The
  rule: the action lists of the element `ActionLists` of the grid file in their sequence,
  and the action definitions of each list in their sequence; a definition is in the table
  only when the alias map of its list in the grid file names it (`<Action ActionName=...>`).
  The research had the sequence and not the alias map condition: its candidate values were
  correct only up to the first definition that the grid does not name (the first 20 of the
  wolf). Live proof: the tables of three kinds from the memory of the client are equal to
  the tables of this rule: Grey Wolf 50 actions (68 definitions), Risen 62 (78), rabbit 19
  (35).
- **The index of a fragment** is the position of the fragment name in the fragment file of
  the controller file of the slice (`<Fragments filename=...>`; the tags of each imported
  file first, a name one time). Live proof: 117 fragment indexes of the three kinds are
  equal (46, 55, 16); an action whose fragment name is not in the file has no fragment
  (0xFFFFFFFF in the client).
- The creature data file has the action table of each slice (`actions`: name, fragment name,
  fragment index, the mark `ServerStarted`). `tools/research/creature_actions.py` reads the
  table of the live client and compares it with the file.
- The values of the Grey Wolf (`wolf_grey_5`), read in the live client:

  | Purpose | Action | Action index | Fragment | Fragment index | `ServerStarted` |
  | --- | --- | ---: | --- | ---: | --- |
  | Idle | `Idle_No_Task` | 36 | `Idle` | 0 | no |
  | Movement | `Move` | 41 | `Move` | 64 | no |
  | Movement in combat | `Move_Combat` | 42 | `Move_Combat` | 65 | no |
  | Default bite | `Attack_Bite` | 8 | `Attack_Bite` | 5 | no |
  | Reaction to a hit | `Reaction` | 25 | `r_R2_F` | 15 | yes |
  | Death | `Death` | 27 | `r_Death` | 4 | yes |

  The Risen (`risen`): `Idle_No_Task` 0 / `Idle` 0, `Move` 50 / 46, `Attack_Swipe_L` 14 / 99,
  `Reaction` 39 / `r_R2_F` 12, `Death` 41 / `r_Death` 3. The rabbit (`rabbit_snowshoe_8`):
  `Death` 0 / `r_Death` 2, `Idle_No_Task` 8 / `Idle` 3, `Move` 10 / 6, `Reaction` 12 / 5.
- The death action of a slice is the action `Death` with a fragment, or `Death_FB` for a
  slice with no such action (13 of the 70 slices of the file; not tested live).
- More actions of the Grey Wolf as a scope record, each one with the option 0 and with 12
  tag bytes of 0 (one live run, with the development command `tools/world fragment`; the
  start count was 1, 2, 3, 4): `Attack_Bite` (8, fragment 5) plays the bite: the wolf goes
  forward with its head down and its mouth open. `Reaction` (25, fragment 15) plays a
  reaction to a hit: the wolf goes down on its front legs and comes up again. `Move` (41,
  fragment 64) is accepted and shows a standing pose with no change of the position (the
  fragment is a blend by the speed: the movement of a creature is stage 3.4b).
  `Idle_No_Task` (36, fragment 0) after them gives the idle pose again. The backend sends
  none of these actions in stage 3.4a. From stage 3.2a the backend sends the action
  `Reaction` of a slice when its creature takes a hit (4.15).
- **A creature that walks** (behavioral proof, stage 3.2a, one Grey Wolf, the development
  command `tools/world fragment`). A sequence of movement states with only the position
  (`worldPosAbs`), the rotation, and the time anchor makes the creature go from one position
  to the next one with moving legs: the client selects the movement action itself from the
  change of the position. 19 states in 3.7 s (one each 200 ms) made the wolf walk 6 m at
  1.75 m/s in a low walk, and 7 states in 1.3 s made it run 7 m at 6 m/s. The states had no
  action field. The rotation of the states turned the wolf to the direction of the path.
  The entity of the backend did not move in this check. From stage 3.4b the behavior of a
  creature sends such a sequence, and the entity moves (4.17).

- **The end of an action, the idle record** (user test 2, UT2-2; static proof of the research `user-test-2.md`: the constructor of an
  action of an action list 0x2A5D630 sets NoAutoBlendOut, the scope code 0x6D899DD and 0x6D87DA8 ends an action at the end of its
  fragment only when `(flags & 0x1002) == 0x1000`, 0x4838EE0 selects the action whose name hash is 0xDC24B515 (CRC-32 of "idle"),
  0x4872130 plays it only after a pause of the server states, and 0x2A2BC20 (0x2A2C0D5 to 0x2A2C122) calls ForceFinish on the action
  of scope 0 at each tick while the newest state has the scope fields with no record; live proof of the run `u2-after`). The client
  keeps the last pose of an action of the server after its fragment, and with the fields with no record it ends its own idle again
  and again while states come (the up and down of a ghost). The backend gives the creature the record of the action `Idle` of its
  slice (a new start count; its fragment loops) at the end of an action with an end, at the end of a walk, and at the end of a stun.
  Live: a Fenwallow Faminegeist after its attack (`Ghost_Starved_33:3`, `Attack_Stab_R` 1.30 s) kept one bent pose from +2.0 s to
  +2.8 s before the correction and played its idle after it; a Grey Wolf crouches in its idle after its bite. A slice with no action
  `Idle` keeps the rule from before.
- **The record of a timed action in each state** (user test 2, UT2-3; static proof: the copy 0x2A2ACD0 keeps the record of an earlier
  state, the target state 0x48709A0 interpolates the time of the record (0x2A222E0), and the apply 0x2A2BC20 starts a one-shot fragment
  again (IAction::SetFragment 0x2A67B30) when the time of the record minus the time of the fragment is less than -0.5 times the length
  of the fragment (0x2A2C28D, the constant -0.5 at 0x7F4016C); live proof of the runs `u2-before` and `u2-after`). While a one-shot
  action of the server with an end plays, each movement state has its record again: the same start count and the time of the state.
  Live: one heavy arrow on an Inquisitor Cleric played three reactions (0.55 s apart) before the correction and one after it; one
  arrow on a Grey Wolf one reaction. No body jump was seen (the risk of the research).
- **The reaction to a hit** (user test 2, UT2-9; game data: the action `Reaction` of `baseai_reaction.actionlist`, its table
  `baseai_reactiontableharness.actionlist`, the action `Pushable` of `baseai_pushable.actionlist`, the status rows
  AI_Reaction_Limiter_Gate and AI_Reaction_Limiter, the cells NoReaction, PowerLevel, HitStun and their "_AI" cells of the damage
  table; live proof of the run `u2-after`): the rules are in [Creatures](systems/creatures.md). Live: a light attack of the great
  axe on a Faminegeist plays power 2 (`r_R2_F`, 0.83 s, a hit stun of 0.77 s, a push of 1.00 m in 0.82 s), its charged heavy attack
  power 3 (1.43 s, 0.93 s, 1.98 m); a heavy arrow on the Cleric power 3 (1.33 s, 1.33 m); an Elite takes no reaction and no push from
  the attacks of the war hammer and the great axe. Not known: the unit of HitStun_AI (taken as frames of 1/30 s). The semantics of
  Chained_Reaction_Count (a value of the behavior tree): stage U3 takes the reading of the data (each full reaction since the last
  ResetReactionCount counts; a decision of the orchestrator).
- **The reaction of stage U3** (research `creature-combat-feel.md` Q2, Q3, Q5; static proof of the client code and the game data,
  live proof of the runs `u3-after*`; the rules are in [Creatures](systems/creatures.md)). The side: the client selects the first row
  of the reaction table of the action whose power and angle condition hold (0x2B9F1C0); the angle of the damage context is from the
  front of the creature (0x2C07B70), positive to its right: live check LC-Q2-1 (a light arrow from the right and from the left turned
  the wolf 45 degrees to the source by AlignToDirection, AlignOffsetAngle 45 and -45 of the wolf rows). The server sends the fragment of the row as the fragment of the
  scope record of the action `Reaction`. The length: the clip HitStun (0x2559640) sets the play speed of the rest of the fragment so
  that it ends at HitStun / 30 s (speed 0.001 to 3.0); live: the Cleric under a heavy arrow 0.73 s and 0.73 m (before 1.33 s and 1.33
  m), and it came to its cast distance under a heavy arrow each 2 s from 22 m.
- **The additive scope record** (stage U3; static proof: the copy of the state 0x2A2ACD0, the record reader 0x2A75F70, the reset of a
  scope with no bit 0x2A72DC0, the time reader 0x14087AE90; the scope order of the controller files; live proof LC-Q5-2). The action
  `Reaction_Additive` plays in the scope "Additive" of the controller of the slice (the wolf: FullBody, Head, Tail, Mouth, Additive,
  ... : scope 4; the Cleric: scope 5). Its fields:

  ```text
  32 scopeData        the mask: bit 0 when an action of scope 0 plays, and bit N of the additive scope
  33 scopeInfoBlob    byte count; the records in the order of the scopes (scope 0 first, then scope N)
  29 scopeTimeBlobEx  byte count, then for each scope 2 or more with a record, in order: its time as a 16-bit float, big-endian,
                      in seconds (0x14087AE90: ntohs, half to float). A record with no time stops the read of the later scopes.
  ```

  The times of scope 0 stay in the fields 28, 38, 49, 50. The client keeps the record of a scope with no bit no more (0x2A72DC0): the
  state after the end of the additive reaction has no bit N. Live: the client accepted the record of scope 4 at rest and in a run (no
  log line of the client, no restart of the run, the flinch of the head and the shoulders at rest for 0.7 s). Encoder:
  `server/rep/states/movement_state.cpp` (`write_scope_record`, `write_scope_time`); test case CF5 (the mask 0x10 with no action of
  scope 0, one 16-bit time, A and B the same bytes).
- **The homing of a creature attack** (stage U3; research Q1; static proof 0x255A140, 0x2564160, 0x255DA20, 0x2565210, 0x25653F0,
  0x25666A0, 0x2A21FB0; live checks LC-Q1-1 and LC-Q1-2 with a trace of these addresses). The client enters the Homing clips of a
  remote creature and asks for its AI target, but it does not call the move (0x2565210, 0x25653F0) or the accumulator (0x2A21FB0): a
  creature has no AI target in the client, and the movement step runs only for the local player. It plays a remote lunge in place (no
  root motion in the pose: LC-Q1-1, result (b)). Thus the server moves the creature in the clips with the time rule of the client code
  ([Creature behavior](systems/creature-behavior.md)), and each observer gets the places in the movement states with the attack
  record. Live: the bite of a wolf from 1.6 m ended near the character (before, the body came back to its place); a ghost followed a
  side step of 1 m.

**The world model for creatures** (project rules, roadmap MP-1 to MP-8).

- A creature is an entity of the entity table with the kind "creature": the kind and the
  slice (pointers into the creature data), the level, the direction, the life state, the
  time of the death, and the action that the server started; the health is in the vitals of
  the entity. It has no character, no account, and no session. The world gives it an actor
  id of the range of other entities (4.2: kind 20, serials from `0x20000000`). The creature
  system (`world/creatures.h`) is the only code that makes a creature, changes its health,
  and removes it.
- **Spawn point rule.** Each 500 ms of the world clock: the world makes the creature of a
  spawn point when a player entity is 150 m or less from the point (horizontal), and it
  removes a creature of a spawn point when no player entity is 200 m or less from the point.
  The two distances are distances of the spawn point, not of the creature (stage 3.4b): a
  creature that goes to a player and away from its point stays in the world (4.17). A
  spawn point has one creature at most. With no player in the world, the world has no
  creature of a spawn point. The first value is larger than each interest distance: a
  creature is in the world before a session can observe it.
- **The cost of the spawn point rule** (stage 3.4c). The file has 32,840 spawn points, and a
  scan must not look at each of them. The points are in a grid with cells of 64 m
  (`world::SpawnGrid`): a scan looks at the cells that are 200 m or less from each character
  of the world (one walk for the two distances, stage 3.4b; 49 cells at most for one
  character), and then at each creature of the world: the creatures of the world are the
  creatures of the points near a player, and the creatures of commands. The points that
  wait (a dead creature, a respawn time) are in their own set. The cost of a scan thus depends on the number of players and on the points near
  them. Measured in the test program (case WS2, a made-up world of 33,856 spawn points; the
  values are from one run and change from run to run): with 1 player a scan looks at the
  points of 36 cells at most (25 points), and 200 scans need 0.2 ms to 0.3 ms; with 50
  players at 50 places a scan looks at 1,096 points, and 200 scans need 25 ms to 31 ms
  (0.13 ms to 0.16 ms for each world tick with its scan), against 1,057 ms to 1,395 ms for
  200 scans of all points for each player. The case has a time limit that a scan of all
  points does not meet (less than one tenth of that time, and less than 400 ms). Live (run
  `w6`, one player, 8 travels through 5 territories in 5 minutes, 32,216 ticks): a tick of
  the game server needs 55 µs on average and 17.1 ms at most, a tick of the world 9.5 µs
  and 15.3 ms at most, and a scan 50 µs and 5.4 ms at most; 698 scans looked at 50,052
  points (72 for each scan). The largest values came in the runs with travels: the scan of
  a travel makes 40 to 60 creatures and removes as many, and the world writes the position
  row in that tick. A run with no travel (219,317 ticks, one player at one place, the
  picture tour) had 5.9 ms as its largest tick of the game server. `tools/world list`
  prints these values (the line `load:`).
- **A point that waits.** A creature of a spawn point that died, or that a command removed,
  does not come back by the distance rule. The point is free again after the command
  `reset`, or at its respawn time on the world clock (`Creatures::respawn_at`): from stage
  3.5a the death of a creature gives its point the respawn time of its spawner (4.21); a
  point whose creature a command removed waits for `reset`. The body of a dead creature
  stays in the world until its respawn time, a command, or the distance rule removes it. At the respawn time of
  a point, when a player is 150 m or less from it, the point first removes the body of its
  dead creature and then makes its new creature (the count `removedByRespawn`). With no
  player that near, the body stays and the point waits.
- **The records of a creature.** The fragments of a creature make records by the rule of each
  entity: a second fragment for one slot (a fragment of a development command), and a
  fragment that makes the record larger than one carrier message, go in a later record, and
  each later record of a creature starts a new bundle.
- A slice of an alias with more than one slice is selected with the percent values of the
  alias, from a count (the same sequence in each run).
- **Interest.** A session observes the creatures near its character in each start (the
  option `--observe-players` is only for the characters of other players). A creature comes
  into the interest table when it is not farther than `m_sliceSpawnRadius` of its slice
  (game data: rabbit 35 m, Risen 82 m, wolf 100 m; not less than 20 m, not more than 100 m),
  and it goes out when it is 20 m farther than that, or when it leaves the world. One scan
  (each 500 ms) takes 16 new creatures at most, the nearest ones first. The records of
  creatures go together in bundles of 960 bytes at most (7 full records): the live run had
  7 records in a bundle of 922 bytes at a world entry.
- **Client messages for a creature.** The route of a client message can be the actor id of a
  creature of the interest table: the backend then finds the target among the faceted
  components of the slice of the creature (the id rule of 4.2 with the root entity of that
  slice; the creature data file has the ids). In the live run the client sent no message
  for a creature: with the crosshair on a wolf for 2 s it sent only input reports (type
  1589, 70 reports of 284 bytes), and for one light sword attack at 1.4 m from the wolf it
  sent only input reports (52 reports of 284 to 415 bytes). It sent no hit message.

**Commands.** Development routes (`tools/world`): `GET /v1/dev/creatures[?character=ID]
[&range=N]`, `GET /v1/dev/creatures/kinds[?q=TEXT]`, `POST /v1/dev/creatures/spawn?character=ID
&kind=KIND[&level=N][&dx=N&dy=N&dz=N][&yaw=N]`, `POST /v1/dev/creatures/remove?creature=ID`,
`POST /v1/dev/creatures/health?creature=ID&health=N` (or `percent=N`), `POST
/v1/dev/creatures/kill?creature=ID`, `POST /v1/dev/creatures/reset`, `POST
/v1/dev/creatures/action?creature=ID&action=NAME[&seconds=N]` (stage 3.4c: it starts an
action of the action table of the slice by its name, with no end or with an end after the
time; `tools/world creature action`; reply 404 for a name that the table does not have or
that has no fragment), and `POST
/v1/dev/world/fragment?creature=ID&slot=N&type=N&state=HEX`. The admin interface has the same
commands (6.4), but not the action command. The character of a spawn command is the reference place: the new creature
is at its position plus the distance (east, north, up; with no distance 3 m to the north).
The two route sets have different rules for a change of a creature (remove, health, kill):
a development route changes each creature of the world, with no character. An admin command
changes only a creature that is 150 m or less from its character, which must be in the
world (6.4). The health command with `percent` 100 gives the exact maximum health.

**Not known, not done.**

- The side fragment of a reaction whose slice has no variant of it (live check LC-Q2-2, not done): the backend then sends the front
  fragment of the same power. The homing of a creature attack is done in stage U3 (above); the root motion of a remote creature is
  not done (LC-Q1-1: the client plays the pose in place, thus not needed).
- The option index and the tag bytes that the client itself has for an action. The scope
  record of the idle action that the client selects itself was not found at the decoded
  offsets in the first 0x2400 bytes of the movement facet and of the movement component.
  The option 0 with no tag bit has live proof for the death, the bite, and the reaction of
  the Grey Wolf. A slice with a weapon needs the tag bits of its weapons for its attack
  fragments: stage 3.4b found the rule of the bits and sends them (4.17, live proof for the
  Ancient Guardian Reaver). A kind with a different variant tag (a Risen with one arm) was
  not checked.
- 50 slices show no body or no name plate, also with their spawn action (the table above;
  1,541 spawn points). Stage 3.4a found five such slices in the start area: the spawn action
  corrected the four Ancient Guardian slices, and `corruption_swarmer` stays (a red effect).
  Not tested for them: a different action of their tables, the other components of their
  slices (a state that the backend does not send), and a longer wait than 15 s.
- A creature of a slice with a spawn action: the client plays the spawn action one time and
  then stands. Stage 3.4b checked what the client shows when the server starts a second
  action after it: an Ancient Guardian Reaver walks, plays its attacks, and stands again,
  and it does not go back into the ground (4.17). If the client plays an idle animation
  after the spawn action was not checked.
- The picture check has one picture of each slice, with one creature of one kind and one
  level. A slice whose kinds have different variants (tags of the action fragments) can
  show a different body for a kind that the tour did not make.
- The death action `Death_FB` of 13 slices, and the death of a kind with more than one death
  fragment (a death from the front or from the rear).
- What the client does with the states that the backend does not send (the stamina
  component, the status effects, the paperdoll of a creature with a weapon).
- The marker state: a record with no marker state was not tested.
- The time of the body of a dead creature in the client (about 5 minutes) was not measured
  with a clock.

### 4.15 Hits: the melee contact report, the hit check, the damage rule, the damage number

Research files: `~/Games/soulwarden/work/research/melee-mode.md`, `hit-reports.md`, `damage.md`,
`combat-actions.md`, and `enemies-start-area.md` (static proof and game data, each one checked
by a second agent). Evidence of the live runs of stage 3.2a:
`~/Games/soulwarden/work/evidence/hit/implement/`. A statement with the mark "project rule" is a
decision of this project, not a contract of the client: the client has no copy of the hit
rules and of the damage rule of the original server.

**How the server finds a hit** (behavioral proof). With the setting of 2.3 on, the client
finds each melee contact itself (the weapon shape of the attack touches a hit volume of an
entity) and puts one item into the third list of the input entry of that simulation step
(4.7). The server finds no contact itself.

| Attack of the player (a sword, a Grey Wolf of a command) | Items of the third list |
| --- | --- |
| One light attack that touches the wolf (1.8 m after the step forward) | 1 |
| One light attack in the air (no creature) | 0 |
| One light attack with the wolf 10 m away (7.9 m after the step forward) | 0 |
| One light attack that touches the wolf, with the setting off | 0 |
| Three attacks on the body of a dead wolf | 0 |

- The item of a contact (96 bytes): the provider is 0xFFFFFFFF in each live contact (about
  100 contacts of 5 weapons). The two references name the same entity: the actor id and the
  entity group id of the creature. The facet target of the second reference is a hit volume
  component of the slice of the creature, by the id rule of 4.2 with the root entity of that
  slice (the wolf: the entity `Head` for a light sword attack, a different entity for the
  thrust of a heavy attack). The point is a position of the world at the creature (0.9 m to
  1.7 m from the position of the wolf, which is at its feet), and the normal and the
  direction are unit vectors.
  The item has no damage value, no attack row, and no time.
- The entry of a contact also has the position of the character (one entry has both fields).
- The client sends an entry in 5 reports (4.7): the session gets each contact 5 times.
- The client sends no other message for an attack: no message with the route of the creature.
- The client shows its own contact effect (a flash at the creature) with no reply of the
  server. It shows no number and no change of the health bar itself, and the creature plays
  no reaction itself.
- A creature has no such report for the player: while a wolf played its bite at the
  character (a scope record of a development command), the client of the player sent only
  input reports with no list item. The world tests the contacts of an attack of a creature
  itself (stage 3.4b, 4.17).

**The attack inputs** (behavioral proof; the bit numbers are static proof of
`combat-actions.md`, each one seen in the captures). The left pointer button sets these bits
of the action words (4.7):

| Bit | Name of the input profile | When |
| --- | --- | --- |
| 555 | `attack_primary` | One input step: the button goes down. Bits 203 (`attack_primary_virtual`) and 408 (`attack_shoot`) come with it. |
| 191 | `attack_primary_hold` | Each step while the button is down, from the step after bit 555 |
| 567 | `attack_primary_release` | One input step: the button goes up |
| 9, 40 | `Use_MainHandSlot_1`, `quickslot-weapon1` | Key 1: the weapon of slot 24 |
| 355, 306 | `Use_MainHandSlot_2`, `quickslot-weapon2` | Key 2: the weapon of slot 25 |

The client starts each attack itself, from the action lists of the weapon. The contact step
(input steps of 1/30 s; live data of a sword, a great axe, a hatchet, a war hammer, and a
spear on a Grey Wolf):

| Input | Attack of the client | Contact |
| --- | --- | --- |
| A click: the button goes up 1 to 8 steps after it went down (the hold bit is on for 7 steps at most) | One light attack | 5 to 12 steps after the button went up (7 to 19 steps after it went down). In a chain of clicks: 7 to 9 steps after the button went down. |
| A hold: the button goes up 9 steps or more after it went down (the hold bit is on for 8 steps or more) | A heavy attack | Sword: 15 to 18 steps after the start when the button goes up at 9 to 12 steps, and 24 or 25 steps for a longer hold. Hatchet 28, great axe 32, war hammer 32 or 33, spear 35 steps. |
| A long hold (4 s) | The client attacks again and again while the button is down, and one more time after it went up | Sword: 24, 64, and 103 steps after the start, and 10 steps after the button went up. War hammer: 33 and 81 steps, and 9 steps after the button went up. |
| Fast clicks (11 steps from click to click, hatchet) | One light attack for each click | 11 to 13 steps from contact to contact: the contact of a click can come in the step of the next click |

**The hit check** (project rule; `world/combat.h`). A contact is a claim of a client. The
session gives each claim to the world (`Combat::claim`) with the values of its client
(`world::AttackTrack`: the attack inputs, and the counts). The two limits of the check (the
claims of one time window, and the last hits of each creature) are values of the character,
not of a session: the combat system keeps them with the character id as their key
(`world::HitLimits`, `Combat::limits_of`). A new session of a character, and a character that
leaves the world and enters it again, have the limits that the character has (case HT14;
before stage 3.4c each new session had new limits, and 13 new sessions of one character made
13 hits in 0.52 s). The time rule of a hit applies to each session of the character; the
rule of the input steps applies in the session and the input sequence of the hit, because an
input number is a value of one client. The world finds the
attacker from the session (rule MP-5). A claim that the check refuses changes nothing, the
client gets no reply, and the log has a line for the first refusal of each cause and the
counts at the end of the session (`rep: hit claims of the session: ...`). The check, in this
sequence:

| Check | Refusal (log text) |
| --- | --- |
| The backend has combat tables | no combat tables |
| The character lives (stage 3.4b, 4.18) | the character is dead |
| The character made 12 claims or less in the last 1 s of the world clock, in all its sessions | too many claims |
| The entity group id of the receiver is a creature of the world, and the actor id is its actor id | not a creature |
| The group reference is the group of the receiver, and the facet target is a component of the slice of the creature | not a component of the creature |
| The interest table of the session has the creature | not observed |
| The creature lives | dead |
| An attack input fits the contact: the newest one that started 3 input steps or more before the contact is on, or it went off 30 steps or less before the contact and not more than 1.5 s before it on the world clock | no attack |
| The character has a melee weapon in its hand (the use `Combat Melee` of the weapon table), and the weapon has a damage row for the attack | no melee weapon |
| No hit of the same creature was less than 8 input steps or less than 250 ms (world clock) before the contact: one attack hurts one creature one time | the same attack |
| The distance (3 axes) from the exact position of the character (4.7) to the position of the creature is 5.0 m or less: 3.5 m for the reach of a melee weapon and 1.5 m of tolerance for the size of the creature and for the age of the position | too far |
| The point of the contact is a position of the world, 4.0 m or less from the position of the creature | bad point |

- The attack is a heavy attack when its attack input was on for 8 steps or more, and a light
  attack if not (the limit of the client for a sword: 12 live attacks).
- The weapon in the hand: the item of the weapon slot of the last weapon key of the client
  (`Entity::combat.selectedWeapon`), when that slot has an item; if not, the first weapon
  slot with an item. The sheathe bits and the active bits of the paperdoll state do not
  follow the keys yet (4.11).
- The session takes each input step one time: an entry with an input number that is not
  newer than the last one is not a new input, also after a pause of the reports (a contact
  counts one time). When the newest entry of a report is not newer and no newer one came for
  2 s, the input numbers of the client started again, and the session takes the new sequence.
- The largest live distance of a contact was 3.9 m (the heavy attack of a spear), 2.3 m for a
  great axe, and 2.2 m for a sword. The limits do not depend on the weapon or on the creature
  yet: the attack shapes are in the animation files of the game, and the tables have no size
  of a creature.
- The check does not stop a client that sends attack inputs and contacts with no attack on
  its screen. It limits such a client to a creature that it observes, 5 m or less from its
  character, with a melee weapon, and to one hit of a creature each 250 ms.

**The damage rule** (project rule, in one function: `Combat::damage`). The values come from
the game files (`tools/gamedata/combat.sh`, file `combat-tables.json`):

```text
weapon = BaseDamage x gear score factor
core   = weapon x (1 + sum of (attribute factor of the weapon x ModifierValueSum of the
         attribute value) + level damage x (level - 1))
raw    = core x DmgCoef + AddDmg
dealt  = raw x (1 - mitigation) x (1 + weakness) x (1 - absorption), each factor not less than 0
```

Stage 1.3b (4.23): `raw` times (1 + the part of the gear of the attacker: BaseDamage of its
perks, the perks of the hit, the equip load); a gem of the weapon converts a part of `raw` to
its damage type; each part times (1 + the DMG<type> part of the attacker) and the defense of
the target for its type; the mitigation of the target times (1 + the armor part of its
effects: a rend).

| Value | Source |
| --- | --- |
| The weapon of an item | Column `ItemStatsRef` of the item tables: a row of the weapon table (`WeaponItemDefinitions`) |
| `BaseDamage`, the four attribute factors (`ScalingStrength`, ...) | The weapon row |
| The damage row of the light attack and of the heavy attack | Columns `TooltipPrimaryAttackData` and `TooltipAlternateAttackData` of the weapon row: rows of the damage table of the weapons of a player (`DamageTable`). A project rule: the client selects the row of an attack in its animation files, and the weapon table names these two rows for the tooltip of the item. |
| `DmgCoef`, `AddDmg`, the damage type | The damage row |
| `ModifierValueSum` of an attribute value | The attribute tables (`Strength`, `Dexterity`, `Intelligence`, `Focus`) |
| Level damage (0.025), and the gear score: rate 0.0112 for each 5 points above 100, the rate times 0.6667 above 500 | The base attributes file (`playerbaseattributes.pbadb`): `Level Damage Multiplier`, `Base Damage Compound Increase`, `Base Damage Gear Score Interval`, `Min Possible Weapon Gear Score`, `Diminishing Gear Score Threshold`, `Compound Increase Diminishing Multiplier`. A weapon row can have its own rate (`WeaponBaseDamageCompoundIncrease`). |
| The gear score factor | (1 + rate) to the power of the number of full intervals above the lowest gear score, with the smaller rate for the intervals above the limit |
| Mitigation | `PhysicalMitigation` or `ElementalMitigation` of the base vitals row of the creature, by the category of the damage type (table `DamageTypes`); 0 for a type of the category `True` |
| Weakness, absorption | Columns `WKN<type>` and `ABS<type>` of the base vitals row |

- Static proof: the first three lines are the rule of the client for the damage of a weapon
  on its item tooltip (helper 0x5BFB530 for the core damage, 0x3691470 for the gear score).
- Project rule: the last line. The game files have the cells, and no proof of how the
  original server used them (a mitigation value as a part of the damage, the armor rating of
  the level of a creature, the order of the factors). The rule does not use the armor rating
  of the level. Some rows of the game have a mitigation above 1 or an absorption below 0: a
  factor is not less than 0.
- The rule has no block and no stamina. The damage is a 32-bit float with no rounding. From
  stage 3.3b-S5 a hit can be critical (a roll, a hit from behind, a headshot of a projectile:
  4.30); the statuses and the gear change the damage from stage 1.3b and 3.3b-S1 (4.23, 4.24).
- The worked example of the research: the item `1hSwordT1` (weapon `SwordT1`, base damage 64,
  gear score 100), the level 1, strength 5 and dexterity 5, the light row `1H_Sword_Attack1`
  (slash, factor 1), and a target with no defense give 64. A target with the mitigation 0.35
  gives 41.6.
- Live values (the character of the development database: level 26, strength 8, dexterity
  25; the core damage of its sword is 64 x (1 + 0.0085 x 3.75 + 0.0065 x 25 + 0.025 x 25) =
  116.44):

  | Weapon (gear score) | Target | Light attack | Heavy attack |
  | --- | --- | --- | --- |
  | Sword `1hSwordT1` (100) | Grey Wolf (mitigation 0.35, weakness 0.2 for thrust) | 75.7 slash | 109.0 thrust |
  | Sword | Fallow Scavenger, a Risen (mitigation 0.35, weakness 0.1 for strike) | 75.7 slash | 90.8 thrust |
  | Sword | Chalky Rabbit (health 16.9) | 75.7: dead | |
  | Great axe `2hGreatAxeT2` (200) | Grey Wolf | 120.0 slash | 144.0 slash |
  | Hatchet `1hHatchetT2` (200) | Grey Wolf | 100.2 slash | 120.2 slash |
  | War hammer `2hWarhammerT2` (200) | Grey Wolf | 133.0 strike | 172.9 strike |
  | Spear `2hSpearT1` (100) | Grey Wolf | 101.7 thrust | 132.2 thrust |

**The result of a hit.**

- The health of the creature goes down by the damage through the creature system
  (`Creatures::hurt`, which uses `set_health`: one path with the health command and the kill
  command). Each session that observes the creature gets the vitals state of 4.14 (the
  health, the vitals data, the change mark 1).
- A creature that lives after the hit plays the action `Reaction` of its slice (4.14: a scope
  record of its movement state with a new start count; behavioral proof for the Grey Wolf
  and the Risen: the creature goes back and comes to its idle pose again). The action has
  an end on the world clock 1.5 s after its start (project rule: the tool does not read the
  length of an animation): the creature then has no action of the server, and a session that
  observes it later gets a record with no action. The observers get no record for the end.
  A slice with no such action keeps its pose. From stage 3.4b (4.17): a creature that plays
  an attack plays no reaction, and the creature gets the character that hurt it as its
  target (`Creatures::hurt` has the attacker).
- A damage that is not less than the health is the death (4.14): the health 0, the dead
  mark, and the death action. The client sends no contact for a dead creature. For a dead
  rabbit the client shows the name of the creature with the text "No equipped Knife" (the
  skinning interaction of a later stage).

**The damage number** (static proof of the layout in `hit-reports.md`; behavioral proof for a
creature target). The session of the attacker, and only it, gets one message for each hit:

```text
DamageReceiverComponentClientFacet_OnDamageDealt, type 2071     (reader 0x65B4B50, handler 0x686CC50)
  target:u64 entity:u64          the damage receiver facet of the character of the attacker:
                                 the target is the entity group id of the character plus
                                 0xCCC47F92DAF6BFC1 (the id of the component DamageReceiver of
                                 the root entity of the player slice)
  actor id:16 bytes  G:u64       the entity that took the damage
  u32 u32                        two keys (feedback, sound): 0
  flags:2 bytes                  low byte first; bit 0 Ranged, bit 3 DamageCausedDeath, bit 5
                                 CanCrit, bit 6 Crit, bit 8 Headshot, bit 9 IsFromStatusEffect:
                                 the backend sets bit 3, and from stage 3.3b-S5 bits 0, 5, 6,
                                 and 8 (4.30)
  count:size                     1
    type:u8 amount:f32 part:f32  the number of the damage type (slash 4, thrust 5, strike 6),
                                 the damage, and the part of the damage before the defense
                                 that the target took: the backend sends 1
```

The body for 64 slash damage with made-up ids (60 bytes; the bytes of the research fixture
and of case HT4): `ccc47f93daf6bfc3 0000000100000002 04000000010000002000000000000000
0000000100000004 00000000 00000000 0000 01 04 42800000 3f800000`.

- Live result: the number shows at the creature for about 1 s, with the icon of the damage
  type, in white. The client shows the whole part of the amount with no rounding (75.7 shows
  as 75, 109.0 as 108, 133.0 as 132: the float is a small part below the number). For a
  damage type that the creature is weak to (thrust for a wolf), the client shows the number
  in yellow with an arrow mark itself: it reads the weakness from its own tables.
- The message has the damage that the world computed. The client computes none.
- Not tested: the two keys, the effect of bit 3, a part that is not 1, and the message for a
  target that is a player. The flags of a critical hit and of a headshot: 4.30 (live check L6
  of stage 3.3b-S5).

**Not known, not done.**

- From stage 3.3b-M1 (4.32) the server follows the fragment that the client plays for each
  attack, with the row of its strike (the ender, the charged heavy, the sub-hits); the rule of
  this section (8 steps of hold, the light or the heavy row, one hit in 250 ms) stays for a
  weapon with no graph in `weapon-actions.json` and for a backend with no such file.
- The heavy attack of a spear gave a contact in 2 of 10 tries (the wolf was 3.9 m from the
  character at the contact). A sword gives no contact for a creature that is 1.0 m or less from
  the character in a heavy attack: a creature has no body that stops the character.
- The reaction of a creature is the same action for each hit. The reaction tracking state
  (type 1927) and the stagger values of the damage rows are not used.
- Ranged weapons and magic (the first two lists of an input entry), the abilities, critical
  hits, blocks, status effects, stamina, damage to a player, damage between players, and the
  durability of a weapon.
- A size of each creature and a reach of each weapon for the distance check.

### 4.16 Travel: the server moves a character to a different place (behavioral proof)

Stage 3.4c. Research: `~/Games/soulwarden/work/research/death-travel.md` (section 2, static
proof, checked by a second agent). Evidence of the live runs:
`~/Games/soulwarden/work/evidence/world-spawn/implement/` (`W5-*`, `W6-*`, `W7-*`). This is not
the fast travel function of the game (its requests, its costs, and its shrines are system
2.6): it is a command of the backend.

**The movement state is the travel.** The client of the owner takes the position of a
movement state of the server (slot 16, type 11) only when the low 4 bits of
`teleportAndMigrationId` (group 0, field 31) are not the teleport number of its own predicted
state (compare at 0x486D89B to 0x486D8B6). It then copies the server state to its predicted
state (0x486EA80): the position, the rotation, and the look direction, with no step between
the two places and for each distance. The start number of a client is 0, and the first
movement state of a session has 1: the world entry is the same mechanism.

| Step | What the backend sends to the session of the owner | Client code | Evidence |
| --- | --- | --- | --- |
| 1, only for a place that is more than 100 m away (project rule) | Message `PlayerComponentClientFacet_OnTeleportWithLoadingScreen` (type 3951): `target:u64 entity:u64 kind:u8`, with the kind 1 | Handler 0x6895A90 (invoke 0x65AC7C0): it shows a loading screen and sets the mark "teleport pending" of the player facet | Behavioral: a loading screen with a tip text for about 4 s; 1 call of 0x6895A90 for each such travel |
| 2 | A state bundle with one record of the own character (interest 1) with the full movement state in the owner view: `rotation` (11), `lookDir` (12), `worldPosAbs` (27, the new place), `teleportAndMigrationId` (31, the next number), `timeAnchor` (40), `dataBits` (41), `moreDataBits` (42). No metadata state (4.2), and no id of a state (`idRel`, `idAbs`) | Apply 0x485A550, compare 0x486D660, copy 0x486EA80 | Behavioral: 1 call of 0x486EA80 for each travel (and 1 at the world entry); the position of the character in the client memory is the place of the state |
| 3 | Message `PlayerComponentClientFacet_OnPlayerTeleport` (type 3954): `target:u64 entity:u64` | Handler 0x6888870 (invoke 0x6564020): it clears the mark and gives the position of the player to the world streaming | Behavioral: 2 calls of 0x6888870 for each travel, one from the message (return address 0x656405F) and one from the movement facet itself at the new teleport number (return address 0x4868625, the event dispatcher 0x4868420) |

- The facet target of the two messages is the entity group id of the character plus the id
  of the component `Player` of the root entity of the player slice (`DF0CE8C98D27619D` in the
  supported build; the rule of 4.2). Only the session of the owner gets them: the handler of
  3951 sets its mark with no check.
- The teleport number is a value of the entity (`Entity::teleport`): 1 at the creation of
  the entity, and a new number for a travel (`number % 15 + 1`: 1 to 15, never 0, the start
  number of a client). The count starts at the number that the client of the owner has: the
  number of the last full movement state that its session sent (`SessionSink::client_teleport`,
  0 before the first one). A session sends only the last movement state of a tick. With a
  count from the number of the entity, 15 travels of one character in one tick gave the
  state the number that the client had, and the client did not take the new position
  (review of stage 3.4c, finding C3; case TV7 with made-up clients, no live run). Thus each
  travel before the next movement state of the owner has the same number. An entity with
  no session counts from its own number. A session that takes the entity later gets the
  number that the entity has. The high 4 bits (the migration number of the client) stay 0.
- The position of the state is the place of the travel plus the spawn lift (0.5 m, 4.6): the
  place is a place on the ground, and the client lets the character fall to the ground.
  Live: at each of the travels of the runs the character stood on the ground, and the first
  accepted position report was 0.02 m to 0.04 m below the height of the place.
- The rotation of the state is the identity: the character and the camera look north after
  each travel (live: the yaw of the camera was 1.566 before and 0.000 after).
- Live results of run `v1` (one session, three travels; `W5-trace-and-log-lines.txt`): a
  travel of 14 m with no loading screen (the character was at the new place in the next
  picture, 2 s later, and a walk of 1 s moved it 4.9 m); a travel of 1,694 m to a spirit
  shrine of a different territory with the loading screen (the first position report of the
  new place came 4.7 s after the command); and a travel of 1,691 m back. The session had no
  new login, and the trace point 0x6769980 (player registration) had no new call.
- Not checked: a travel with no message 3954 (the movement facet calls the same handler
  itself, thus the message is probably not necessary; the backend sends it), the kind 0 of
  message 3951, a travel of more than 100 m with no loading screen, and what the client of
  an observer shows for a movement state of a different player with a new teleport number
  (there is no second client).

**The world model** (project rules; `World::travel`, `world/world.h`).

- The command names a character (rule MP-9). For a character that is in the world, the world
  writes the position row first. Then it sets the position of its entity (the position
  index, the revision), gives it its teleport number, and marks its movement state (rule
  MP-2: each observer gets the state through the usual path). The session of the owner gets
  `SessionSink::traveled`: it sends message 3951 when the place is far, starts the check of
  the position reports again from the new place (4.7), and sends message 3954 after the
  bundle with the movement state.
- A travel whose row the database did not accept changes nothing (review of stage 3.4c,
  finding C5; case TV8): the entity keeps its place and its teleport number, the session
  gets nothing, and the command gives the reply 500 (`TravelError::storage`). Before this
  rule the world moved the entity, the command gave the reply 200, and the row kept the old
  place. A position of an entity whose write failed at a different time (the save interval,
  a change of the owner) keeps its mark: the world tries the write again at the next
  interval.
- A session that observes the character compares its interest distances in its next tick,
  not at its interval of 500 ms (`World::last_travel`): it sends the stop of the entity when
  the new place is outside its leave distance, and the record with the new position when it
  is inside. It never sends a record with a place that is outside. A session near the new
  place gets a first record of the character. The same scan removes the creatures of the old
  place from the interest table of the owner.
- The creatures of the new place wait for the client of the owner (user test 1): they come
  into the interest table when the client confirms the place (an accepted position report,
  or a camera near the place; the same condition as `RepSession::camera_before_travel`), in
  the scan of that moment, and at the latest 20 s after the travel
  (`kCreaturesAfterTravel`). The loading screen of the client hides the start of the first
  records: the spawn action of a slice that needs it (4.14) then played while the client
  loaded, and the body of the creature stayed hidden (the Reaver of the Tomb of the Old
  Warrior after a far travel). Live: after a travel of 309 m with the loading screen the
  Reaver came into the table 2.5 s after the travel, with its body and its name plate. The
  20 s also apply to a session whose client sent no camera report yet (finding F4 of the
  review of the corrections of user test 1, case RU5: before, each interest scan needed a
  first camera report, and no creature came into the table of such a session).
- A travel ends each lock of the input of the client (4.20): the session of the owner sends
  the messages 3746 and 6709 before the messages of the travel (live proof: a character that
  a conversation kept in place walks after a travel; before, the user stayed in place after
  three travels).
- The spawn point rule (4.14) runs in the tick of the travel: the creatures of the new place
  are in the world before a session looks for them, and the creatures of the old place leave
  the world when no other player is near them.
- A place for a character is a position of the world with a height from -99 m to 999 m (the
  height range of the wire is -100 m to 1000 m, and the position check refuses its two
  limit values). The command refuses a different place, and nothing changes.
- A character that is not in the world has no entity to move: the command writes the place
  into its position row, and its next world entry is there (the reply has
  `"applied":"stored"`).

**The travel places** (game data, `tools/gamedata/places.sh`, file `travel-places.json`). Each
region of the game copy has a list of territory landmarks (`TerritoryLandmarks` of
`regions/REGION/region.metadata`): a type, a world position, and the ids of the territory, the
area, and the point of interest that contain it. The tool takes the landmarks of two types:
27, a spirit shrine (the place where the game puts a character after a fast travel), and 20,
the respawn place of an inn (the place after a recall). These are the places of the game data
where the game itself puts a character: each one is a place where a character can stand, with
a height on the ground. A landmark of the type 1 (a settlement) has a height that is not a
ground height (round numbers such as `4810.0 6829.0 146.0`). The name comes from the
territory tables (table type `TerritoryDefinition`: the name key of each territory, area, and
point of interest) and the English text files. The supported build gives 161 places in 16
territories: 133 spirit shrines and 28 inns. The id of a place is its kind and the id of its
point of interest (`shrine_40170`, `inn_40300`). Without the file the backend starts, and the
travel command takes only a position in numbers. The tool writes no file when a landmark of
one of the two types has a position with a wrong form (no position, a position that does not
have 12 bytes, a number that is not finite): a file with the other places looks complete
(review of stage 3.4c, finding C6; `tools/gamedata/check_places_tool.py`, case TV9). Each of
the 161 landmarks of the supported build has a correct position.

Live results (run `w6`, one session with 8 travels, 8,514 position reports accepted and none
refused, 1 report from the place before a travel not used):

| Place | Territory | Distance of the travel | What the client showed |
| --- | --- | --- | --- |
| Shrine of Pillars (`shrine_20430`) | Brimstone Sands | 6,514 m | The character on the floor of the shrine, the compass text "SHRINE OF PILLARS", the interaction text "Fast Travel" of the shrine, a Highlands Goat (level 62) 8 m away with its name plate; 51 creatures in 150 m |
| Empyrean Shrine (`shrine_40353`) | Great Cleave | 4,523 m | The character on snow beside the shrine, the compass text "EMPYREAN SHRINE", two Forge Raiders (level 60) with their name plates; 47 creatures in 150 m |
| Werna Isle Shrine (`shrine_30154`) | Monarch's Bluffs | 5,973 m | The character on the floor of the shrine, the compass text "WERNA ISLE SHRINE", a Siltboot Shaman (level 16) with its name plate; 31 creatures in 150 m |
| Polder Shrine (`shrine_50255`), with the button of the tool window | Weaver's Fen | 3,687 m | The loading screen, then the character at the shrine, the compass text "POLDER SHRINE"; 40 creatures in 150 m |

- The first position report from the new place came 3 s to 5 s after a travel with the
  loading screen. The height of a travel place was correct at each of the 6 places of the
  runs: the character stood on the ground or on the floor of the shrine.
- The client showed the game time of its own clock: the places were dark (before stage 2.2 the
  backend sent no world time; 4.43).
- A travel with numbers to a height that is 0.4 m below the ground worked (the spawn lift
  is 0.5 m). A height that is farther below the ground was not tested.

**The commands.** `tools/world places [TEXT]`, `tools/world travel CHARACTER PLACE`, and
`tools/world travel CHARACTER X Y Z` (routes `GET /v1/dev/world/places` and `POST
/v1/dev/world/travel`, with the argument `place`, or `x`, `y`, and `z`, and when necessary
`screen=auto|on|off` for the loading screen); the admin command `travel` with the same
arguments and the route `GET /admin/v1/catalog/places` (6.4); and the tab "Travel" of the
tool window. The two route sets use one function of the common layer
(`server/http/travel_commands.cpp`).

**Not known.** In the live runs of stage 3.4e two travels with numbers had no effect in the
client: the backend moved the entity and sent the movement state (teleport numbers 10 and
13), and the client kept its character at the old place; a second travel moved it. The two
came after a `heal` of a dead character and after a long walk with many `heal` commands. The
cause is not known.

### 4.17 The behavior of a creature: sense, way, attack, way back, patrol and wander (project rules; behavioral proof for what the client shows)

Stage 3.4b. Research: `~/Games/soulwarden/work/research/creatures.md` and `damage.md`. Evidence
of the live runs: `~/Games/soulwarden/work/evidence/fight/implement/` (`F*`, `L6-*`). Code:
`world/behavior.h` (the rules and their constants), `world/creatures.h`, `world/navigation.h`,
`gamedata/creature_data.h`, `gamedata/height_maps.h`; tools `tools/gamedata/creature_data.py`,
`creature_behavior.py`, and `height_maps.py`. Stage 3.4e adds the patrol, the wander, the
anchor of a fight, the reset (immune, with the trail), and the wait at the place nearest to a
target with no way (result file `~/Games/soulwarden/work/evidence/patrol/implement/RESULT.md`).

**What is a contract of the client, and what is a rule of this project.** The behavior of a
creature ran on the server of the game. The files of the game copy have the values of a
creature (its vision, its leash, its speeds, its attacks: the table below), and the research
found no file with the rules of the behavior. Thus each rule of this section is a project
rule: simple, and the same for each creature family. The contract of the client is only what
it shows for a state: the walk of a creature whose position changes (4.14), and the action of
a scope record with its tags (below).

**The behavior data** (game data, `tools/gamedata/creatures.sh`, file `creatures.json` in
format 2). The tool reads these values for each slice, each kind, each level, and each spawn
point:

| Value | Source in the game files | Member of the file |
| --- | --- | --- |
| The vision of a creature at rest: an angle and a distance | The Perception component of the slice: `m_ambientVisionProfile` with `m_visionAngle` and `m_visionDistance` | Slice `behavior.sight`: `[angle, distance]` |
| The vision in a fight | `m_combatVisionProfile` of the same component | `behavior.combatSight` |
| The leash at rest and in a fight (the largest distance from the home) | The Blackboard component: the entries (`m_name`, `m_value`) `AmbientLeashDistance` and `CombatLeashDistance` | `behavior.leash`: `[rest, fight]` |
| The speed of a walk and of a run in a fight | The Blackboard entries `CombatWalkMaxSpeed` and `CombatRunMaxSpeed`; the Pathing component (`m_navConfigType`, `m_maxSpeed`) when the blackboard has none | `behavior.speed`: `[walk, run]` |
| The tag state of the animation fragments | The weapons of the slice (`m_slotType`, `m_itemName`: slot 24 is the main hand, slot 27 the off hand), the weapon table (`MannequinTag`, `OffHandMannequinTag`), and the tag definition file of the animation controller | Slice `tags`: 12 bytes as 24 hexadecimal digits |
| The attacks | The animation database of the slice (`m_animationDatabase`, an `.adb` XML file) and of each weapon (`AnimDbPath` of the weapon table): a fragment with a procedural event `CAGE-Damage`; the damage table of the slice (`m_damageTable`) | Slice `attacks`: a list of `[action index, time of the action, contacts]` |
| The damage of a kind as a part of the base damage of its level | `DamageMod` of the base row of the vitals row times `CategoryDamageMod` of its creature type | Kind `damage` |
| The base damage and the gear score of a level | `BaseDamage` and `GearScore` of the level table (`VitalsLevelData`) | `levels`: `[level, health, damage, gear score]` |
| The terrain height at a spawn point | The height map of the region (below) | Spawn point, value 10 |
| The speed with no target (a patrol, a wander; stage 3.4e, format 3) | The Blackboard entry `AmbientNavMaxSpeed`; the Pathing configuration `Wander` or `AmbientLeash` when the blackboard has none | `behavior.ambient` |
| The wander (stage 3.4e) | The Blackboard entries `Enable_Wander` (a flag), `WanderDistanceMin`, `WanderDistanceMax` | `behavior.wander`: `[on, nearest, farthest]` |
| The patrol route of a spawn point (stage 3.4e) | The spawner slice: `m_patrolPathEntity`, `AIPatrol`, `AIWaypoint` (4.14) | `routes`, and spawn point value 11 |

- One contact of an attack is `[time, reach, damage factor, added damage, damage type, number
  of the type on the wire, category, name of the damage row]`. The time is the sum of the
  times `ExitTime` of the `Blend` elements before the damage event in its fragment. The
  damage factor and the added damage are `DmgCoef` and `AddDmg` of the damage row, and the
  type is `DamageType` of the row (the number `IntID` and the category of the damage type
  table).
- **The reach of a contact is a project value.** The game has the shape of a contact (a box,
  a capsule, a sphere) at a joint of the animated body, and the tool does not animate a body.
  For a shape at the root of the body, the reach is the far end of the shape (its offset
  plus its half length, times its scale) plus 0.5 m, not less than 1.5 m and not more than
  6 m. For a shape at a different joint (an arm, a weapon), the reach is 2.2 m.
- **Which actions are an attack** (project rule). The action has a fragment, and the variant
  of the fragment that the creature plays has one damage event or more. Each event names a
  row of the damage table of the slice that is not a ranged row, with a damage type of the
  damage type table and a damage factor of 0.05 or more. No word of the name of the action
  is `Combo`, `Breakout`, `Reaction`, `Structure`, `Run`, `Running`, `Spray`, `Throw`, or
  `Summon` (a part of a chain, a reaction, an attack on a structure, an attack that needs a
  run or a special body).
- **Which variant of a fragment a creature plays** (the rule of the animation system, as the
  tool uses it): the variant with the most tags whose tags are all tags of the creature. The
  variants of the off hand have the tag `Offhand` too.
- Counts of the supported build (the tool needs 51 s with an Oodle library, 33,783 pak
  entries; the file has 9.4 MB): 501 of the 503 slices have behavior values, 414 have one attack or more, and 271
  have a tag. 406 slices fight (a vision distance, a run speed, and an attack): 2,989 of the
  3,848 kinds, with 24,228 of the 32,840 spawn points. **859 kinds do not attack** (8,612
  spawn points): the tool writes one line for each of them with `--no-attack FILE`. The
  causes: the damage row of each of its damage events is not in its damage table (372
  kinds), no damage event in its fragments (209), no damage table (148), no run speed (85),
  and only attacks that the rule above leaves out (45). A creature of such a kind stands at
  its spawn point and takes damage, as in stage 3.4a.
- The loader refuses a file whose behavior data do not fit together, as a whole (the world
  then has no creature): an attack whose action the action table of the slice does not have
  with a fragment, an attack with no contact, a contact after the end of its attack, a
  reach, a speed, or a distance that is not a number of its range, a tag text that is not 24
  hexadecimal digits, and a damage type with no number or no category.
- A file of format 1 (stage 3.4c) has none of these values. The backend reads it as before:
  each creature stands at its spawn point.

**The tag state of the animation fragments** (static proof 0x6D87130, and behavioral proof
for one slice). A creature with a weapon has a variant of each attack fragment for each
weapon, and it has no variant with no tag. The field `globalFragTags` of a movement state
(12 bytes, 4.14) selects the variant.

- The bit of a tag: each byte of the 12 bytes gets, in the sequence of the tag definition
  file (after the tags of the files of its element `Imports`), each group that has no place
  yet and that fits in the bits that the byte has left, and then one bit for each tag with
  no group that has no place yet. A group with N tags needs the number of bits of the number
  N: the value 0 is "no tag of the group", and the tag K of the group has the value K, from
  1. The first bit of a byte is its lowest bit.
- Live proof (run `f2`, the development command `tools/world creature action ... --tags HEX`):
  the attack `Attack_DualSickle_DoubleSlash_R` of an Ancient Guardian Reaver does not play
  with 12 tag bytes of 0 (the Reaver stands, `L6-reaver-attack-does-not-play-with-no-tags`),
  and it plays with the bytes `24 04 00 ...` (the tags `1H_Melee`, `Sickle`, and `Sickle_OH`;
  `L6-reaver-attack-plays-with-weapon-tags`). A Grey Wolf has no tag (12 bytes of 0).
- Each scope record of a creature has the tags of its slice from stage 3.4b: the death
  action, the reaction, the spawn action, and each attack.

**The terrain heights** (game data, `tools/gamedata/heights.sh`, folder `heights` of the game
data folder). The client shows a creature at the exact position of its states and looks for
no floor (4.14): a creature that walks needs the height of the ground at each step.

- Source: `region.heightmap` of each region of the world, a TIFF file of 2048 by 2048
  samples of 16 bits (LZW data in strips, with the horizontal predictor). The sample of the
  local position (x, y) of a region is in row `2047 - y` and column `x`. The height in
  meters is the sample times 2048 / 65535. The region `r_+XX_+YY` starts at x = XX times
  2048 and y = YY times 2048.
- The tool writes one file for each region: `r_+XX_+YY.swh` (49 regions, 136 MB, 107 s; the
  pak entries are stored data, and the tool needs no Oodle library). The form of a file, all
  numbers little-endian: a header (`"SWHT"`, version 1, the region, the size 2048, the tile
  size 64, the height of the sample 65535), an index of 1,024 entries (offset and length of
  each tile, from the south to the north and in each row from the west to the east), and
  the tiles. One tile is one zlib stream of 65 by 65 samples: the last row and the last
  column are the first samples of the next tile. A sample is stored as its difference to
  the sample to its west.
- The backend opens a region file only when a position in it is asked, and it reads only
  the tile of 64 m by 64 m of that position. It keeps the 512 tiles that it used last
  (`gamedata::HeightMaps`). The height between four samples is the linear value. Without
  the files the backend starts, and no creature leaves its place.
- The heights of the tool are equal to the terrain heights of the research at six places
  (`creatures.md`: the terrain at spawn points of the start area, for example 136.54 at
  `8816.58 4288.17` and 140.11 at `8815.25 4319.13`).
- From stage 3.4d the samples of these files are the terrain after the cookies of the
  placements (4.19): the client changes the height map near some placements, and the tool
  takes the changed samples of the navigation tool when they are there
  (`SOULWARDEN_NAVIGATION_WORK/terrain/REGION.u16`; `tools/gamedata/navigation.sh` makes the
  folder `heights` again at its end). The reader for a spawn point (`Terrain.height`) and
  the files give one height for each place: in stage 3.4b the reader took a wrong sample in
  the last half meter at the east edge of a region with no region to its east
  (`tools/gamedata/check_heights_tool.py`, test case RF10).
- **The terrain is not each floor.** A floor of a building, a bridge, a rock, and a cave
  are not in the height maps. Thus the project rule: a creature walks only when its spawn
  point is 1.0 m or less from the terrain height (`gamedata::kTerrainTolerance`). A
  creature whose spawn point is farther from the terrain does not leave its place: it
  turns to its target, and it attacks a target that is in the reach of its attack. Counts
  of the file of stage 3.4b: 21,630 of the 32,840 spawn points are 0.3 m or less from the
  terrain, and 24,419 are 1.0 m or less from it. Of the 24,228 spawn points of kinds that
  fight, 6,893 are more than 1.0 m from the terrain: their creatures do not leave their
  place. With the terrain after the cookies (stage 3.4d) the counts are 22,312 and 24,797.
  From stage 3.4d this rule is the rule only at a place with no navigation data: with the
  data a creature walks when it stands on a free floor (4.19), and 1,207 spawn points of
  kinds that fight do not walk.
- A creature of a command (`tools/world creature spawn`) near a character that stands on
  the terrain (1.0 m or less) stands on the terrain at its own place: the ground is not
  level. Near a character that is not on the terrain (a floor), it gets the height of the
  character, as before, and it does not walk. With navigation data (stage 3.4d) the ground
  of the command is the floor of the data: a creature near a character on a floor gets the
  floor at its own place that is nearest to the height of the character, and it walks.

**The two functions of the navigation** (`world/navigation.h`; the place of the path search
of stage 3.4d). The behavior asks only these two functions for a place:

- `ground_height(position)`: the height of the ground at a position. The position has a
  height, because one place can have more than one floor: the result is the floor that is
  nearest to that height. In stage 3.4b the terrain is the only floor.
- `next_step(from, goal, distance)`: the place of a creature after one step to a goal, on
  the ground. In stage 3.4b the way is the straight line: a creature goes through a wall
  and through a rock.

**The rules of the behavior** (project rules; the constants are in the namespace
`world::behavior_rules`). The behavior decides and moves each creature one time each 200 ms
of the world clock. A creature has three modes: at rest (it can roam, stage 3.4e), in a
fight, and on its way back (the reset).

| Rule | Text |
| --- | --- |
| Sense | A creature at rest gets a character as its target when the character is in its vision: not farther than the vision distance of its slice, in the angle of its slice **to each side** of its direction (an angle of 180 degrees or more is each direction), and not more than 12 m higher or lower. No wall stops the vision (stage 3.4d). |
| Target by a hit | A creature gets the character that hurts it as its target, for each distance and direction. A creature has one target: a hit of a different character does not change it while its target is a character that it can fight. |
| Which characters | A character that is in the world, lives, and is not ignored by the creatures (the command below). A creature of a different kind is not a target. |
| Way | A creature with a target turns to it and goes to it with the run speed of its slice, in steps of 200 ms, until the target is 0.6 m inside the reach of its next attack (not nearer than 1.0 m). Each step is a place of `next_step`: the height is the height of the ground. |
| Attack | In that distance, and with the target not more than 3 m higher or lower, the creature starts the next attack of its slice (the attacks in the sequence of the file, again and again). The attack is an action of the server (a scope record): the creature keeps its place and its direction until the end of the action, then waits 0.9 s. |
| Contact | The world tests each contact of an attack at its time on the world clock. The contact hurts the target when the target is in the world, lives, is not ignored, is not farther than the reach of the contact plus 0.3 m, is not more than 3 m higher or lower, and is in an angle of 70 degrees to each side of the direction of the creature. One contact that passes is one damage result (4.18). A contact of an action that a different action replaced does nothing. |
| Way back | A creature loses its target when the target is dead, left the world, is ignored, is farther than 60 m (a travel), or when the creature is farther from its anchor than the leash of its slice for a fight (40 m for a slice with no leash value). The anchor is the place where the creature took its target (stage 3.4e; before, the home). It then goes back to its anchor with its run speed (the reset). On the way it is immune: it takes no damage and no target by a hit or by its vision, and its vitals state has the immune mark (4.5). With no path back it walks the places of its way out back (the trail). At its anchor it has its full health; at its home (the spawn point) also the exact place and direction of its spawn point. |
| No way | A creature whose target has no way to it goes to the place that it can get to that is nearest to the target, and waits there with its target for 12 s: it turns to the target and attacks it when it is in the reach. Each attack starts the 12 s again. After the wait it loses its target (the way back), and its vision does not take that character for 8 s. |
| Roam | A creature at rest walks its patrol route, or wanders near its home, only while a character is near it (below). |
| A creature that does not walk | It turns to its target (a new movement state for each 8 degrees), attacks a target that is in the reach of its next attack, and loses a target that is farther than its vision distance of a fight (30 m for a slice with no value). It is at its anchor at once. A creature that does not walk does not roam. |
| Health | A creature keeps its health in a fight and on the way back. It has its full health when it is at its anchor (no regeneration in a fight: stage 3.5a). |
| Hit of a player | The rules of 4.15 stay: the health goes down, and the creature plays the action `Reaction`. A creature that plays an attack plays no reaction: the reaction would take the attack away, and a player that hits fast would stop each attack. A dead creature has no target, and the attack that it played does no damage. |

- The home of a creature is its spawn point, or the place of its spawn command. Its anchor is
  the place where it took its target: for a creature at rest at its spawn point the two are
  one place, and for a creature on its patrol the anchor is a place of its route.
- **The spawn point rule** (4.14) uses the distance of the spawn point, for the two
  distances. A creature that goes to a player and away from its point stays in the world
  while a player is 200 m or less from the point. The leash of each slice is less than the
  difference of the two distances (the largest leash of the file is 70 m).
- **The position index and the interest tables follow a creature.** A step changes the
  position of the entity through the creature system (`Creatures::move`), which puts the
  entity at its new place in the position index and marks its movement state. A session
  whose character is near the new place gets the creature in its interest table at the next
  scan, and a session that is too far loses it (the rule of 4.14, with the position of the
  creature).
- **The log.** One line for each new target (`world: creature ID (KIND) has a target: NAME
  (CAUSE, N m)`), for each lost target with its cause, for each attack, for each hit with
  the values of the damage rule, for each contact that did not hurt its target, and for
  each creature that is at its home again. From stage 3.4e: `has no way to its target NAME:
  it goes to the place nearest to it and waits there with it`, `lost its target (CAUSE): it
  goes back to its anchor, N m, immune`, `has no path back to its anchor: it walks the N
  places of its way back`, `has no way to its anchor and no session observes it: it is at
  its anchor in one step`, `is at its home (its anchor)` or `is at its anchor`.

**The roam and the reset** (stage 3.4e; project rules, each constant in `behavior_rules`,
`world/behavior.h`; the values of a slice and of a route are game data, 4.14).

| Rule | Text |
| --- | --- |
| Who roams | A creature at rest that walks (a free floor of the navigation data), that is not dead, and that has a patrol route (its spawn point) or a slice that wanders. It roams only while a character is not farther than the leave distance of its interest (`World::interest_leave`: the radius of its slice from 20 m to 100 m, plus 20 m; 120 m at most): the distance at which a session of that character can observe it. The world selects these creatures one time each 1 s of the world clock, from the characters with the position index (`kRoamScanInterval`). A different creature stands where it is: it needs no time. |
| Patrol | The creature walks to the next waypoint of its route with the ambient speed of its slice (1.5 m/s for a slice with no value, `kDefaultAmbientSpeed`) and the walk action (`Move`, 4.14), on the path of `next_step`. At the waypoint (0.3 m or less, `kRoamArrival`) it turns to the direction of the waypoint when the waypoint has `m_matchAlignment`, waits for a random time of the range of the waypoint (the generator of the world), and plays the task of the waypoint for that time when the action table of its slice has an action with that exact name (the letter case is not ignored). A negative wait (-1 in the game files) is no wait, with two exceptions: at the waypoint of a route of one waypoint, and at a waypoint whose task ends with `_LOOP` (the letter case is ignored), the creature stays for ever (it then needs no time, and a task with that name plays with no end). A fight replaces that task: after the reset the creature goes to the waypoint again and plays its task again (finding F8 of the review of stage 3.4e, case RP8; before, it stood with no task for ever). The waypoint is reached only on its floor (the row "No path back"). |
| Policy | A loop: after the last waypoint the first one. Back and forth: the waypoints in their sequence, then back. At random: a random waypoint that is not the waypoint where the creature is. A route with a policy that is not defined is a loop. |
| Wander | A creature with no route whose slice wanders goes to a random place at a random distance of its wander range (`WanderDistanceMin` to `WanderDistanceMax`, not farther than the leash of its slice at rest) from its home, in a random direction, with the ambient speed and the walk action. It tries 6 places in one step (`kWanderTries`): a place with no floor, or with a floor that is not free, is not taken. At the place it waits 4 s to 12 s (`kWanderWaitShortest`, `kWanderWaitLongest`). |
| No way on a roam | A waypoint or a place with no way to it: the creature waits 3 s (`kRoamNoWayPause`) and takes the next goal. |
| The first walk | Each creature starts its first walk 0 s to 4 s after its creation (a random time): the creatures of one place do not start at one time. |
| The random numbers | The world has one generator (splitmix64, `World::random_unit`); `WorldOptions::randomSeed` gives its start value (the backend takes it from the clock at its start; a test sets it, and one value gives one sequence). |
| Anchor | When a creature takes a target, the place where it is is its anchor, and the first place of its trail. The leash is the distance from the anchor. At the end of the way back the patrol or the wander goes on from the anchor: a patrol goes to its next waypoint. |
| Immune | On the way back (the mode `home`) the creature takes no damage (`Creatures::hurt` gives the error `immune`; the hit check of 4.15 refuses the claim with the cause "the creature is on its way back (immune)") and no target by a hit or by its vision. The health command and the kill command do not lower its health either (the reply 400 "the creature is on its way back: it takes no damage"; finding F7 of the review of stage 3.4e, case RP7): a creature on its way back does not die, and a kill and its rewards (4.21) come only from a fight. The vitals state has `invulnerability` 1 and `displayImmuneWhenInvulnerable` 1 for each observer (4.5), and 0 again at the anchor. |
| Trail | In a fight the creature keeps the places of its way: one place for each 1 m (`kTrailSpacing`), 512 places at most (`kTrailLength`; the anchor stays the first one). A way that comes back to an older place of the trail removes the places after it (the trail has no loop). |
| No path back | A creature is at its anchor, at a place of its trail, and at a waypoint when it is near the place in the plane and on its floor: the two heights are 1.0 m or less apart (`kFloorTolerance`; two floors of one cell are farther apart). A place on a different floor is no arrival: the navigation finds the way to it or says that it has none (findings F1 and F2 of the review of stage 3.4e, cases RP1 and RP2: before, a creature below its anchor jumped through the ceiling while a session observed it, and a creature below a waypoint of a floor with no way played the task of the waypoint). When the way back to the anchor has no path (`noWay` or `noGround`), the creature walks the places of its trail back, each one with `next_step` (it does not jump). A place of the trail with no way to it is left out. When the anchor itself has no way, the creature is at its anchor in one step only when no session can observe it (no character of a client is in the leave distance of its interest); if not, it stands immune at the last place and tries again at each step. |
| No way to the target | `next_step` with the goal "the nearest place" (4.19) gives the path to the place that the creature can get to that is nearest to the target. The creature waits there with its target for 12 s (`kNoWayHold`); each attack starts the time again. The 12 s start when the creature is at that place (the end of its path, or no step nearer), not at the first step of the path to it (finding F4 of the review of stage 3.4e, case RP4: before, a way of more than 12 s ended before the place). Then it loses its target with the cause `no way to the target, after its wait at the place nearest to it`. |

- The commands: `tools/world creatures` prints `PATROL (waypoint N of M)`, `, stays there)`
  (a creature that stays for ever), `, stands: CAUSE)` (no character near, no navigation data
  of its place yet, no free floor at its place, no behavior data of its slice), `WANDER`,
  `GOES BACK (immune)`, `ON ITS TRAIL`, and `WAITS (no way to its target)`. `tools/world
  list` prints the line `roam` (the creatures that roam now, the steps, the waypoints, the
  places of a wander, the goals with no way, the ways back, the waits, the ways on the trail,
  the jumps, and the refused hits on an immune creature). The development command
  `tools/world creature anchor CREATURE X Y Z` (route `POST
  /v1/dev/creatures/anchor?creature=ID&x=N&y=N&z=N`) moves the anchor of a creature in a
  fight to a different place: a test of a way back with no path (the place where it took its
  target stays the second place of its trail).

**What the backend sends, and what the client shows** (behavioral proof, runs `f4` to `f6`).

- **The walk.** A movement state with the new position and the new direction for each step
  (5 states in 1 s). The first state of a way has the locomotion action of the slice
  (`Move_Combat`, 4.14), and the other states of the way have no scope fields: the client
  selects the walk or the run from the speed of the creature. Correction of user test 1: the
  states of stage 3.4b had no action, and the creature slid in its pose (in the pose of its
  last attack, or with its spawn action again: the client keeps a scope record, 4.14). The
  pictures of stage 3.4b showed a pose of the run in some frames: the user saw the slide in
  the live game. Each observer gets the same states.
- **An attack** is a scope record in the movement state (4.14): the index of the action and
  of its fragment, the option 0, the tags of the slice, and the time from the start. The
  client plays it one time. A session gets the scope fields only when the action changed
  after its last record: the next way after the attack gets the locomotion record, and a
  state at the end of an action gets the fields with no record. A step of a way replaces a
  reaction to a hit that plays (a creature does not slide in its reaction).
- **The spawn action** of a slice that needs it (4.14) is a value of the creature
  (`Creature::spawnAction`) for its whole time in the world. The first movement state of
  each session for the creature has it, with the time 0 (user test 1; before, the time from
  the creation of the creature). An attack, a reaction, and a walk do not take it away, and
  the backend does not send it again at their end. Live: an Ancient Guardian Reaver of a
  command comes out of the ground, walks to the character, attacks, goes back, and stands at
  its home (`F12-reaver-of-a-command-...`): it does not go back into the ground. A dead
  creature with a death action has that action in the first state of a later session. A
  dead creature of a slice with no death action has its spawn action there (finding C4 of
  the review of stage 3.4c: before, a creature that died while its reaction played lost the
  spawn action at the end of the reaction).
- The client shows no health change and no reaction of the player character for an attack
  action alone: the damage is the health system of 4.18.
- The client moves the body of a creature forward in some attacks (the lunge of the wolf)
  and puts it back at the position of the server at the end of the action. The position of
  the backend does not change in an attack.
- **A roam** (stage 3.4e) is the same movement states: one for each step of 200 ms, the record
  of the walk action `Move` at the start of each walk (4.14), and the fields with no record at
  a waypoint (the client plays the idle action). A task of a waypoint is a scope record with
  an end. Each observer gets the same states (case PT10).
- **The reset** sends the vitals state with the immune mark (4.5) to each observer at the
  start of the way back and at the anchor, and the run (`Move_Combat`) for the way.

**Live results** (the character `Soulwarden`, level 26, with light chest and leg armor of
gear score 60, 2,186 health):

| Creature | Where | What the run showed |
| --- | --- | --- |
| Grey Wolf (`Wolf_Grey_7`, level 18) | Spawn point `8815.25 4319.13`, and creatures of a command at the start place | Target at 11.3 m (vision 12 m, 75 degrees to each side). It runs to the character, bites (119.3 damage), and lunges (417.5). With the ignore value on, it runs back and stands at its home. Its leash: it lost its target 70.1 m from its home. |
| Fallow Scavenger (a Risen, level 18) | Floor of a hut at `8873.61 4338.63` | It comes out of its hut and swipes (2,186 to 2,009). Its spawn point is 0.87 m above the terrain: it walks on the terrain, below its floor. |
| Canary Miner (`Damned_Villager_Pickaxe_15`, level 20) | Near the Shadowmine Shrine | Target at 10.0 m. Its attack `Attack_Kick` did 533.2 damage (2,186 to 1,653), then it started `Attack_1H_Ground_Pound`. |
| Ancient Guardian Reaver (level 17, a slice with a spawn action and with tags) | Spawn point `8572.83 4241.20 166.60` | The point is 1.42 m above the terrain (a floor of a ruin): the Reaver does not leave its place. With the character 2.0 m from it, it attacks with its two sickles (151.6 damage for each contact, 2 or 3 contacts for each attack) and stands at its place after the fight. |
| Ancient Guardian Reaver of a command | On the terrain at the start place | It comes out of the ground (its spawn action), gets its target at 9.1 m, walks 7.4 m, attacks (1,883, 1,580, 1,429 health), goes back, and stands. |
| Siltboot Deckhand (`Drowned_Sailor_2H_Axe_7`, level 15) | `6466.83 2880.21`, Monarch's Bluffs | Target at 9.6 m. It walks to the character and hits two times (530.7 and 216.2 damage; 2,186 to 1,440). |
| Chalky Rabbit (level 1) | Spawn point at the start place | It has no attack: it stands 7 m from the character, and the character keeps its health. A rabbit does not flee. |

Live results of stage 3.4e (runs `pc1`, `pt1`, `pt3`; videos in
`~/Games/soulwarden/work/evidence/patrol/implement/proof`):

| What | Where | What the run showed |
| --- | --- | --- |
| The patrol of the start wolf | Spawn point `8815.25 4319.13` | The wolf walks its loop of four waypoints with the walk action at 1.75 m/s, turns to the direction of each waypoint, and waits there (`P9-1`, 90 s, with its track). |
| A wander | A Risen of a command at the start place | It walks to a random place of its range, waits, and walks again (`P9-2`). |
| A fight from a patrol | The start wolf | It sees the character on its patrol, runs, attacks, follows the character to the end of its leash (measured from its anchor), turns, runs back immune, and patrols again (`P9-3`). |
| No path back | A Grey Wolf of a command at the Tomb of the Old Warrior, its anchor moved to a ledge with no way (`tools/world creature anchor`) | It walks the 19 places of its trail back to the place where it took its target, and it stands there immune while the session observes it. After a travel of 290 m (no session near) it is at its anchor in one step (`P9-4`, track and log). |
| No way to the target | The same tomb: the character on a raised stone (1.4 m above the floor) and on a ledge (6.7 m above it) | On the stone: the wolf goes to the nearest place (1.6 m from the character) and attacks from there; each attack starts the wait again. On the ledge: the wolf goes to the place below the ledge, waits 12.1 s, goes back immune 5.7 m to its anchor, and takes the character again 8.0 s later (`P9-5`, two videos, track, log). |
| A far territory | Mosswater Bourg, Everfall (`10300 3300` to `10500 3400`) | 64 creatures roam near the character: ghouls, revenants, porters, faminegeists, and alligators. An alligator walks its route of 10 waypoints (56 m) back and forth in the shallow water at 1.81 m/s and waits at its two ends (`P9-6`). |
| The immune mark | A Grey Wolf that stands immune at the end of its trail | 4 sword attacks at 1.5 m: no claim; a wolf with no mark at the same distance: 3 hits in 3 attacks (4.5). |

**The load** (criterion of stage 3.4c, kept). A creature at rest needs no time: the step
looks only at the creatures of one set (a target, or the way back), and the vision rule
runs from the characters with the position index of the world (the creatures in the
largest vision distance of the data around each character).

- Test case FT9 (a made-up world of 33,856 spawn points, each one with a creature that
  fights; the values are from one run): with 50 characters, 600 creatures in the world, and
  no creature with a character in its vision, 1,000 ticks of the world (10 s of the world
  clock) need 2.2 ms. With 50 fights at one time (50 creatures of 650 in the step), 1,000
  ticks need 3.6 ms, and the largest step of the behavior needs 54 µs. The case has a time
  limit of 1 s.
- Live (run `f6`, one fight of five Grey Wolves with one character): a step of the behavior
  needs 2.9 µs on average and 185 µs at most, with 5 creatures in the step; a tick of the
  world needs 3.1 µs on average. At the end of the run (10 minutes, 7 targets, 15 attacks):
  1,382 steps of 3.6 µs on average, the largest one 529 µs.
- `World::load` has the time of the steps, and `tools/world list` prints it in the line
  `behavior`, with the counts (targets, attacks, contacts, position changes).
- **The roam** (stage 3.4e). Only the creatures near a character roam (the set `m_roaming`, from
  the characters each 1 s). Test case PT9 (one run): 50 characters with 20 creatures of spawn
  points with a route and 10 creatures of commands that wander near each one (1,500 creatures
  that roam), 500 spawn points far from each character (no creature) and 20 creatures of
  commands far from each character (they stand): 1,000 ticks of the world (10 s of the world
  clock) need 76.5 ms, the largest tick 3.0 ms, a step of the behavior 1.37 ms on average
  (75,000 roam steps). The case has a time limit of 3 s.
- Live (run `pt1`, Mosswater Bourg, one session, 64 creatures that roam near the
  character, 142 creatures in the world): the state bundles of the creatures need 9.0 kB
  each second on average (7.1 kB to 9.9 kB in 68 s), with 247 records of creatures each
  second. A tick of the world needed 103 µs on average.

**The switches.**

- `SOULWARDEN_CREATURE_BEHAVIOR=0` (backend option `--no-creature-behavior`) turns the
  behavior off for a start: the creatures stand at their spawn points, as in stage 3.4c. The
  backend log and the tool window say so.
- The creatures ignore one character: `tools/world ignore CHARACTER on|off` (route `POST
  /v1/dev/world/ignore?character=ID&ignore=1|0`), the admin command `set_creatures_ignore`
  with the argument `ignore` (6.4), and the mark "Creatures ignore this character" of the
  tab "Character" of the tool window. The value is a value of the character in the memory
  of the world, with the character id as its key: it stays when the session of the
  character ends, until the backend stops. A creature that has the character as its target
  loses it at its next step.

**Not known, not done.**

- From stage 3.4d (4.19) a creature goes on a path of the navigation data, stands on its
  floor, and does not see through a wall. What this section says about the straight line,
  about the terrain height of a spawn point, and about the pause and the sequence of the
  attacks is the rule of stage 3.4b: it is the rule now only at a place with no navigation
  data. The rules of stage 3.4d for the shelter of a character, the health that comes back,
  the hits of an attack, light and heavy attacks, and the crowd are in 4.19.
- The reach of an attack is a project value, not the shape of the game. The attack list of
  a slice has each action that passes the rule: a slice can have an attack that the
  original behavior used only in a special condition (the kick and the ground pound of a
  miner). The sequence of the attacks is a project rule (4.19: the light attacks in the
  sequence of the file, and a heavy attack after each three of them).
- The client moves the body of a creature in some attacks (root motion), and the backend
  does not move the entity.
- The weapon of a creature does not show in its hand (the Siltboot Deckhand has an axe in
  the game): the backend sends no paperdoll state for a creature.
- No group aggression, no creature that calls others, no ranged attack, no magic, no ability
  of a creature, no swimming or flying, and no flee behavior (a rabbit stands).
- The tasks of the waypoints are names of the animation databases (24,895 waypoints have a
  task, and only 23 tasks are an action of the action table): the other tasks need the
  ambient actions of the animation database with their tags (a later stage). The creature
  stands in its idle action at such a waypoint.
- The ambient speed is the game value with no live proof from the original server: 18 slices
  have 0.07 m/s (the faminegeists of Mosswater Bourg move 3 m in 58 s).
- The client can still report one contact on an immune creature (a swing at a creature beside
  it); the backend refuses it. The client shows no immune text (4.5).
- The 859 kinds with no attack do not attack: they take no target. A kind with no attack
  that has the roam data (a patrol route or the wander values, and the behavior data of its
  slice) patrols or wanders as each other creature (finding F9 of the review of stage 3.4e,
  case RP9). 372 of them name a damage row that their damage table does not have: the row
  can be in a different table.
- A creature near a travel place or a respawn place attacks a character there 10 s after
  the character came (the shelter of 4.19; a Highlands Goat of level 62 is 8 m from the
  Shrine of Pillars).

### 4.18 Health and death of a character: damage of a creature, the death screen, the respawn (behavioral proof; the rules are project rules)

Stage 3.4b. Research: `~/Games/soulwarden/work/research/death-travel.md` (section 1) and
`damage.md` (static proof, checked by a second agent). Evidence of the live runs:
`~/Games/soulwarden/work/evidence/fight/implement/` (`L1-*` to `L5-*`: the live checks of the
research with a development command, before the code; `F3-F4-F5-*`, `F6-*`: the code). Code:
`world/health.h`, `world/combat.h` (`Combat::creature_damage`), `server/rep/health_messages.h`,
`server/rep/states/vitals_state.cpp`.

**Who finds a hit of a creature.** The client of a character finds no hit of a creature and
sends no report for it (4.15). The world tests each contact of an attack of a creature
(4.17), computes the damage, and changes the health of the character. The client computes
nothing.

**The damage rule** (project rule: the formula of the original server is not known; the
tables are game data). One function computes it (`Combat::creature_damage`):

```
base   = BaseDamage of the level of the creature x damage factor of its kind       (creatures.json)
raw    = base x DmgCoef of the damage row of the contact + AddDmg of the row       (creatures.json)
level  = 1 + DamageModifier of the level difference (creature level - character level)
armor  = the sum, for the armor items of the slots 0 to 4 of the character, of
         round1( ArmorRatingScaleFactor x round1( gear score ^ 1.2 x set scale factor ) )
part   = armor / ( armor + gear score of the level of the creature ^ 1.2 )
damage = raw x level x ( 1 - part )
```

Stage 1.3b (4.23): `armor` times (1 + the armor part of the perks and the effects of the
character for the category: a dust, the status of the heart rune), and `damage` times (1 - the
ABS<type> part of the character for the damage type) and (1 - BaseDamageReduction).

- `round1` is a value with one decimal. The set scale factor of an item is
  `PhysicalArmorSetScaleFactor` for a damage type of the category `Physical` and
  `ElementalArmorSetScaleFactor` for the category `Elemental` (table `ArmorItemDefinitions`,
  the row `ItemStatsRef` of the item). The two exponents 1.2 are `Armor Set Rating Exponent`
  and `Armor Mitigation Exponent` of the base attributes file. The armor of one item is the
  rule of the client for the value that it shows (static proof 0x5BFB400). A damage type of
  the category `True` has no armor.
- The level difference is the table `LevelDisparityData` (`LevelDisparity`,
  `DamageModifier`): 0.2 for 6 levels, 0.4 for 7, 0.6 for 8, 0.8 for 9, and 1.5 for 10 to
  20. A difference with no row gives 0: a creature of a lower level does its full damage.
- These values are in `combat-tables.json` (`tools/gamedata/combat.sh`; the members `armor`
  with the two exponents and 14,889 items, and `levelDamage` with 15 rows). With a file from
  before stage 3.4b, or with no file, the armor and the level difference have no effect:
  the damage is the raw damage.
- Not in the rule: the attributes of the character (constitution gives health only), a
  weakness or an absorption of the character, a block, a dodge, a critical hit, and a
  status effect.
- **Worked example** (test case FT5, and live in each run): a Grey Wolf of level 18 bites a
  character of level 26 with `LightChestT1` and `LightLegsT1` of gear score 60. `BaseDamage`
  of level 18 is 616.41, and the damage factor of the kind is 1: base 616.41. The row
  `Attack_01_Bite` has `DmgCoef` 0.2: raw 123.28. The level difference is -8: level 1. The
  armor: 60 ^ 1.2 = 136.08; chest `round1(136.08 x 0.25)` = 34.0 and `round1(0.3 x 34.0)` =
  10.2; legs `round1(0.2 x 34.0)` = 6.8; armor 17.0. The gear score of level 18 is 180, and
  180 ^ 1.2 = 508.54: part 17.0 / 525.54 = 0.0323. Damage 123.28 x 0.9677 = 119.29. The
  backend log of a live run: `119.3 damage of the type Slash (damage of the creature 616.4,
  of the attack 123.3, level factor 1.00, armor 17.0, mitigation 0.032)`.

**The health of a character on the wire.** The health is the field `HealthAmount` of the
vitals state of the character (slot 41, type 15; 4.5).

| What the backend sends | To whom | Client code | Evidence |
| --- | --- | --- | --- |
| The vitals state with the new `HealthAmount`. Each state after the first one of a session also has `vitalsData` (field 2: 18 bytes; bit 0 of its first byte is "dead") and `healthChangeFlags` (field 3) with bit 0 | The owner and each observer of the character, by the usual path (`World::touch`) | Apply function 0x67A6F10; reader of `vitalsData` 0x69515B0 | Behavioral: a fragment with only `HealthAmount` 1000 changed the HUD of the owner to 1,000 of 2,186 (`L1-*`); the bar is grey at the full health and red below it |
| Message `VitalsComponentClientFacet_OnDamage` (type 3601) | Only the session of the character that took the damage | Reader 0x658C360, handler 0x686C6A0 | Behavioral: a red mark with the direction of the source at the middle of the screen, a red edge of the picture, and the number of the damage in red near the character (`L5-*`, `F12-reaver-at-its-spawn-point-...`) |

- The body of message 3601: `target:u64 entity:u64 source:u64 flags:2 bytes part:f32 x:f32
  y:f32 z:f32 count:size`, then for each entry `damage type:u8 amount:f32 part:f32`. The
  target is the facet of the vitals component of the character: (entity `Stats`
  `0xE6448D4D53852704` XOR the root of the player slice `0xDAEE4E2555650AB7` XOR the entity
  group id of the character) plus the component id `0xEEEFFB82ABAFE4B1` (the rule of 4.2).
  `source` is the entity id of the creature. The flags `02 00`: bit 1 is "the damage has a
  place", and the three numbers are the place of the source. The first `part` is the damage
  as a part of the maximum health; the client shows the mark for 0.05, for 1, and for 200,
  and the backend sends a value from 0 to 1. The backend sends one entry: the number of the
  damage type on the wire, the damage, and the part 1.
- The message changes no value of the client: the health is in the vitals state. A vitals
  state with no message lowers the bar and shows no mark.
- The first vitals state of a session for a character that lives has no `vitalsData` and no
  `healthChangeFlags`, as before stage 3.4b (the stored bytes of test case T2 did not
  change). A session that starts to observe a dead character gets the two fields in its
  first state.
- From stage 3.3b-S6 the owner view of the state that carries a heal also has bit 1 of
  `healthChangeFlags`: the heal number of the owner (4.31). The observer view never has it.

**The death** (behavioral proof, runs `f1` and `f6`; the times are project rules).

| Step | What the backend sends to the session of the owner | Client code | Evidence |
| --- | --- | --- | --- |
| 1 | The vitals state with `HealthAmount` 0 and bit 0 of `vitalsData` | 0x67A6F10; getter of the bit 0x684AC70 | The health 0 alone is not a death: the client shows the death only with the bit |
| 2 | Message `PlayerComponentClientFacet_OnPlayerDeath` (type 3950): `target:u64 entity:u64 time:f32`. The target is the entity group id plus `0xDF0CE8C98D27619D` (the player component, as for the travel messages of 4.16) | Handler 0x68879F0 | With the time 0 the respawn screen shows at once (`L2-*`). With the time 8 the text "YOU DIED!" shows with a count of 8 s, then the respawn screen (`L3-*`). The backend sends 5 s (`Health::kRespawnTime`) |

- The client plays the death of its own character itself: the character falls and lies on
  the ground, and the picture goes dark (`F6-death-by-five-wolves-...`). The respawn screen
  has a map, the list "Respawn points" with the one row "Random Location", and the button
  "Respawn". The backend sends no list of respawn points (camps, inns, and houses are
  system 2.6).
- Each observer of the character gets the vitals state with the dead bit. What the client
  of an observer shows for a dead remote player was not tested live (one client).
- **A dead character** (project rules): it takes no damage; a hit claim of its client is
  refused (cause `the character is dead`); no creature has it as its target, and a creature
  that had it loses it and goes back to its anchor (4.17); a contact of an attack that plays
  does not hurt it. Its entity stays in the world at the place of its death.

**The respawn** (behavioral proof, runs `f1`, `f4`, and `f6`).

| Step | Message | Client code | Evidence |
| --- | --- | --- | --- |
| 1, client to server | `PlayerComponentServerFacet_RequestRespawn` (type 1938): `target:u64 entity:u64 random:u8 id:text`; 18 bytes with `random` 1 and an empty id for the row "Random Location" | Sender 0x69128E0; it shows the loading screen of the client itself (0x6895A90) before it sends | 2 requests in the captures of run `f1` |
| 2 | The vitals state with the full health and with bit 0 of `vitalsData` clear | 0x67A6F10 | The death screen closes when the bit is 0 again |
| 3 | The movement state of a travel: the new place with the next teleport number (4.16) | 0x486EA80 | The character is at the new place |
| 4 | Message `PlayerComponentClientFacet_OnPlayerTeleport` (type 3954) | Handler 0x6888870 | The loading screen ends 5 s to 8 s after the request, and the character lives with its full health (`L4-*`, `F6-respawn-button-...`) |

- The backend sends no message 3951 for a respawn: the client showed its loading screen
  already.
- **The respawn place** (project rule): the travel place of the game data that is nearest
  to the place of the death (the spirit shrines and the inns of `travel-places.json`,
  horizontal distance). With no travel places, the character lives again at the place of
  its death. The change of the place is a travel (`World::travel`, 4.16): the position
  check starts again at the new place, the row is written, and the creatures of the new
  place come by the spawn rule. When the world refuses the travel (a row that the database
  did not accept), the character lives again at the place of its death, and the session
  sends message 3954 itself.
- A respawn ends each lock of the input of the client (4.20): the messages 3746 and 6709 go
  before message 3954 (with the travel of the respawn, or before the 3954 that the session
  sends itself). Case UT1.
- The world finds the character of a respawn request from the session. A request that
  names the entity of a different character changes nothing and gets no reply. A request
  for a character that lives changes nothing, and the session sends message 3954: only
  that message ends the loading screen that the client showed for its request.
- **A dead character that leaves the world** (project rule): the world writes its respawn
  place into its row when it leaves (`World::leave`), and each world entry starts with the
  full health. The health and the death of a character are not in the database: a
  character is never dead after a login.
- **The commands.** `tools/world heal CHARACTER` (route `POST /v1/dev/world/heal`), the
  admin command `heal` (6.4), and the button "Heal" of the tab "Character" of the tool
  window (its text is "Revive" for a dead character) give a character its full health. A
  dead character then lives again at its place, with no travel: the death screen closes
  with the vitals state alone. For a character that is not in the world nothing changes.
  `tools/world hurt CHARACTER AMOUNT` (route `POST /v1/dev/world/hurt`, a development route
  only) lowers the health of a character, and `tools/world message CHARACTER TYPE HEX`
  (route `POST /v1/dev/world/message`) sends one message with your bytes to the session of
  a character: the live checks of this section used it before the code was there.

**Not known, not done.**

- What the client of an observer shows for a dead remote player, and for a remote player
  that lives again.
- The original time to the respawn screen, the original respawn places, and the loss of
  the original game at a death (the durability of the equipment) are not known. The row
  "Random Location" is the only row of the list.
- From stage 3.4d the health of a character that is not in a fight comes back (4.19: 3
  percent of the maximum each second, 6 s after the last damage). No food, no potion effect,
  and no combat state of the client: the health potion of the quick slot has no effect yet.
- The character has no reaction to a hit (a stagger): the backend starts no action for the
  character.
- Damage numbers of more than one contact at one time are on top of each other on the
  screen.
- The flags of message 3601 other than bit 1, and the fields of `vitalsData` after its
  first byte (the "death's door" condition of the game: the backend sends 0).

### 4.19 The navigation data: where a creature can stand, its path, and its sight (static proof for the game files; live proof for the terrain rule, the agent values, and what the client shows; the rules are project rules)

Stage 3.4d. Research: `~/Games/soulwarden/work/research/navigation.md`. The extraction of the
collision data: `~/Games/soulwarden/work/evidence/navigation-data/implement/RESULT.md`. The
live checks and the live proof of this stage:
`~/Games/soulwarden/work/evidence/path-search/implement/` (`checks/CHECKS.md` with the checks
C1 to C14, `proof/N12-*`, `data/`). Code: `world/navigation.h` (the rules of the path and
their constants), `gamedata/navigation_data.h` (the reader of the files), `world/behavior.h`;
tools `tools/gamedata/navigation.sh` (`navigation_data.py`, `collision_data.py`,
`nav_assets.py`, `nav_slices.py`, `nav_shapes.py`, `nav_terrain.py`, `navcore.cpp`,
`nav_file.py`) and `tools/research/nav_draw.py`.

**What the game copy has, and what is a rule of this project.** The navigation mesh of the
game was on its server: no file of the game copy has it (research). The client stops its own
character at each wall: thus the game copy has the collision shapes of the world, and it has
the marks that the level designers made for the navigation mesh of the server. The tool makes
the navigation data of the backend from these. The formats of the game files below are
contracts of the supported build. The size of a creature, the cell of the data, the path
search, and the sight rule are project rules.

**The sources of one region.** A region is a square of 2048 m (`r_+XX_+YY` starts at
x = XX * 2048, y = YY * 2048; 49 regions). The files are below
`sharedassets/coatlicue/newworld_vitaeeterna/regions/REGION/` in the pak files.

| Source | What the tool takes | World |
| --- | --- | ---: |
| `region.heightmap` | the terrain: 2048 by 2048 samples | 49 files |
| `capitals/L/L.capitals.json` | the placements of a layer: the cookies of the terrain (below), and each slice that a placement names (a placement with the slice name `<PLOT>` is in the chunks already) | 85,430 placements with a slice |
| `region.waterqt` | the water surface and the floor below it | 99.8 km2 of water |
| `region.chunks` and the chunk slices | the colliders of the static objects (a chunk slice has the objects of one square of a layer; `chunkType` 2 is a collision chunk) | 1,167,268 collider placements |
| `region.distribution` | the trees and the stones: a slice with a variant for each row | 2,728,908 rows |
| `region.slicedata` | the meshes of a slice with a variant | 71,887 entries |

`region.vegetation` has only the grass, the ferns, and the brambles (172.8 million instances,
no physics file): not a source. Live check C7: the player walks through a bramble.

**A solid body** (static proof: the activation of the two component sets at the image
addresses `0x17320E0` and `0xBB0530`). A render mesh is not a solid. A body is an entity of a
slice with one of these component sets:

| Component | Type id | What gives the shape |
| --- | --- | --- |
| game rigid body | `5ba2d1fc-db9d-4b81-88c9-89787d5c19f1` | `m_configuration`: `m_collisionType` 0 with `m_rnrAsset` (a physics file); type 0 with no `m_rnrAsset` (the physics file of the mesh of the same entity: the product of the same source asset whose path is the mesh path with the ending `.rnr`); type 1 (the simple shapes of `m_collisionShape`, with the offset `m_center`) |
| engine static physics with a mesh collider | `95d89791-6397-41bc-aac5-95282c8ad9d4` and `2d559eb0-f6fe-46e0-9fce-e8f375177724` | the physics file of the mesh of the entity |
| engine static physics with a primitive collider | `95d89791-...` and `9cb3707a-73b3-4ee5-84ea-3cf86e0e3722` | the shape component of the entity: box `5edf4b9e` (`Dimensions`, the full size), sphere `e24cbff0` (`Radius`), cylinder `b0c6aa97` and capsule `967ec13d` (`Height`, `Radius`) |

- The simple shapes of `m_collisionShape`: `c6651a66` (`m_box`) and `27462017` (`m_aabb`) are
  boxes with the half size on each axis (static proof `0x169DB60`), `7f2ef312` is a sphere
  (`m_Radius`), `709b11ea` is a capsule along z (`m_Height`, `m_Radius`).
- The place of an entity in its slice is `m_worldTM` of its transform component (`484ae67d`:
  12 numbers, the three basis vectors and the translation). It is relative to the root of the
  slice, not to the parent entity (live check C1: the wall of the start settlement is at its
  data place only with this rule). The transform of an entity of a chunk slice is relative to
  the `WorldPosition` of its chunk.
- An engine collider with no static physics component is no solid (a trigger). A static
  physics component with `EnabledInitially` 0 is no solid at the start (static proof
  `0xCC1D20`).
- A slice can make other slices: the spawner component `b52d0d52-99cd-41be-b6f7-d2519c15bf14`
  (`m_sliceAsset`, `m_sliceVariant`, `m_spawnOnInitialize`). The tool places the slice of a
  spawner only when `m_spawnOnInitialize` is 1. Live checks C5 and C13b: the crafting stations
  of the settlement and the gate of a fort (spawners that are off at the start) do not show
  with this backend, and the player walks through their places.
- The collision filter of a body is a text (`m_strFilter`, `CollisionFilter`). The file
  `sharedassets/physics/default.collisionfilters` has 76 categories and 209 filters
  (`InheritsFilters`, `IsCategories`, `CollideWithCategories`). Two bodies collide when a
  category of one of them is in the "collide with" list of the other one (inferred from the
  data). The filter of a creature is `AI`: it collides with `Character`, `CharacterOnly`,
  `Static`, `Structure`, and `Terrain`. The navigation data has only the placements that stop
  a creature (`Structure` 1.22 million, `Tree` 0.45 million, `Static` 0.29 million,
  `Static_NotTraversable`, `CharacterOnly`, `House`, and others).

**The physics file `.rnr`** (static proof: the reader at `0xCC6500`, one shape at `0x10F7920`;
little-endian; 10,620 files in the game copy).

| Part | Layout |
| --- | --- |
| header | magic u32 = `0x1234ABCD`, version u32 (6 or 7), set count u32 |
| surface set | text size u32, text, count u32, u16 * count |
| after the sets | 16 bytes; flag u8 (version 5 and more); u8 (version 7 and more); with the flag: count i16, u16 * count |
| shapes | shape count u8, then the shapes: type u32, body, user size u32, user data (empty, or 48 bytes: a 3 by 4 matrix of f32, the transform of the shape in the file). The first shape is the collision shape |

| Type | Shape | Body |
| ---: | --- | --- |
| 1 | box | half size 3 f32, margin f32 |
| 2 | sphere | radius f32 |
| 3 | convex hull | vertex count u32 (31 bits), plane count u32, vertices 3 f32, planes 4 f32, margin f32, adjacency flag u8 |
| 5 | cylinder | end point 3 f32, end point 3 f32, radius f32, margin f32 |
| 7 | capsule | end point 3 f32, end point 3 f32, radius f32 |
| 10 | mesh | version u32 (100), vertex count u32, triangle count u32, vertices 3 f32, indexes 3 u16 for each triangle, flag u8 (with it: 3 u16 for each triangle), tree size u32, tree bytes |
| 11 | compound | count u32, then for each child: a 3 by 4 matrix of f32 and a shape |

14,278 of the 16,900 meshes are closed surfaces: the tool fills a closed mesh as a solid (a
travel into a rock of the data puts the character into the shell of the rock, live).

**The terrain.** `region.heightmap` is a TIFF file: 2048 by 2048 samples of 16 bits, LZW in
strips of 4 rows, horizontal predictor. The sample of the local place (x, y) is in the row
2047 - y and the column x, and the height is sample * 2048 / 65535 m. The height at a place
is between the four samples around it. A neighbor sample that the world does not have (the
east and the north edge of the world) repeats the sample of the edge: with no east sample the
diagonal sample is the north one, and with an east sample and no diagonal one it is the east
one. The terrain reader of `navcore.cpp` took the mean of the east and the north samples for
a missing diagonal sample (finding F11 of the code review of stage 3.4d); now the two readers
of the tool (`height_maps.py`, `navcore.cpp`) give one height
(`tools/gamedata/check_navigation_tool.py`, case RN10). The data of 11 regions changed, only in
the tiles of their east or north edge: `r_+00_+06` to `r_+05_+06`, `r_+06_+00`, `r_+06_+01`,
`r_+06_+03`, `r_+06_+05`, and `r_+06_+06` (`r_+06_+02` and `r_+06_+04` are equal).

**A source that does not decode stops the tool** (finding F10 of the code review of stage
3.4d). A placed collider whose physics file does not decode, or is not there, stops the
region: no region file, no complete report, status 1 (before, the region had no collider
there and looked complete). The world build of 2026-10-13 had no such file in its 49
regions: the data does not change for this rule. A work file of the collision step from
before this rule is not a complete region (finding F6 of the review of the corrections of
user test 1, case RN10): the tool uses a stored report only when it says "complete", has the
format and the tool version of the tool, and names no physics file that did not decode. The
tool version is 2 (`TOOL_VERSION` of `navigation_data.py`): the next run of the tool makes
each region again. The backend does not read the tool version.

**The terrain is not the height map near some placements: the cookies** (static proof with
the addresses below; live proof C14 and C13a). A placement of a capitals file can have the
member `cookie` (`innerRadius`, `outerRadius`, `maxHeightBlend`, `edgeNoise`,
`edgeNoiseFeatureSize`, `groundNoiseScale`; the reader `0x69B42D0` gives the defaults 0.5,
8.0, and 0.5 to the three noise keys). The client changes the 16 bit samples of a region one
time, directly after it read the file (`0x69E7360` calls `0x6A26A70`), so that the terrain
near the placement goes to the height of the placement:

- Only the cookies of the placements of the same region, with `maxHeightBlend` above 0,
  sorted by their height with the lowest one first (`0x69B0440`). Each cookie changes the
  samples that the cookies before it changed.
- One cookie (`0x6A23AC0`) changes the samples of a square with the half side
  1.5 * outer - 0.5 * inner around its place. For the sample (i, j) the client computes the
  values at the place origin + (i + 1, j + 1): a shift of 1 m.
- The values at one place (`0x6A197F0`): the target height is the height of the placement
  plus (a + b - 1) * `groundNoiseScale`, where a and b come from two sums of noise (the
  function `0x6A023B0`). The weight goes from 1 at the inner radius to 0 at the outer radius
  with a smooth step, and the edge noise (`0x69F72E0`) moves the place and the radius by
  (outer - inner) * `edgeNoise` / 2 at most. The new sample is old + (target - old) * weight
  * blend.
- The noise is a texture of 1024 by 1024 values (read at `0x6A29B10`) that the client makes at
  its start (`0x6A0E2E0`, `0x6A03D70`) from OpenSimplex noise in 4 dimensions on a torus, with
  the constant seed 77374 (`0x6A208C0`, `0x6A1CF50`). The tool makes the same texture with the
  same public noise code (`tools/gamedata/vendor/open-simplex-noise`, public domain), and the
  program must be compiled with no joined float operations (`-ffp-contract=off`).
- Evidence: the height of the 760,708 vegetation instances near a cookie of the start region
  (an instance has the height of the live ground). The median difference to the terrain goes
  from 0.109 m (the plain height map) to 0.016 m (with the rule), and the 90 percent value
  from 1.819 m to 0.032 m. Five live positions of the player agree with the rule (0.043 m at
  most; -1.44 m to +1.08 m with the plain height map). The world has 5,235 cookies. The den
  of a bear at (10788.69, 4504.78) is 4.7 m below the plain height map (C13a): a cookie, not a
  hole in the terrain.
- The exact sequence of the float operations is in `nav_terrain.py` and in the command
  `cookies` of `navcore.cpp`. The terrain heights of the game data folder (`heights`) and the
  terrain height of each spawn point of the creature file are made from the terrain after the
  cookies: 3,143 of the 32,840 spawn points got a different terrain height (1,239 by more than
  1.0 m).

**The water.** `region.waterqt` is an object stream with `regionSize` (2048) and
`quadtreeNodes`: a node has `Height` (the water surface), `FloorHeight`, and `Flags`. The
nodes are in level order, a node with the height 65535 has four children in the sequence
(0, 0), (1, 0), (0, 1), (1, 1), and the smallest square is 1 m (static proof `0x69DA800`).
The terrain below the ocean level (`oceanLevel` 40.0 of `terrain.json`) has water too.

**The navigation marks of the level designers** (static proof: the names of the areas at
`0x39A1460` and `0x39DC880`). The navigation obstacle mesh component
(`500a7bfe-52fe-aa45-6b0e-b1253a4d7bf7`) has `m_navArea` (0 UNWALKABLE, 3 UNWALKABLE_SOLID, 8
WALKABLE_SOLID, 63 WALKABLE, 4 to 7 jump links), `m_meshSource`, and `m_registerWithNavMesh`.
The tool uses two forms: a mark with the areas 0 or 3 and the source 3 (the rigid body of the
same entity) makes the top of that solid no floor; a mark with the areas 0 or 3 and the
source 6 (`m_inlineShape`, a list of simple shapes) is a volume with no solid in which no
floor is free. The areas 8 and 63 change nothing. A mark with a different source is not read
and counted (472,809 with the source 0).

- **The gates of a settlement are closed for a creature.** The start settlement has three such
  volumes, each one in a gate of its wall ((8904.68, 4297.39), (8814.05, 4210.92), and
  (8894.8, 4146.5); boxes of 10 m by 3 m by 10 m and of 14 m by 3 m by 10 m). With these
  marks no creature has a way into the settlement, as in the game. The player walks through
  the gates (C2). Live proof: a wolf 8 m outside the north gate sees a character in the gate,
  finds no way, and stays; it attacks a character that stands at the outer edge of the gate.

**What the tool leaves out** (each count is in `navigation.json`): the navigation marks with
a source that the tool does not read (472,809); the spawners that are off at the start
(466,804: the trunk of a cut tree, the gates of a fort, the crafting stations, quest objects);
a variant with no entry in `region.slicedata` (21,159); a collider whose mesh has no physics
data (2,759) or no shape (2,528); a physics file or a mesh that is not in the game copy (569);
and 300 others.

**The agent: the size of a creature** (project values; one size for each creature; the tool
has them as defaults, and each file has them in its header).

| Value | Number | Cause |
| --- | ---: | --- |
| Cell of the file | 0.25 m | The analysis has a cell of 0.125 m, and a cell of the file is 2 by 2 analysis cells: an opening of 0.7 m stays open, and the data of the world are 1.2 GB |
| Radius | 0.35 m | The player stops 0.44 m from a wall face (C1) and goes through the opening of the railing of the porch (C3). A radius of 0.44 m closes that opening in a grid of 0.125 m |
| Height | 1.8 m | The height of a person: a creature does not go below a floor that is lower |
| Step | 0.35 m | The player goes up steps of 0.2 m and not up an edge of 0.37 m (C9) |
| Slope | 50 degrees | The player walks up terrain of 51 degrees (C10) |
| Wade | 1.2 m | The player walks on the floor of a pond in water of 0.97 m, and the spawn points of the creatures of that pond are on that floor (C11) |

**How a region becomes navigation data** (`navcore.cpp`, one tile of 64 m at a time with a
border):

1. Each triangle of each collider that stops a creature becomes spans: for each analysis
   column that it touches, its height interval. A convex shape and a closed mesh are solids.
   The terrain (after the cookies) is one span for each column.
2. The top of a span is a floor when its surface is not steeper than the slope limit and the
   free height above it is the agent height or more. A floor is free when no span of a column
   nearer than the agent radius is in the height interval of the body (from the step height
   to the agent height above the floor).
3. The navigation marks: the top of a solid with the mark "not walkable" is no floor, and no
   floor is free in a cut volume.
4. A floor below the water surface gets a water mark: shallow (the wade limit or less) or
   deep. A creature does not walk on a floor in deep water.
5. A cell of the file has the free floors of its four analysis cells (floors that are 0.25 m
   or less from each other are one floor): one place can have 16 floors. A cell with more
   floors stops the tool with a message (`kMaxCellFloors` of `navcore.cpp`; finding F8 of the
   review of the corrections of user test 1, case RN10: before, the tool wrote such a cell,
   and the backend refused the tile). The live runs of stage 3.4e read no tile that the
   backend refused (`0 with no data` in the line `navigation:`).
6. The sight: each floor has the height interval above it that is free in all four analysis
   cells. A cell with no free floor has its surfaces as floors with the mark "blocked", so
   that the sight over a wall has its interval too.

**The file of a region** (`r_+XX_+YY.swnav` in the folder `navigation` of the game data
folder; all numbers little-endian; a project format):

```
file      := header directory tile*
header    := "SWNV" version:u32 (3) regionX:i32 regionY:i32
             cell:f32 radius:f32 height:f32 step:f32 slope:f32 wade:f32   (the agent)
             tileCells:u32 (256) tilesPerSide:u32 (32) ocean:f32
             heightScale:f32 (the terrain height of the sample 65535: 2048) reserved:u32 * 2
directory := (offset:u64 size:u32 plainSize:u32) * 1024      (index = tileY * 32 + tileX)
tile      := one zlib stream of: terrain runCount:u32 run* extraCount:u32 extra*
terrain   := u16 * 65 * 65: the terrain samples at the whole meters of the tile, each one as
             the difference to the sample to its west (the first sample of a row: to the first
             sample of the row to its south), 16 bits with wrap
run       := code:u8 length:u16: the same code for `length` cells, row by row
             0 no floor; 1 one free floor, the terrain surface, the sky above it; 2 the same
             below shallow water; 3 the same below deep water; 4 the terrain surface, no place
             for a creature; 4 + n: n floors follow in the extra list for each cell of the run
extra     := height:u16 (1/32 m) flags:u8 sightLow:u8 (1/8 m) sightHigh:u8 (1/4 m)
             flags: 1 the terrain surface, 2 shallow water, 4 deep water, 8 blocked.
             The sight is free from height + sightLow to height + sightHigh
             (sightLow 255: no free interval; sightHigh 255: the sky)
```

One file has 32 by 32 tiles of 64 m, and each tile is one zlib stream: the backend reads only
the tiles near the players. `navigation.json` of the folder has the list of the regions, the
agent values, each thing that the tools left out, the time, and the memory of the tool. The
backend reads the header and the directory of each file at its start and refuses a folder
with a file that is not correct (a wrong version, a region that is not the region of the file
name, agent values outside their limits or not the same in each file, a directory entry
outside the file) as a whole: it then works with no navigation data, and the log says so.
Before the code review of stage 3.4d (finding F9) it kept the other files of a folder with
different agent values. A tile that does not decode is refused alone (`0 with no data` of the
line `navigation:`): a creature there walks by the rule of the terrain. A tile is not correct
also when a cell has more than 16 floors (the reader kept 16 of them before: finding F3) and
when its stream has a byte after the checksum of its zlib stream (finding F4).

**Numbers of the supported build.** The data of the world: 49 regions, 1,218,738,457 bytes
(the largest region 76.9 MB, a region of open sea 2.2 MB), 3.08 billion free floors, 2,320,424
placements, 5,235 cookies. The tool needs 43 minutes with 8 processes (the largest process
1.7 GB) and 2.1 GB of work files. A second run makes only the regions that are not complete.
The terrain heights after the cookies: 142 MB.

**The navigation of the world** (`world/navigation.h`; project rules, each constant in the
namespace `world::navigation_rules`). The behavior of a creature gets each place from two
functions, as in stage 3.4b; their bodies changed:

- `ground_height(position)`: the height of the floor of the navigation data that is nearest
  to the height of the position (one place can have more than one floor). With no tile of
  the place: the terrain height of the folder `heights`.
- `next_step(creature, from, goal, distance)`: the place after one step on the path of the
  creature. The result is a place (`moved`), "the creature waits" (the tile of its place, of
  its goal, of a link between two areas, or of the step itself is not there yet, or its
  search waits), "no way", or "no ground" (the creature stands on no free floor). A tile that
  the world does not have yet is no proof of "no way": the world asks for it, and the
  creature waits (code review of stage 3.4d: findings F5, a path in the memory of a creature
  that went into a tile that the world removed, and F8, a goal whose tile was on its way and
  that the navigation kept as "no way" for 3 s). The tile of the goal itself must be there
  before the navigation moves the goal to a free floor near it (finding F5 of the review of
  the corrections of user test 1, case RU6: before, a goal 1.7 m from the edge of a tile that
  was there went to a floor of that tile, and the creature walked while the tile of its goal
  was not there).
- **The place nearest to a goal with no way** (stage 3.4e; `next_step` with `nearest`, for a
  fight only). A goal with no way gives a path to the place that the creature can get to that
  is nearest to the goal: the search goes to the cells nearest to the goal first, in the
  limits of a search, and keeps the cell with the best estimate. The path goes to that cell
  when it is nearer to the goal than the start by one cell or more. The steps on it have
  `NavStep::partial`, and the arrival there is "arrived" with the same mark. A goal on no
  free floor (a character on a stone, a roof, a ledge) has a goal place of its own for the
  search. The creature searches again when its goal moved 3 m or more from the goal of that
  path (`kNearestRepathDistance`). The line `navigation:` counts these paths.
- **The tiles.** One time each 500 ms the world asks for the tiles that are 96 m or less from
  each character and 32 m or less from each creature on a path. A thread of its own reads
  them (the world thread reads no file), and the world takes them at the start of a tick. A
  tile that no one asked for in 10 s is removed. A creature whose tile is not there yet waits.
- **The areas.** The reader gives each free floor of a tile the number of its area (the floors
  that have a way to each other with steps of the step height or less). The world joins the
  areas of two tiles over their common edge when a tile comes. A goal in a different area has
  no way: the answer needs no search (for the place nearest to such a goal in a fight, the
  search runs, above).
- **The search.** First the straight line: when each cell on the line has a floor that the
  creature can step to, the path is the line. If not: A* on the cells (8 directions; a
  diagonal step needs its two side steps; a floor in shallow water costs 2 times). All
  searches of one tick of the world make 4,000 steps at most, and one search makes 60,000
  steps at most: the other searches wait in a queue, and a search continues in the next tick.
  A path that needs more steps is "no way" (the navigation does not look again for 3 s).
- **A new path.** The creature keeps its path while its goal is 1.0 m or less from the goal
  of the path. For a goal that moved more, the creature goes on with its old path while the
  new search runs, and the new path starts at a cell 12 cells ahead.
- **The step.** A creature goes to the farthest cell of its path that it can reach on a
  straight line. A step ends at such a corner of the path: the client moves a creature on
  the straight line between two of its movement states, and a line over a corner would go
  through the solid there.
- **Which creature walks** (replaces the rule "1.0 m or less from the terrain height" of
  4.17): a creature walks when a free floor of the data is 0.5 m or less from its place in
  the plane (the distance to the area of the cell of the floor) and 0.6 m or less in the
  height. Before the code review of stage 3.4d the limit was 0.5 m plus one cell to the middle
  of a cell (finding F6). The world asks one time, when the tile of the
  creature is there: until then the creature has no answer. A creature on no free floor does
  not leave its place: it turns to its target and attacks in its reach.
- **The sight.** A line from 1.5 m above the feet of the creature to 1.5 m above the feet of
  its target: each cell on the line must have a free interval at the height of the line. In
  the cells of the two ends, the part of the line in the cell must be in the free interval of
  the floor of the body there (a body in a solid stops nothing): a ceiling between a creature
  and a character on one vertical line stops the sight (finding F2 of the code review of
  stage 3.4d: such a line had no cell between its ends, and the creature attacked through the
  ceiling). From the review of the corrections of user test 1 (finding F1, case RU1) the test
  goes from cell to cell on the line (each cell that the line crosses, also over the corner
  of a cell), with the exact interval of heights of the line in each cell: before, a short
  line over the edge of two cells tested the height of the middle of the line only, and a
  creature saw and attacked through a ceiling there. An object lower than 0.5 m above a
  floor does not stop it. With no tile, nothing stops the sight. The vision rule, the start of an attack, and each contact of an attack
  use it: a creature does not take a character behind a wall as its target, it does not
  attack through a wall, and a contact does not hurt a target that went behind a wall while
  the attack played (finding F1).
- **With no navigation data** (no folder, a folder that is not correct, a region with no
  file, a tile that is not correct): the rule of stage 3.4b for that place (the straight line
  on the terrain of the folder `heights`, and no wall stops the sight). The log of the
  backend says at its start which one it has.
- The world measures the navigation (`World::load`, the line `navigation:` of `tools/world
  list`): the tiles and their bytes, the time of the tile reads, the searches and their
  steps, the time of the searches of a tick, the goals with no way, the largest queue, and
  the sight tests. No rule of the world reads these values.

**The behavior of a creature with the navigation** (changes of 4.17; `world/behavior.h`):

- A creature whose target has no way to it (a different area, a search with no path in its
  limit, the end of its path with the target not in its reach) goes to the place nearest to
  its target and waits there with it for 12 s (stage 3.4e, 4.17; before, it lost its target
  at once). Then it goes back, and its vision does not take that character again for 8 s. A
  hit of the character gives the target again.
- The way back is a path too. With no path back the creature walks its trail back (stage
  3.4e), and it is at its anchor in one step only when no session can observe it (before,
  at once).
- **Water and steep ground.** A creature walks in water that is 1.2 m deep or less, and its
  path prefers dry ground (a cell in water costs 2 times). A floor in deeper water is no
  place for a creature: a character that swims has no way to it for a creature, and the
  creature waits at the place nearest to it (stage 3.4e). A creature does not go up or down ground that is steeper than
  50 degrees, and it does not step up or down more than 0.35 m: it goes around. A character
  on a rock or on a roof with no way up is safe from a creature with a melee attack.

**The rules for a fight that a player can play** (stage 3.4d; project rules;
`world/behavior.h`, `world/health.h`, `gamedata/creature_data.h`):

- **The shelter.** For 10 s after a character came to a place (a world entry, a travel, a
  respawn, a revive by a command) the vision of no creature takes it as a target. The time
  starts when the client of the character reports its first position from the place (the
  loading screen of the client is over), and it ends 60 s after the arrival at the latest. A
  creature that has the character as its target loses it at a travel. A creature that the
  character hurts takes it in this time too. The values are in the entity of the character
  (`Entity::life`). Live: a character that travels to a place 9.0 m in front of an Ancient
  Guardian Reaver is its target 10.1 s after the first position report.
- **The rest.** A character that lives, and that took no damage and did no damage for 6 s,
  gets 3 percent of its maximum health each second (`Health::tick`). A creature that takes
  the character as its target, and each attack of a creature, starts the 6 s again. The
  vitals table of the game has no value for this (`HealthBaseTickRate` is 0 for a player).
- **The hits of an attack.** Two contacts of one attack that are 0.15 s or less from each
  other are one hit: 242 of the 2,091 attacks have two contacts with the same damage row at
  the same time (the two hit volumes of one swing), and in stage 3.4b each one was a damage
  result.
- **Light and heavy attacks.** The weight of an attack is the sum of the damage factors of
  its hits. An attack is heavy when its weight is more than 0.5 and more than 1.5 times the
  smallest weight of its slice (the lunge of a wolf: 0.7; its bite: 0.2). A creature makes
  its light attacks in the sequence of the file, and one heavy attack after each three light
  attacks. The pause after an attack is 1.5 s, and 2.5 s after a heavy attack (0.9 s in stage
  3.4b).
- **The crowd.** A creature does not end a step of its way to its target 0.5 m or less from a
  different creature that lives: it makes a shorter step or waits, and it attacks from its
  place when its target is in the reach of its attack. Creatures with the same target wait
  for each other. A creature goes through a creature with a different target (or with none)
  after 10 steps of waiting, also in the last steps of its way, on the free line to its
  target (finding F7 of the code review of stage 3.4d: there the patience was not counted,
  and a creature waited for a creature with no attack for all time). The way home has no
  such rule.
- Numbers of three live fights with a character of the level 26 (2,186 health, a sword of
  the first tier, two pieces of light armor) and Grey Wolves of the level 18:

| Fight | Stage 3.4b rules | Stage 3.4d rules |
| --- | --- | --- |
| One wolf, the character does nothing | dead after 24.4 s (92.8 damage each second; 5 bites, 4 lunges) | dead after 40.1 s (58.0 damage each second; 9 bites, 3 lunges) |
| One wolf, the character makes light attacks and steps back (16 and 15 hits of the harness) | dead after 51.8 s (48.4 damage each second) | lives after 68.9 s with 158 health (29.4 damage each second) |
| Three wolves, the character does nothing | dead after 10.4 s (228.7 damage each second) | dead after 11.7 s (199.5 damage each second) |

**Which spawn points walk** (the report of `tools/gamedata/navigation.sh DIR --spawn-points
creatures.json REPORT_DIR`): 31,296 of the 32,840 spawn points are on a free floor of the
data. Of the 24,228 spawn points of kinds that fight, 23,021 walk (6,893 did not walk in
stage 3.4b, by the rule of the terrain height). 1,207 do not walk: 545 are more than 0.6 m
below the nearest free floor (below the ground, or in a solid of the data), 512 are more than
0.6 m above it (on a mesh with no collision, or in the air), 86 are in deep water, 39 are on a
surface that is no place for a creature, and 25 have no free floor at their place.

**The load** (live, one character, the navigation data of the 49 regions). The backend start
reads the 49 headers. One character has 16 tiles (9.1 MB to 11.3 MB); after a travel of
6.4 km the world has the tiles of the two places for 10 s (32 tiles, 24.5 MB). The thread
reads a tile in 4.8 ms to 7.0 ms on average (16.1 ms at most). The largest tick of the world
with a world entry and two far travels: 9.6 ms (the tick of a travel writes the position row).
A fight with five wolves on the stairs of a porch: 5 searches with 2,317 steps in one tick,
7.4 ms; a search tick with the full 4,000 steps needs 2.2 ms to 6.1 ms. The peak memory of
the backend process: 97 MB. For 50 characters at 50 different places the tiles need 0.5 GB
to 0.65 GB (computed from 0.6 MB to 0.8 MB for each tile and 16 tiles for each place);
characters at one place share their tiles. Test cases NV6 and NV10 have the limits.

**The commands.** `tools/world path X0 Y0 Z0 X1 Y1 Z1 [--tsv FILE]` (route `GET
/v1/dev/world/path`; a place can be the name of a character) prints the corners of the path
between two places, its length, and the steps of the search. The command reads the tiles of
the two places in the world thread: the largest tick of the line `load:` then has that time.
`tools/world track CREATURE SECONDS [--tsv FILE]` prints the place of a creature each 200 ms.
`tools/world list` has the line `navigation:`. `tools/research/nav_draw.py NAV_DIR X0 Y0 X1
Y1 OUT.png --path FILE` draws the data from above with a path on it, and
`tools/gamedata/nav_file.py at NAV_DIR X Y` prints the floors of one cell.

**Not known, not done.**

- The original navigation agent, the original path rules, and the original vision rule are
  not known. One agent size is used for each creature: a large creature (a bear, a boss) goes
  through an opening of 0.7 m, and its body is in the wall beside it.
- The sight uses the collision shapes: an iron fence stops the vision of a creature as a
  wall does (live: the yard fence of the start settlement), and a window does too.
- No jump link, no drop from an edge, no climb: a creature has no way to a place that needs
  a step of more than 0.35 m. The marks of the level designers for jump links (areas 4 to 7)
  are counted and not used.
- A path that needs more than 60,000 search steps is "no way": a target 5 m away behind a
  cliff with a way around of 60 m or more is lost. Each such search uses the steps of 15
  ticks, and the searches of the other creatures wait in that time.
- A creature does not see a different creature as an obstacle of its path: the creatures of
  one fight stand in a queue behind each other on a narrow way, and only the ones in the
  reach attack. No creature goes around a different creature.
- Objects that move, and objects of spawners that are off at the start (a door, a gate of a
  fort, a crafting station), are not in the data. The client does not show them with this
  backend.
- The navigation marks with a shape of the designer that the tool does not read (the source
  0: 472,809 marks), and the meaning of the sources 4 and 5.
- A creature in the air or below the ground of the data (1,207 spawn points of kinds that
  fight) does not leave its place. The list with the cause of each one is the report of the
  tool.
- No swimming creature and no flying creature: a creature of deep water stands.
- The client moves a creature on the straight line between two movement states with its own
  speed. On stairs the body follows the steps; on a path with many corners the walk can look
  less smooth than in the game.

### 4.20 The lock of the input of a client: a conversation, a cutscene (static proof and behavioral proof, user test 1)

User test 1: the user talked to a person of the start settlement, and the character never
moved again, also after three travels. The backend log of the user had the client messages
6705 and 2473 two seconds after the character stopped, and no answer.

**What a conversation is in the client.** The key E at a person with the prompt "Talk" (the
option `OpenConversation`, action 63, a committed interaction; `interaction.md` and
`conversations.md` of the research) starts two things, and the client starts both itself
with no message to the server and no answer of the server:

- The interaction of the client: its record at Interactor +0x100 and +0x310 (0x4916540,
  0x4916D60), and the block of the input report (4.7, entry field 14).
- A player cutscene: the camera of the conversation (the picture of the person and of the
  character, the text of the person, "E Next", "ESC Back"). Its start (0x2EF13F0) sets the
  mark at PlayerCutscene facet +0xC24, calls method +0x420 of an interface of the player
  entity with 1 (the input of the character is blocked), and turns the action map "default"
  off.

**The end.** At the last "Next" or at Escape the client ends the cutscene (0x2F08890). For a
cutscene whose end the server must confirm (facet +0xC22) it does not free the input. It
sends the end and waits:

| Type | Direction | Body | Client code | Proof |
| --- | --- | --- | --- | --- |
| 6705 `PlayerCutsceneComponentServerFacet_OnClientCutsceneEnded` | client to server | `target:u64 entity:u64` (16 bytes; the target is the entity group id plus the component id `D49931510E1DD23F` of PlayerCutscene in the root entity) | sender 0x2EDA370, called by 0x2F08890 | live: one message for each end of a conversation |
| 6709 `PlayerCutsceneComponentClientFacet_OnServerCutsceneEndRequestFinalized` | server to client | `target:u64 entity:u64` | handler 0x2EFE730: clears facet +0xC24, calls method +0x420 with 0, turns the action map "default" on | live: the character walks after it |
| 2473 `InteractorComponentServerFacet_RequestCancelCommittedInteractionFromClient` | client to server | `target:u64 entity:u64 reference:32 bytes` (48 bytes; the component id `6C5718DBFE6DF612` of the Interactor) | sender 0x490AD30 | live: one message after each 6705 |
| 3746 `InteractorComponentClientFacet_CancelCurrentInteraction` | server to client | `target:u64 entity:u64` | handler 0x490EDA0 calls 0x4916150: in a committed interaction the client cancels it and sends 2473 | live: an open conversation closes, and the client sends 2473 and 6705 |

- The handler of 6709 with no cutscene changes nothing that plays: the mark is 0, method
  +0x420 gets 0, and the map is on (live: 6709 at a time with no conversation, and the
  character walks).
- The handler of 6708 (`OnServerCutsceneCanceled`, 0x2EFE670) stops a cutscene that waits for
  its grant (facet +0x130) and does not free the input: the backend does not send it.

**The answers of the backend** (`server/rep/lock_messages.h`, `RepSession`):

- 6705 of the own character: message 6709 at once, for the PlayerCutscene facet of the
  character. A call that names a different entity, or that is too short, gets nothing.
- 2473 (stage 2.5a, 4.35): a body of exactly 48 bytes with the reference of the committed interaction of the character ends it
  (`world/interactions.h`); a different reference, no interaction, or a short body changes nothing. Before stage 2.5a the backend
  had no interaction state and changed nothing.
- The request of the conversation (the block of the key E, action 63) gets 3743 result 1 from stage 2.5a. Result 2 closes the
  conversation at once (live check L1 of stage 2.5a: 6705 comes, the prompt "Talk" is back).
- A travel and a respawn send 3746 and 6709 before the messages of the travel (3951, 3954):
  a lock of a cause that the backend does not know ends at the latest there.
- Only the session of the owner gets these messages. The backend has no conversation: the
  person speaks the default text of its state and the window closes (`conversations.md`).

**Why a travel did not free the character.** The lock is the mark of the PlayerCutscene facet
and the input interface of the player entity: a travel (the movement state with a new
teleport number, 3951, 3954) does not touch them. The client sent 6705 one time, at the end
of the conversation, and then waited for 6709 with no time limit.

Live proof (run `ut1b`): the persons Tekla Petrowski (artificer, the place of the user), Hoang
Lin (Marauder commander), and Balian Clark (magistrate): the conversation opens, ends with E,
and the character walks 4.9 m, 7.9 m, and 8.7 m with the key W for 1.5 s. A lore page ("United
in Blood", E Read) opens the journal and locks nothing. A travel in an open conversation
closes it, and the character walks at the new place. A respawn: the character walks. Before
the correction (run `ut1a`): no movement after the conversation and after a travel; the two
development messages 3746 and 6709 (`tools/world message`) freed the character.

### 4.21 The rewards of a kill and the respawn of a creature (behavioral proof of what the client shows; the rules are project rules with the values of the game data)

Stage 3.5a. Research: `~/Games/soulwarden/work/research/loot.md` (verified). Evidence of the
live runs: `~/Games/soulwarden/work/evidence/loot/implement/` (`live/R1-*`, `live/L*`). Code:
`world/rewards.h` (the rules, `reward_rules`), `gamedata/loot_tables.h`, `world/creatures.h`
(the respawn time), `world/inventory.h` (`Inventory::grant`), `server/http/loot_commands.cpp`;
tools `tools/gamedata/loot.sh` (`loot_tables.py`) and `tools/gamedata/combat.sh` (the weapon
mastery of a weapon). Test cases LT1 to LT8.

**What the client shows** (behavioral proof, runs `loot-l1` to `loot-l4`). The backend sends
no new message: the rewards are changes of four states of the owner that the client has from
stage 1.3, 1.5, and 1.6. The HUD (`lootticker2024.luac`) shows one line for each change:

| Change | State | What the HUD shows | Proof |
| --- | --- | --- | --- |
| A new item or a larger stack | Container state of the bag (slot 61, type 1755): a journal entry with `ActionCausingSync` 1 (`ItemGathered`) for each slot that changed | The loot notice: the icon and the name of the item ("Longsword", "Fire Staff", "Padded Flawed Jasper Amulet", "Trapper Boots") | Static: 0x48D5EC0, 0x48E4130, 0x48E2CC0 (`loot.md` 3.1). Live: `R1-*` (a give command with the cause 1), `L8-01`, `L8-05`, `L8-07` (kills) |
| The total of the experience | Progression state (slot 70, type 899), group 1 field 0 | "+27 XP" (the difference of the totals), and "LEVEL 27" for a new level (group 0 field 0) | Live: `R1-*`, `L8-01`, `L8-11` |
| The coin | Currency state (slot 60, type 3786) | "+4.80 Coin" (the difference) | Live: `R1-*`, `L8-01` |
| The experience of a weapon mastery | Categorical progression state (slot 65, type 911): the rank and the experience in the rank | "+27 War Hammer Mastery" with the bar of the rank; a new rank also changes the point state (slot 67): "UPGRADE AVAILABLE, mastery points" | Live: `R1-*`, `L8-01`, `L8-05`, `L8-07` |

- The cause 0 (`ItemAdded`, the default of the give command) is not in the masks of the notice
  (research). The load of the bag at a world entry has the cause 0x11: no notice.
- The notice of an item shows also for a slot that is new in the vector (a full bag of 6 slots
  got the slot 6 with the cause 1: "Longsword").
- An item had five perk indexes 0 on the wire before stage 1.3b; a loot item now gets the
  perks of the roll of 4.23 (live I12: a rapier of a drop with the perk "Acutely Empowered").
  The client shows the perks and the gem that the item definition itself has: the amulet
  `AmuletJasperT2` shows "+1.19% Max Health" and "Strike Ward I", and its rarity "Uncommon"
  (live `L8-10`). A weapon of a drop table shows "Common" and its gear score (`L8-03`).

**The data** (game data, `tools/gamedata/loot.sh`, file `loot-tables.json` of format 1; 1.8 MB):

| Value | Source in the game files | Member of the file |
| --- | --- | --- |
| The experience, the coin (hundredths), and the progression amount of a kill of each creature level (70 levels) | Table `Rewards` (`RewardData`): `KillExperience`, `KillCurrency`, `KillProgressionCurrency` | `rewards` |
| The factors of each creature type (99 types: Solo, Solo+, Elite, Critter, ...) | Table `RewardModifiers`: `Experience`, `Currency`, `CurrencyRewardChance`, `LootDropModifier`, `ProgressionCurrencyAmount` (an empty cell in each row of the supported build: null) | `modifiers` |
| The level difference (the creature level minus the level of the character) | Table `AILevelDisparity`: `KillExpModifier`, `MaxRewardLevelDelta` | `disparity` |
| The coin chance of the kill event and of each override | Game events (`GameEventData`): `KillEvent` (`CoinRewardChance` 0.05) and each `KillEventIdOverride` of a vitals row | `events` |
| The credit data | `playerbaseattributes.pbadb`: `Event Credit Data` (`Credit Time Limit Seconds` 120, `Credit Health Threshold Percentage` 0.01, `Credit Range Limit Meters` 200, the 10 `Event Credit Modifiers`: experience, loot, and currency factors for 1 to 10 characters) and `Max AI Loot Receiver Count` 10 | `credit` |
| The gear score range of each item tier at each level | Table `XPLevels`: `GSLimitT1` to `GSLimitT5` (the row `Level Number` K is the level K + 1) | `gearScore` |
| The largest gear score of the loot of a creature level | Table `VitalsLevels`: `AILootGSCeiling` | `lootCeiling` |
| The loot data of each vitals row | The base vitals tables (`VitalsBaseData`): `CreatureType`, `CanDropLoot`, `LootDropChance`, `IgnoreLootDropModifier`, `LootTableId`, `LootTags`, `NoExpNoCoin`, `KillEventIdOverride`; the level variant rows (`BaseVitalsId`) | `bases` (7,408), `vitals` (8,779 ids) |
| The loot tables that a vitals row can reach (108) | `LootTablesData`: the rows `NAME` (`AND/OR`, `RollBonusSetting`, `Conditions`, `ConditionOverridesRoll`, `UseLevelGS`, `GSBonus`, `ItemN`, `GearScoreRangeN`), `NAME_Qty`, and `NAME_Probs` (`MaxRoll`, the thresholds) | `tables` |
| The loot buckets that such a table names (85) | `LootBucketData`: `LootBucketN` (row 0), `ItemN`, `QuantityN`, `TagsN`, `MatchOneN`, `OddsN` | `buckets` |
| The weapon mastery of a weapon | `WeaponItemDefinitions`: `WeaponMasteryCategoryId` (125 of 587 weapons: the weapons of a player) | `combat-tables.json`: value 12 of a weapon row |

- The tool stops (status 1, no file) for a reference of a table, of a bucket, or of a vitals
  row that does not resolve, a quantity or a range text that is not a number, reward levels
  that do not start at 1 and go up by 1, and a number that is not finite. From stage 3.3a
  (code review of stage 3.5a, findings F3 and F4) also for a variant vitals row whose base row
  is not there, a kill event override or a creature type of a base row that the events or the
  reward modifiers do not have, fewer credit modifiers than receivers, a negative reward or
  reward factor, a coin chance outside 0 to 1, and the flag text "NONE". Facts of the game
  data that the tool accepts, each one in a list of the tool: the flag text "TRULE" (true),
  the chance text "z" (0), the tag range "49-59-59" (49 to 59), and the four variant rows with
  no base row (`VARIANTS_WITH_NO_BASE_IN_GAME`: Werewolf_Rabid_20328, Upyr_10221a_Named,
  Upyr_10220a, Upyr_10220b). The whole part of a reward is its value (a cell has the text
  "340.00000000000006"). The check is `tools/gamedata/check_loot_tool.py` (cases LT8, RL3).
- The backend refuses a file whose parts do not fit together, as a whole (case LT1): with no
  file (or no valid file) a kill gives nothing, as before stage 3.5a. From stage 3.3a (finding
  F2, case RL2) this includes a base row whose creature type or kill event the file does not
  have, and fewer credit modifiers than receivers. A combat tables file of
  a tool from before has no mastery: a kill then gives no mastery experience.
- The respawn values of a spawner (`m_minRespawnCooldown`, `m_maxRespawnCooldown`) are in the
  creature file since stage 3.4a (spawn point values 7 and 8): 16,713 points have 360 and 360,
  15,800 have 360 and 0, 184 have 360 and 15, and 143 have other values.

**The rules** (project rules; the values are the values of the game data):

| Rule | What the backend does |
| --- | --- |
| A kill | A death of a creature by the hit of a character (`Creatures::hurt` with an attacker). A death by the health command or by the kill command gives nothing. |
| The credit | Each hit of a character adds its damage (not more than the health that the creature had) to the credit entry of that character in the creature (`Creature::credit`). At the death a character gets the rewards when its damage is 1 percent or more of the maximum health of the creature, its last hit was 120 s or less before the death, and it is in the world, lives, is not ignored by the creatures (`tools/world ignore`), and is 200 m or less from the creature (the credit data of the game, a rule of this project for an AI kill). 10 characters at most, the most damage first. The credit modifier of the game for that number of characters applies to the experience and the coin (1 character: 1; 2: 0.7 and 0.8; 3: 0.55 and 0.6); the loot factor is 1. The credit of a creature goes away when it is at its anchor with its full health (the reset). |
| The experience | The reward level is the creature level, at most the level of the character plus `MaxRewardLevelDelta` of the level difference (10 for +10 to +20). A difference outside the table has the row at its end (-20 or +20); a difference with no row (-9 to +2) has no factor. The experience is `KillExperience` of the reward level times the credit modifier times the experience factor of the creature type times (1 + `KillExpModifier` of the difference), the whole part (the calculation of the client, 0x5C4C1D0; `loot.md` 5.1). It goes to `Progress::add_experience`. Worked example: a Grey Wolf of level 18 (Solo) and a character of level 26: the difference -8 has no row, `KillExperience` 27: 27 experience. A Sacred Starhide of level 61 and a character of level 26: the difference +35 has the row +20 (delta 10): the reward level 36, 45 experience. |
| The weapon mastery | The weapon in the hand of the character (`Combat::weapon_in_hand`, 4.15) and the mastery of its weapon row (`WeaponMasteryCategoryId`): that mastery gets the same amount as the character, through `Skills::add_experience` (4.13). No weapon, or a weapon with no mastery: none. A rule of this project: the factor of the game (`ProgressionCurrencyAmount`) is an empty cell in each row of the supported build, and the kill handler of the client (0x31FD9E0) gives an empty weapon list to the calculation; the client builds a list of the masteries of the weapons of the slots 24 to 26 for a different event (0x31C2280). |
| NoExpNoCoin | No experience, no mastery, and no coin (764 base rows); the loot is rolled. |
| The coin | The chance is `CurrencyRewardChance` of the creature type when it is above 0, or the coin chance of the kill event of the vitals row (0.05). With that chance (the generator of the world): `KillCurrency` of the reward level times the coin factor of the creature type times the credit modifier, the whole part (hundredths of a coin). |
| The loot | A vitals row with `CanDropLoot` and a loot table. The outer chance is `LootDropChance` times the loot factor of the creature type (not with `IgnoreLootDropModifier`) times the loot factor of the credit; with that chance the loot table of the row is rolled (below). |
| The items and the coin | The inventory system (`Inventory::grant`): each item goes into the stacks of the item that are not full first, then into new stacks in the first empty slots, with the whole quantity of the drop (not the limit of the give command: finding F11, case RL11); each slot that changed has the cause 1 (the loot notice). The coin is in the same write (`Store::write_items` with the coin), not above the wallet limit. |
| One change | From stage 3.3a (code review of stage 3.5a, finding F1; roadmap rule MP-7) the reward of one character is one transaction of the store (`Store::Batch`): the experience, the mastery, the items, and the coin. Each write of the three systems is a savepoint of it. When the store refuses a write, the transaction writes nothing, and the entity of the character gets its values of before again (each system changed it after its own write): no part of the reward is given (case RL1, `RewardCounts::refused`). A part that a rule refuses (a mastery that the progression tables do not have) is not given, and the other parts are. |
| A full bag | The bag has 500 slots at most (the vector of the client). An item that has no slot is not given: the world has no item on the ground in this stage (`loot.md` 3.2: an ItemDrop needs a loot entity and the LootTracker). The other items, the coin, the experience, and the mastery are given. The weight of the items is not a limit (the client shows the encumbrance). |
| The respawn time | At the death of a creature of a spawn point, the point waits until the death plus the respawn time of its spawner: a time from the smallest to the largest value with the generator of the world when the largest value is larger; the smallest value when it is not (a largest value of 0 is no value: the version converter of the client, 0x3BF6720, gives an old spawner only `m_minRespawnCooldown`); 30 s for two values of 0 (`kShortestRespawn`, 4 points). Then, with a player 150 m or less from the point, the body leaves the world and the point makes a new creature (a new entity id, full health, its patrol from the first waypoint); with no player near, the body leaves by the distance rule (200 m) and the point waits for a player. A creature that a command removed has no respawn time, also a dead body with a respawn time of its death (finding F8, case RL8): its point waits for the command `reset`. The respawn time costs nothing in a scan: a scan looks only at the points near a player (case LT7). The respawn times are not in the database. |
| The loot switch | A development value of a character (the debug message `DebugSetForceDropAILoot` of the client, type 1356, `loot.md` 3.3): the outer chance and the coin chance of its kills pass. `tools/world loot CHARACTER on`, the admin command `set_force_loot`, the mark "Each kill drops loot and coin" of the tab "Character" of the tool window. |

**The roll of a loot table** (the rules of the roller of the client, 0x368B070, with the
generator of the world; `loot.md` 1.3 and 1.4):

- The conditions: a condition that is a numeric tag of the roll adds its value to the bonus;
  of the other conditions (tags) one must be a tag of the roll, or the table gives nothing.
  The numeric tags of a kill: `Level` (the level of the character), `EnemyLevel` (the creature
  level), `MinContLevel` and `MinEnemyContLevel` (the lower one of the two levels), and
  `GlobalMod` (0). The tags of a kill are the `LootTags` of the vitals row ("Beast",
  "Common"). A loot limit (`[LIM]...`), a territory, a season, and an event are not tags: a
  table with only such conditions gives nothing.
- The roll: `ConditionOverridesRoll`: the bonus is the roll. Else `AddToRoll`: a whole number
  from 1 to `MaxRoll` (1 for `MaxRoll` 0) plus the bonus; `ClampMax`: a number from
  min(bonus + 1, `MaxRoll`) to `MaxRoll` (0 for `MaxRoll` 0); `IgnoreBonus`: from 1 to
  `MaxRoll`. An empty setting is `AddToRoll`. A whole number of a range is uniform (the model
  of `loot.md` 1.3: the float helper of the client is not exactly uniform).
- An entry is reached when its threshold is the roll or less (the entries are in the sequence
  of their thresholds). AND: each reached entry (from the highest one down). OR: one entry of
  the highest reached threshold; one of the entries with that threshold, each with the same
  chance.
- An entry: an item with its quantity (a whole number of its range; a largest quantity of 0
  gives 0); a table visited quantity times; a bucket with quantity selections.
- A bucket selection: each candidate whose tags fit (a numeric tag: the value is in the range,
  the two ends included; a tag of one number has the range to 10000, the bucket parser of the
  client, 0x4458900; MatchOne: one tag is enough, else each tag), then each candidate with its
  odds (`OddsN`, 1 for an empty cell), then one of the candidates that are left with the same
  chance, and a quantity of its range. The loot bias (`LootBiasingEnabled`) and the filter of
  the looted items (`FilterLootedItemsN`) are not done.
- A table that a path visits a second time gives nothing (the guard of the client: "Attempting
  to visit table '%s' for a second time"); a path is 32 tables deep at most, and a roll gives
  64 items at most.
- The gear score of an item (a model of 0x367F590 and 0x368EBE0, `loot.md` 2.1): the
  `GearScoreOverride` of the item; or the range of the nearest entry that has one; or, with
  `UseLevelGS` of a table on the path, the range `GSLimitT<tier>` of the lower one of the two
  levels, not above `AILootGSCeiling` of the creature level; or the range of the item table.
  The `GSBonus` of the tables on the path adds to it. Not above the largest gear score of the
  item; the smallest gear score of the item table is not a limit (a Fire Staff of tier 2 with
  200 to 300 in its table has 196 from a Grey Wolf of level 18: `GSLimitT2` of the level 18
  is 180 to 220, the ceiling 220).
- Measured with the data of the supported build (`tools/world roll`): a Grey Wolf of level 18
  for a character of level 26 gives an item in 83 of 2,000 kills (the outer chance 0.1, then
  the common table: 35 percent no entry, the gear table: 40 percent no entry); with the loot
  switch a Risen of level 18 gives one of 51 items (weapons and armor of tier 2, the Lost
  family armor, ectoplasm, Azure vials, travel stones).

**The commands** (development routes only, but the switch): `tools/world loot CHARACTER
on|off` (`POST /v1/dev/world/force-loot?character=ID&force=0|1`; admin `set_force_loot` with
`force`; the state of a character has `health.forceLoot`), `tools/world roll KIND [LEVEL]
[--count N] [--forced] [--character CHARACTER]` (`GET /v1/dev/loot/roll`: for each item the
rolls that gave it, one for each roll also when a roll gave it more than one time (finding F10,
case RL10), and the quantity of all its drops; nothing is given), `tools/world creature respawn CREATURE SECONDS` (`POST
/v1/dev/creatures/respawn?creature=ID&seconds=N`: the respawn time of the spawn point of a
dead creature from now). `tools/world list` has the line `rewards`, and `tools/world
creatures` has the respawn time of the point of a dead creature.

**Live results** (the character `Soulwarden`, level 26 then 27, the war hammer `2hWarhammerT2`
in weapon slot 2 at mastery level 8):

| Kill | Where | What the client showed |
| --- | --- | --- |
| Grey Wolf (`Wolf_Grey_7`, level 18), with the behavior of the creatures on | Start area, `8806 4312` | "+27 War Hammer Mastery", "+27 XP", "+4.80 Coin", "Fire Staff" (gear score 196; `L8-01`, `L8-03`). The wolf bit the character once before its death. |
| Fallow Scavenger (a Risen, level 18) | The street of the start settlement | "+27 War Hammer Mastery", "+27 XP", "+4.80 Coin", "Padded Flawed Jasper Amulet" (gear score 190; `L8-05`, `L8-10`). Two kills before it gave no item (the common table). |
| Sacred Starhide (a bobcat, level 61) | Brimstone Sands, `7172.6 8272.2` | "+45 War Hammer Mastery", "+45 XP", "+6.60 Coin", "Trapper Boots" (gear score 270; `L8-07`, `L8-09`) |
| A Sacred Starhide with the experience 20 below the next level | Brimstone Sands | "LEVEL 27" with the four notices (`L8-11`) |
| Young Scorpling (a Critter, `CanDropLoot` false) | Brimstone Sands | No notice: the creature type Critter has the experience factor 0 (`L8-06-a-critter-...`) |
| Sacred Nemean (level 61, 28,348 health) after the health command 20 | Brimstone Sands | No notice: the hit did 20 damage, less than 1 percent of the health (`L2-...`) |
| The respawn of the Sacred Starhide of the spawn point | Brimstone Sands | 360 s after the death (the spawner 360 and 360) the client had no body (the client removes it after 300 s), and at the next scan the creature stood at its point with its name plate and its full health (`L5-02`, log line `world: creature ... of the kind Bobcat_61 ... (spawn point)`). |

**Not known, not done (stage 1.3b and later):**
- The perks of an instance: done in stage 1.3b (4.23: the perk roll of the client, the five
  perk places on the wire and in the database). Open: the `PerkBucketOverridesN` and
  `PerkOverridesN` of a loot table entry (no table that the backend reads has them).
- The gems, the dyes, the bind rule of a new instance (`BindOnPickup` is sent as the flag of
  the definition), the loot bias for the equipment of the character (`LootBiasingChance` of
  `XPLevels`), the loot limits and their counters (`javelindata_lootlimits`), the filter of
  the looted items, the tags of a territory, of an event, and of a mutation, the high water
  mark (`HWMMult`), and the `ChanceToExceedIndex` of a table.
- The original credit rule of an AI kill (the credit data is the data of a game event; the
  group credit modifiers are not used), rested experience, the Azoth and the territory
  standing of a kill (`TerritoryStanding` of the creature type, `Minimum AI Level For
  Territory Standing` 7), the currency of a season, and the loot of a quest.
- An item on the ground and the LootTracker (slot 33, type 982) with its AI loot entry; the
  timers of a chest (60 min) and of the AI loot (5 min).

### 4.22 Abilities: the keys of the bar, the cast, the cooldown state, the status effects of a creature (live proof of stage 3.3a; the rules are project rules with the values of the game data)

Stage 3.3a. Research: `~/Games/soulwarden/work/research/ability-use.md` (two passes, "not
ready") and `abilities-effects.md`. Live checks L1 to L8 and the live proof:
`~/Games/soulwarden/work/evidence/abilities/implement/` (`live/LIVE-CHECKS.md`, `live/L*`,
`proof/V*`). Code: `world/abilities.h` (the rules, `ability_rules`, `Abilities::plan`),
`gamedata/ability_data.h`, `world/creatures.h` (`apply_effect`, `knock`), `world/behavior.cpp`,
`world/combat.h` (`Combat::strike`, `Combat::claim`), `server/rep/client_input.h`,
`server/rep/states/cooldown_state.cpp`, `server/rep/states/status_state.cpp`; tools
`tools/gamedata/abilities.sh` (`abilities.py`) and `tools/gamedata/creatures.sh` (the reaction
fragments of a slice). Test cases AB1 to AB12.

**The keys** (live proof L1, static proof `0x651C180`, `0x6870BB0`). A key of the bar sets bits
of the action words of the input entry (4.7). The first entry of a press has the press bit and
the virtual bit, one or two entries have the hold bit, and one entry has the release bit:

| Key (place of the bar) | Press | Virtual | Hold | Release |
| --- | ---: | ---: | ---: | ---: |
| Q (place 0, input number 402) | 402 | 479 | 39 | 211 |
| R (place 1, input number 115) | 115 | 571 | 332 | 196 |
| F (place 2, input number 139) | 139 | 523 | 207 | 172 |

- The bits are the same for each weapon: the weapon in the hand and its bar give the ability
  (`Abilities::press`).
- The client plays the ability with no answer of the server: the animation, the movement of
  Charge (about 10 m), and its own cooldown (the setter 0x6923930 runs at the press with the
  ability id as the key). It does not play a press in its own cooldown, and it sends the bits
  of that press. Its HUD shows no countdown for its own cooldown (live L1).
- The client has no message for an ability start and none for a refusal (L2: no client message
  other than 1589 near a press; `abilities-effects.md` 2). A cooldown of the server alone does
  not stop the client: a press in it plays the ability (L2). The backend sends no refusal and
  no correction (none is proved).
- One press after the first weapon change of a session sent the key bits, and the client did
  not play the ability (no setter call, live L1 and run r2). Its input entries were equal to
  the entries of a press that played, in each field. Stage 1.3b (I8): not reproduced in five
  tries after a weapon change (live I8-01 to I8-04). A press in the local cooldown of the
  client gives the same symptom: the client sends the bits, does not play the press, and keeps
  its countdown; a cooldown reset of the server between two presses (`tools/world cooldowns
  NAME reset`, used in the tries of stage 3.3a) made the server take such a press (live I8-05).
  The backend now keeps the end of the client cooldown and refuses such a press (the rule
  "The cooldown" below; test case IZ9; live I12).
- After a travel the body of the character faces north, also when the camera faces a
  different direction. Wrecking Ball and Charge go in the direction of the body (live run r3):
  a short walk turns the body to the camera.

**The contacts of an ability** (live proof L3). The client reports each melee contact of an
ability in the third list of its input entry, as for an attack (4.15): provider FFFFFFFF, and no
ability id. Measured (input steps of 1/30 s from the press):

| Ability | Contacts |
| --- | --- |
| Wrecking Ball | one, 24 steps after the press (0.8 s) |
| Charge | one, 14 steps after the press, for a creature 6 m in front; none for a creature that the run went past |
| Maelstrom (with No Reprieve) | two, 14 and 26 steps after the press (the two spins), with two different hit volumes |
| Gravity Well | one, 16 steps after the press (the swing of the axe) |
| Shockwave, Path of Destiny | none: no item in any list (L7) |

**The shot of Gravity Well** (live proof L7). 15 steps after the press (the marker
`CastGavityWellProjectile` at 0.533 s) the entry has one item of the second list (192 bytes,
4.7): the origin at the character (1.5 m higher), the direction of the camera, and at byte 180
the value `attack` 8E14366B, the CRC-32 of "gravitywell" (the `RangedAttackName` of the spell
row). The client shows no vortex: the vortex of the game is an entity of the server (the slice
`slices/items/weapons/greataxe_gravitywell_vortex.dynamicslice`), which this backend does not
make. The backend reads the origin, the direction, and the attack of each shot
(`InputShot`).

**The cooldown state** (live proof L4; static proof: constructor 0x66F3C90, writer 0x666C2B0,
reader 0x6951A10, apply 0x683B0B0, getter 0x67E9840). Slot 45 of the player slice, type 2932
(`MB::CooldownTimersComponentReplicatedState`), owner only (the remote player slice has no
such component):

```text
group mask 01, field mask 07 (fields 0, 1, 2: the maps of the places 0, 1, 2 of the bar)
field := 00 (no change entries) 01 sequence:size64 count:size ( abilityId:u32 end:u64 minimumEnd:u64 )*
```

- The three outer keys of the component (+0x5D4, +0x5D8, +0x5DC) are 0x192, 0x73, 0x8B in the
  live client: the input numbers 402, 115, 139 of the three places. The apply function gives
  field N to key N: field 0 is the map of place 0 of the bar of each weapon (Charge and
  Wrecking Ball), and the key in a map is the ability id (live: Charge in field 0 shows the
  countdown on Q).
- The two times are the server time in microseconds: the time of the world clock (the steady
  clock of the backend, the clock of the registration reply, 3.4) divided by 1,000. The client
  shows the end minus its actor time as the countdown (live: 19 then 15 for an end 20 s after
  the send). The second time is the minimum end (the start plus `MinTime` of the data:
  `ability-use.md` Q4).
- The backend sends each map in the full form with the world revision of the change as its
  sequence: a list with a larger sequence replaces the list of the client. A cooldown that
  ended leaves the maps at the next change. No fragment in the first record of a character
  with no cooldown (the stored bytes of case T2 did not change).
- A cooldown is per ability, not per place (live: Charge started 6 s after Wrecking Ball, both
  on place 0).

**The status effects of a creature** (live proof L5; static proof: constructor 0x4013900,
apply 0x4028F70, compact reader 0x4044030). The component `StatusEffects` of a creature slice
(state type 4236; its slot is a value of the slice: 8 for the Grey Wolf, `CreatureSlice::
statusSlot` from the slot list of the creature data). Group 3 has the maps of an observer:
field 0 the full map (`statusIndex:u16 wallEnd:u64 potency:f16 stacks:u8 flags:u8`) and field 1
the compact map (`statusIndex:u16 packed:u8`, bits 0 to 6 the stacks, bit 7 the potency 1).

- The client shows the icon of an effect at the name plate of a creature from the compact map
  only: the full map alone shows no icon, and an empty compact map removes each icon (live
  L5). The backend sends only the compact map (`00 01 sequence count (index 81)*`), with the
  effects whose status row has `ShowInNameplates`.
- The number of an effect is the position of the CRC-32 of its lower-case id in the list of
  the CRC values of all rows of the status tables in the unsigned order, with the duplicate
  (Sanctuary): the list of the live client (manager `[[0x14A333CE8]]` +0x218) has 4,517
  entries, and WarHammer_Shockwave_Stun is 1710, WarHammer_Ult_SlowOnCC 1728,
  GreatAxe_Vortex_Root 1059, Status_Passive_GreatAxe_Gravity_Root_Hold 2182 (the "all rows"
  candidates of the research; the tool gives the same numbers).
- A compact entry has no end: the server sends the list again at the end of an effect.

**The actions of a control effect** (live proof L6, run r2). The action `Stun` of a slice
(fragment `r_Stun_Loop`, 432 of 503 slices) plays a stun pose. The action `Reaction` with the
fragment `r_R4_F` of the slice (the reaction of the power 4, 454 slices) plays a knockdown: the
Grey Wolf rears up and falls on its back (`first-run/R2-01-*`, `proof/V01-*`). The fragment
indexes of the reactions of the five powers (`r_R0_F` to `r_R4_F`) are a member `reactions` of
a slice of the creature file (`tools/gamedata/creature_data.py`, `read_reaction_fragments`).

**The data** (game data, `tools/gamedata/abilities.sh`, file `ability-data.json` of format 1):

| Value | Source in the game files | Member |
| --- | --- | --- |
| The cooldown of each ability (98) | `CooldownData`: `AbilityID`, `Time`, `MinTime` | `cooldowns` |
| Each status effect (4,516 ids of 4,517 rows): its number, `BaseDuration`, `MoveSpeedMod`, `Stunned`, `NoNav`, `StopOnHitCount`, `ShowInNameplates`, `DMGStandard` | Each `StatusEffectData` table | `statusEffects` |
| Each spell (1,777): `Radius`, `Height`, `CastDistance`, `NumToSpawn`, `SpawnRate`, `Duration` | Each `SpellData` table | `spells` |
| The damage rows of the abilities | `DamageTable` (the combat tables, 4.15) | `combat-tables.json` |

The tool stops (no file) for a table type that is not there, a number that is not finite, a
negative cooldown, a flag text that is not a flag, and two rows of one list with one id and
different values. The backend refuses a file that is not complete as a whole; with no file it
refuses each press (`no data`).

**The rules** (project rules; the values of the data where it has a value):

| Rule | What the backend does |
| --- | --- |
| The cast | The session gives each press bit of a new input step (one cast for each input sequence: a client sends an entry in five reports) to the world (`Abilities::press`), with the input number, the look direction of the camera, and the lock (a travel that the client did not confirm). The world finds the character from the session, the weapon in its hand (`Combat::weapon_in_hand`) and its ability table (the weapon mastery of the weapon row), and the ability of the place of the key on the bar of that table. It refuses (no cast, no cost, no hit, no message) when the character is dead, the input is locked, the weapon has no ability table, the place is empty, the character does not have the node, the ability has no rule here or the data has no row for it, its cooldown runs, or a different cast plays (the time of the cast below). |
| The cooldown | At an accepted press, the cooldown of the data (`Time`, and the minimum `MinTime`) on the world clock: a value of the character with the character id as its key (`Abilities`), which a new session and a new entity of the character keep. The end of the cooldown of the client is a second value (stage 1.3b, I8): a reset command ends the cooldown of the server, not that of the client, and a press before the end of the client cooldown is refused (live I8-05 and I12: the client does not play such a press and keeps its countdown). A cooldown reduction (Stimulated Reduction, Refreshing Move) changes the two ends. |
| The cost | Stage 1.3b (I14, 4.23): the mana cost of the first spell of the plan whose `ManaCost` id the mana table of the players has, times 1 + the `ManaCostMult` of each learned node that lists the id or "All"; a press with less mana than the cost makes no cast and starts no cooldown; an accepted cast pays through the one mana path (`Abilities::change_mana`). The six abilities of the build have no cost: their spells name `SpecialAttack` (war hammer) or no id (great axe), and the mana table has no row `SpecialAttack` (static proof: the client pays nothing for an id with no row). Live I12: six casts, the mana stays 100, no mana meter (as live L8). |
| Melee phases | A contact of the session of the caster in the window of a phase (input steps from the press, and the same time on the world clock plus 0.5 s) is a hit of that phase (`Combat::claim`), with its damage row; one hit of a creature for each phase. The hit check of a melee attack stays (the target, the observer, the immune mark, the weapon, the distance, the point, the rate). The damage goes through `Combat::strike`, the one function of each hit: `Combat::damage` with the row, `Creatures::hurt` with the attacker (the credit and the rewards of a kill), the damage number to the owner. |
| Area phases | At the time of a phase (a timer of the world clock), the creatures of its circle from the position index (`World::for_each_near`: no scan of the world), not more than 3 m higher or lower (or the `Height` of the spell), with no wall between the center and the creature (`Navigation::sees`), 32 at most (the nearest first); each one gets the damage and the effects one time for each phase. A creature on its way back takes nothing. A caster that left the world or died does nothing. |
| The effects | A status effect has the values of its row: a stun (`Stunned`: no movement, no attack; its attack that plays stops, and it plays its action `Stun`), a root (`NoNav`: no movement; it turns and attacks in its reach), a slow (`MoveSpeedMod`: the speed of each step times 1 + the value; the strongest slow of the effects), a weaken (`DMGStandard`: the damage of the creature times 1 + the value, `Combat::creature_damage`), the end at the next hit (`StopOnHitCount`). The same effect again gets the later end. A knockdown (power 4) or a stagger (power 2): the action `Reaction` with the reaction fragment of the power, and no movement and no attack for 2 s or 1 s. The world ends each effect at its time (only the creatures with an effect). A creature on its way back takes no effect, and a creature with a root or a stun on its way back waits (it is not at its anchor). |
| Charge | One phase: steps 3 to 90, row `GreatAxe_Engage1` (the build has no upgrade of Charge). Cast time 0.5 s. From stage 3.3b-S4 the dash of the client in the envelope of the position check, and the dash for the observers (4.26). |
| Riposte | Stage 3.3b-S4 (4.26): the window `Rapier_Riposte_Timer` (1 s) on the caster at the press; a creature hit in the window starts the counter of the server: at 0.433 s the row `1H_Rapier_Riposte_Stun` (`_Enhanced` with the node Rapier_Riposte_Mod3) on the attacker, one time, with the stun of its StatusEffect. Cast time 1 s. Cooldown 18 s (data). |
| Maelstrom | Phase 1: steps 3 to 20 (to 45 with no No Reprieve), row `GreatAxe_Maelstrom` with `_Reach` (Storm's Reach) and `_RP2` (Gravity); phase 2 with No Reprieve: steps 21 to 45, row `GreatAxe_Maelstrom_NoReprieve` (`_RP2`). Gravity: the root `Status_Passive_GreatAxe_Gravity_Root_Hold` (0.4 s) 0.33 s after a hit. Cast time 0.9 s. |
| Gravity Well | The swing: steps 3 to 30, row `GreatAxe_GravityWell` (0.25). The projectile: the first shot of the session with the attack "gravitywell" (for an origin more than 5 m from the character, the origin is the character, 1.5 m higher), or with no shot the server throws it from the character in the direction of the press 0.83 s after it. It flies in the plane to the first creature that is 1 m or less from its line, to a wall, or to `CastDistance` of `Spell_GreatAxe_GravityWell_Projectile` (10 m), at 20 m/s; the vortex starts there (the radius of `Spell_GreatAxe_GravityWell_Vortex`, `_Upgrade` with Unyielding: 4 m): the root `GreatAxe_Vortex_Root` (1 s) and the slow `GreatAxe_Vortex` (60 percent, 3 s). 3 s later the burst: row `GreatAxe_GravityWell_Burst` (Corruption), with Crowded Well 10 percent more for each creature in the vortex, 5 at most. Cast time 0.6 s. |
| Wrecking Ball | One phase: steps 3 to 45, row `Warhammer_Damage_WreckingBall` (`_Push` with Breathing Room), a knockdown, and Aftershock. Cast time 0.8 s. |
| Shockwave | 0.8 s after the press: the circle of `WarHammer_Spell_Shockwave` around the caster (3 m; `_L2` and the row `_L2` with Meteoric Crater: 4 m), row `Warhammer_Damage_Shockwave`; the stun `WarHammer_Shockwave_Stun` (2 s, ends at the next hit), the weaken `WarHammer_Shockwave_Weaken` with Frailty (10 percent, 10 s), and Aftershock. Cast time 0.8 s. |
| Path of Destiny | From 1 s after the press: `NumToSpawn` circles of the spell (3) of its `Radius` (4 m), one each `CastDistance` (3.8 m) in front of the caster in the direction of the press, `SpawnRate` (200 ms) apart; one phase (a creature takes one hit of the path). The spell and the row `_L2` with Seismic Waves, `_L3` with Seismic Waves and Stimulated Reduction; Seismic Waves: a stagger; Stimulated Reduction: each hit takes 5 percent of the cooldown of the data away (5 hits at most, not below the minimum end); Aftershock. Cast time 1 s. |
| Aftershock | Each hit of an ability of the war hammer (Wrecking Ball, Shockwave, Path of Destiny) with the node gives `WarHammer_Ult_SlowOnCC` (20 percent, 4 s). |
| The observers | Each session that observes the creature gets its status state (the icons) and its movement state (the action of the effect); only the session of the caster gets the cooldown state and the damage number (case AB9). |
| The cost of an area | A phase looks only at the entities of the position index near its center; the end of the effects looks only at the creatures with an effect (case AB10: 100 Shockwaves near 40 creatures in a world of 4,040 creatures). |

**The commands** (development routes; the admin interface has `reset_cooldowns`,
`assign_node`, and `set_ability_bar`): `tools/world cooldowns CHARACTER [reset]` (`GET
/v1/dev/abilities?character=ID`, `POST /v1/dev/abilities/reset`), `tools/world node CHARACTER
WEAPON NODE...` (`POST /v1/dev/progress/node`: one mastery point through the skill system, with
the rules of the tree), `tools/world bar CHARACTER WEAPON SLOT ABILITY` (`POST
/v1/dev/progress/bar`), `tools/world creature effect CREATURE STATUS [SECONDS]` (`POST
/v1/dev/creatures/effect`: an effect of the data, or `knockdown` or `stagger`). `tools/world
list` has the line `abilities`.

**Not known, not done** (stage 3.3a):

- The abilities of the other weapons (26 tables). Each one needs its live check of L1 and L3
  (which phases send a contact, their windows), its rule in `Abilities::plan` (its phases,
  rows, spells, and effects), and for a spell or a projectile its area rule (L7): the bow, the
  musket, the blunderbuss, the staffs, and the gauntlets send projectiles (the second list):
  the basic shots have their rule from stage 3.3b-S3 (4.27); the abilities of these weapons do not.
- An entity of a spell (the vortex of Gravity Well, the effects of Path of Destiny and
  Shockwave): the client shows only what it plays itself.
- The pull of Maelstrom and of the vortex (`ImpactDistanceY`): done in stage 3.3b-S4 (4.26).
- The effects on the caster and the passive nodes (stage 3.3b-S1 did Safety Measures, Greed,
  and Hammer Time: the caster effects of 4.24): Unyielding,
  Enduring Strike and Hardened Steel (grit), the other empower nodes, Critical Gains, Keen Edge,
  Critical Condition, Mauler's Resolve, Crowded Protection, Center of Attention, Outnumbered,
  Guarded Sprint, Concussive Impact, Prevailing Spirit, Epitome of Bonk, Exhaustive Attacks
  (no stamina of a creature), Death's Embrace, Greed, Mauler's Fury, Power Through Pain, and
  Absorb (no projectile of a creature is blocked). Critical hits.
- The counts of the activities of the ability state (slot 46, fields 3 and 4: `actionDataCount`, `actionDataAbilityIds`)
  and a refusal or a correction for a press that the backend refuses. Fields 1 and 2 (the hits of an ability): 4.33.
- The full status map of an observer (its wall clock) and the status state of the owner (the
  tray of a player): done in stage 3.3b-S1 (4.24), but the owner tray stays empty.

### 4.23 Itemization: the perks of an item, the gear values, the heart rune, the consumables, the cost of an ability (live proof of stage 1.3b; the rules are project rules with the values of the game data)

Stage 1.3b. Research: `~/Games/soulwarden/work/research/itemization.md`, and the results of the
stage in `~/Games/soulwarden/work/research/scratch/itemization-implement/` (`perk-roll/`,
`perk-swap/`, `cost-analysis/`). Live checks and the live proof:
`~/Games/soulwarden/work/evidence/itemization/implement/` (`live/LIVE-CHECKS.md`, `live/I*`).
Code: `gamedata/perk_data.h`, `world/item.h`, `world/inventory.h` (the perks of an instance),
`world/gear.h` (the gear values and the perks of a hit), `world/perk_roll.h` (the perk roll of a
loot item), `world/runes.h`, `world/consumables.h`, `world/abilities.h` (the cost, the mana, the
client cooldown), `world/combat.h` (the damage with the gear), `server/rep/states/` (the charge
state, the item map of the cooldown state, the bonus map of the attribute state, the mana state,
the parts of the paperdoll state). Tools: `tools/gamedata/perks.sh` (`perks.py`),
`tools/research/perk_index.py`. Test cases IZ1 to IZ11.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| A perk of an item on the wire is the index of the perk in the perk list of the client: entry 0 is "no perk", then the rows of the seven perk tables in the order ItemPerks_Gems, Infix, Deprecated, Artifacts, 2025, EquipmentSetBonuses, ItemPerks; in each table the order of the buckets of the hash map of the table (descending bucket of `crc * 0xDE5FB9D2630458E9`, low and high words added, and the bucket count a power of two). 1,822 entries for the supported build. | Live proof I1: the list of two client starts is equal in each entry to the list of the tool. | `0x448E310` (list), `0x448E4F0` (bucket roll) |
| The five perk places of an item record (bag and paperdoll) are five numbers in the size code (4.10); a gem is a perk of a place (PerkType Gem). The client computes the rarity from the count of the perks. | Live proof I2: the tooltips show the perks, the gem, the gear score, and the rarity; the equip keeps them (I3-01). | `0x5C481D0`, `0x5C528D0` (rarity) |
| The client shows and uses a different perk for some perks of an item: the perk swap table (ItemPerkSwapData). An ItemId row of the item wins; else the first ItemClass row, in the sequence of the table, whose classes are all classes of the item. A pass changes each perk one time; the client makes two or three passes (a chain of two steps). A perk whose new perk the item has already (not a gem, not a charm) goes out. Then the client removes a perk that the item cannot have (no class of its ItemClass, or a class of ExcludeItemClass, or deprecated). | Static proof (perk-swap/RESULT.md); live proof I2: the Soldier perk of a weapon shows as Acutely Empowered, of armor and jewelry as Wellness. | `0x44407E0` (pass), `0x4431E80` (loader), `0x5C0A440` (class test), `0x5CF4A40`, `0x5CF5750` |
| The potency of a perk: 1 plus, for each step of its ScalingPerGearScore, the slope times the part of the gear score range of the step that the gear score covers from 100. | Live proof I2: the numbers of the tooltips (Health 0.03 x 1.5715 = 4.7 percent, and three more). | `0x3692600` |
| The client shows the fixed perks of an item definition (Perk1..5) also when the instance does not have them (the heart rune, the opal jewelry). | Live proof I2. | — |
| The attribute state, group 0 field 1 (`attributeBonuses`): a map of the attribute type and the bonus. The client adds the bonus to the value of the character sheet. | Live proof I4: CON 7 to 55 with a bonus of 48. | — |
| The client computes the equip load, the average gear score, and the armor of the inventory screen from the paperdoll items itself; the load of each armor item counts in whole tenths. | Live proof I4 and I12: 23.25 + 15.5 shows as 3.8; the five armor items of the build (231.57) as "Medium 22.9"; the armor 1,564.9 and 1,173.7 of the backend rule. | — |
| The charge state, slot 48, type 5257 (`MB::ChargeComponentReplicatedState`): group 0 field 0, one byte, the percent of the heart rune charge. The HUD fills the icon of the key G from it. | Live proof I6 and I12. | `0x66F13D0`, `0x279E210` (reader), `0x683AB80` (apply) |
| The key G (`rune_ability`) is bit 450 of the action words of the input entry; the client empties its icon at once and shows no explosion of its own. | Live proof I6 and I12. | — |
| The quick slot keys 3 to 6 (`Use_Quickslot_1` to 4) are the bits 404, 117, 140, 513 for the paperdoll slots 14 to 17; no message. The client plays the drink itself, also in a cooldown of the server. It sends no bit for an item whose level requirement is above the level of the character. | Live proof I7 and I12. | — |
| ContainerComponentServerFacet_ServerUseItem (type 2917): the entry "Use" of the action menu of an item of the bag. Body: target u64, entity u64, bag slot u32 (20 bytes). | Live proof I7 (the message); I12 could not open the action menu with the harness. | `0x48B4E40`, `0x48E7D00` |
| The cooldown state (slot 45, type 2932), group 0 field 3 (`ccdmap`): a list in the full form of entries `key:u32 end:u64 a:u32 b:u32`, the key the CRC-32 of the lower-case item id; the client shows the countdown on each quick slot and bag item with the key. The key of a cooldown id shows nothing. `a` and `b` are 0. | Live proof I7 and I12 (the countdowns of the potion and of the potions of the same cooldown id). | — |
| The mana state (slot 47, type 1652), fields 2 and 3: the delay that is left before the regeneration, and the rate. The client adds the rate times the time to its mana (after the delay) and compares its value with the next state. | Static proof (cost-analysis/RESULT.md). Not proved live with a cost: the build has no ability with a cost. | `0x676B8E0` (step), `0x6949DE0` (spend) |
| The damage number (type 2071) has a list of entries; two entries (a converted part of a gem) show two numbers at the target. | Live proof I12 (148 Slash and 156 Corruption). | — |
| The paperdoll state of the owner in more than one part (4.11): in a first record each entry has the cause "load", and a later part with entries of the same list group took the items of that group of the earlier part out of their slots. | Live proof I12 (a second part with the heart rune only took the jewelry and the quick slot items out). | — |
| The cost of an ability: a cost id with no row in the mana table (ManaCosts_Player) costs nothing: the lookup returns false and writes no value; the pay paths then pay nothing, and the check IfManaCost is false. The mana cost is the row times the product of (1 + ManaCostMult) of each active ability whose ManaCostList has the id or "all", not below 0, times a character multiplier. The stamina cost is the row times the cost multiplier of the action plus the StaminaCostFlatMod of the first active ability that lists the id. The client pays no spell `ManaCost` itself. | Static proof (cost-analysis/RESULT.md). | `0x6801430`, `0x67EAED0`, `0x67EB010`, `0x2C34F60` |

**The data** (`perk-data.json`, `tools/gamedata/perks.sh`): the perk list in the order of the
client; each perk (type, condition, ItemClass, ExcludeItemClass, exclusive labels, scaling,
affixes, abilities, channel, deprecated); the cells of the affixes (AffixStatData) and of the
ability rows that the perks name; the perk buckets with their weights; the perk places of each
item (Perk1..5, PerkBucket1..5, PerkSlot1..5, ForceRarity, CanHavePerks); the consumables
(AddStatusEffects, DurationOverrides, CooldownId, CooldownDuration, SharedCooldownLocString,
RemoveStatusEffects, RemoveStatusEffectCategories, DisableUseInCombat; format 2: the RequiredLevel
`level`); the encumbrance row
Player and the load of each armor item; the exclusive labels; the swap table; the EquipAbility
rows of the heart runes; the perk generation data (the largest perk count of a tier for each
gear score, the largest channel, MinLevelRollPerks). `ability-data.json` (format 2) has the
cells of each status row and spell row, and the cost tables (4.22); format 3 (stage 3.3b-S1)
adds the status categories and the caster rows (4.24). `combat-tables.json` has
AttackRuneCharge and HitRuneCharge of each damage row; `creatures.json` the HitRuneCharge of
each contact; `progression-tables.json` the mana regeneration of the row Player of the base
vitals.

**The rules** (project rules; the values of the data where it has a value):

| Rule | What the backend does |
| --- | --- |
| The perks of an instance | Five places with the PerkID text (the database: the columns `perk1` to `perk5`, schema 7; the wire: the index of the perk data at each load). A give and a perk command check each perk (in the data, one place, one gem at most, a perk for the item class, not deprecated) and apply the swap table of the game; the stored perks are the perks that the client shows and uses, so that its passes change nothing. A stack takes only an item with the same perks. |
| The perk roll of a loot item | The procedure of the client (`world/perk_roll.h`; perk-roll/RESULT.md): no rolled perk below level 7; the fixed perks; the largest perk count of the tier at the gear score; one count roll from the last bucket; a bucket roll with the channel, label, class, and deprecated filters, a label roll by weight, and a weighted pick; one Inherent perk at most. No perk biasing. Each instance of a drop gets its own roll. |
| The gear values | The perks of the equipped items (armor, jewelry, the weapon in the hand for OnActive and OnUnsheathed, the heart rune) with the swap and the potency: MOD&lt;attribute&gt; (the attribute bonus, a whole number), MaxHealthMod and MaxHealth (a part of the health of the base, the level, and the constitution), BaseDamage (a part of the damage), DMG&lt;type&gt;, ABS&lt;type&gt;, PhysicalArmor and ElementalArmor, BaseDamageReduction, the regeneration of a status with TickRate; an ability row counts IsStackableMax times at most. Each effective perk of an item counts one time (finding F2). The statuses on the character (a food, a dust, the status of the heart rune, the status of an affix) count through the status system with the limits of their categories (stage 3.3b-S1, 4.24). |
| A gem conversion | DamagePercentage of an affix of the instance of the weapon of the hit (the slot of the hit or of the cast: finding F1): that part of the raw damage has the DamageType of the affix, with the defense of the target for that type; the damage number has two entries. |
| The perks of a hit | The ability rows with OnHit of the perks of the attacker, with the conditions that the backend evaluates (AttackType, DamageTableRow, RemoteDamageTableRow, TargetStatusEffectCategory, the health, stamina, and mana percents, grit, the vitals category, DontHaveStatusEffect, InSequence; a blank comparison is LessThanOrEqual: 4.24) and ActivationCooldown: BaseDamage, OtherApplyStatusEffect (a rend of the armor of the creature, a damage over time based on the damage of the hit, a chain of a spell), DmgPctToHealth (a heal), CooldownTimer with CDRImmediatelyOptions ActiveWeapon (a shorter cooldown of the bar of the weapon in the hand). A row with a different condition has no effect. The burst of Insatiable Gravity Well: the spell InsatiableGravityWell around the character (the client text of the perk). |
| The equip load | The load of the armor items in whole tenths; the class by the percents of the row Player (light below 26, medium below 46, heavy to 100); the damage part (+0.15, 0, -0.15) and the heal factor of the class. |
| The heart rune | The charge of the character (60 at most; 6 at most for one gain): AttackRuneCharge of an accepted hit, HitRuneCharge of a hit taken, SourceRuneChargeOnApply and OnTick of a status of a perk with a change of the health. The key at a full charge: the charge goes to 0, the character gets the first variant status of the action that a perk of the rune has (Dominator, Greater, Major; one status), and 3 s later one explosion: each creature in Radius 5 and Height 5 of the spell Rune_Detonate_AoE that the character sees, the row Rune_Detonate_AoE with the weapon of the rune. A death before the end: no explosion. Stage 3.3b-S1: the status is a status of the status system, its end status Rune_Detonate_CastAoE casts the explosion, and a death or a leave removes it (finding F4). |
| The consumables | A quick slot key or the message 2917 (its entity must be the entity of the session: finding F6) uses the item of the slot: a consumable of the data, not SiegeWarOnly, not above the level of the character (RequiredLevel: finding F5), not in a fight for DisableUseInCombat, not in its cooldown. One item goes in one transaction; the effects: one heal and one mana change for an effect with no duration, or an effect for its time (ticks with TickRate, StopOnHitCount); the cooldown of the item, shorter by the perks with OnUsedConsumable; the other items of the cooldown id get 80 percent of it (the client text of SharedCooldownLocString) or all of it; a shared end does not make the end of an item earlier (finding F7); a change of the bag or the quick slots gives the item map again (finding F8). The effects are statuses of the status system (4.24); the cooldowns are values of the character. |
| The mana | One path for each change (`Abilities::change_mana`): a cost, a potion. A spend starts the regeneration after ManaRegenDelay (0), or ManaRegenDelayOnEmpty (2 s) at 0; the rate is ManaBaseRegenRate (2 in each second); the mana state has the delay and the rate while the mana is not full. |
| The owner and the observers | The charge state, the item map, the attribute bonus, and the mana state go to the owner only; each observer of a creature gets its health, its status icons (a rend), and its death (case IZ10). |
| The cost of the rules | A hit looks at the perks of one character and at the creatures near its target (a chain); the status tick looks only at its due list (4.24); an explosion looks at the position index near one character (case IZ11: 50 characters, 2,000 hits, 4,400 creatures). |

**The commands** (development routes; the admin interface has `give_item` with `perks`,
`set_item_perks`, `use_item`, `set_rune_charge`, `activate_rune`; the tool window gives an item
with a gear score and perks): `tools/world give CHARACTER ITEM [QUANTITY] [--gs N] [--perks
P1,...]`, `tools/world perks CHARACTER SLOT P1,... [--paperdoll]` (`POST /v1/dev/items/perks`),
`tools/world gear CHARACTER` (`GET /v1/dev/gear`), `tools/world use CHARACTER SLOT
[--paperdoll]` (`POST /v1/dev/items/use`), `tools/world rune CHARACTER charge PERCENT|activate`
(`POST /v1/dev/rune/charge`, `/v1/dev/rune/activate`), `tools/world perkroll ITEM [--gs N]
[--level N] [--count N]` (`GET /v1/dev/perks/roll`).

**Not known, not done** (stage 1.3b):

- The status state of a player: done in stage 3.3b-S1 (4.24); the speed of a status is not
  applied by the client.
- The action menu of an item in the bag with the harness (I12): the message 2917 is proved
  (I7), and the world path of the use is the path of a quick slot key.
- A food with a level requirement above 60 (FoodCONT5: level 65): the client sends no use, and
  the server refuses a use (finding F5).
- The perks with a system that the backend does not have: grit (Thwarting Strikes), stamina
  (Abyssal Gambit, Gambled Energy, Stamina Recovery), critical hits (Resilient on armor),
  dodge (Shirking Fortification), crowd control on a player (Freedom), the removal of a
  debuff (Purifying Toast), and a player target (the PvP ring perk). The speed of the status of
  the heart rune: the server applies it, the client does not (4.24).
- The other heart runes (nine abilities), the crafting, the repair, the salvage, the socket
  action, and the perk biasing of a roll.
- A paperdoll group of lists larger than one part: one part of its own, in chunks of the
  carrier (finding F12, case RI12); not checked live.

### 4.24 The status effects of a player: the status state, the wall clock pair, the values of a status (live proof of stage 3.3b-S1; the rules are project rules with the values of the game data)

Stage 3.3b-S1. Research: `~/Games/soulwarden/work/research/abilities-effects.md` (sections 4, 5,
7) and `abilities-shared.md` (K15, the comparator). Live checks and the live proof:
`~/Games/soulwarden/work/evidence/abilities-status/implement/live/` (`LIVE-CHECKS.md`: L1, L2,
runs s1-live and s1-live2). Code: `world/statuses.h` (the one owner of the statuses of a
character), `server/rep/states/status_state.cpp` (`encode_player_status_state`),
`server/rep/states/player_state.cpp` (the clock pair), `world/gear.h` (the values, the caster
effects, the comparator), `world/position_report.h` (the speed in the position check),
`gamedata/ability_data.h` (format 3). Document: [Status effects](systems/status-effects.md). Test
cases ST1 to ST11, RI1 to RI15.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The status state of a player: component `StatusEffects`, slot 44, type 4236 (the type of a creature, 4.22). Group 1 is the owner: field 0 the full map (`statusIndex:u16 wallEnd:u64 potency:f16 stacks:u8 flags:u8`, 14 bytes), field 1 the lightweight map (`statusIndex:u16 wallEnd:u64`), field 2 the tray (`trayKey:u32 wallEnd:u64 stacks:u8 potency:f32`, 17 bytes; the key the CRC-32 of the lower-case status id). Group 3 is an observer: field 0 the full map, field 1 the compact map (4.22). Each list in the full form `00 01 sequence count entries`: a larger sequence replaces the list, a smaller one changes nothing, an empty list removes each entry. | Live proof L1, L2, s1-live2. | `0x4013900` (constructor), `0x4038E80` (the reconcile of the owner map), `0x4039600` (removal), `0x4028F70` (apply), `0x403E430` (HUD refresh) |
| A full map entry of the owner alone gives the HUD icon of a row with `ShowInUITray`, its countdown, its stack count (×2), its visual effect on the character, and its armor in the character sheet (after a new open of the inventory). A tray entry of the same status gives a second icon (Rune_Detonate_Major, a food); the dust merged the two. The backend sends an empty tray. | Live proof L1 (L1-09 to L1-14), L2 (L2-11 to L2-14), s1-live (two icons), s1-live2 (one icon). | — |
| The attributes of a food (MODConstitution) in the character sheet come from the attribute state (4.23, the bonus map), not from the status map: CON stays 7 with the food in the map alone. | Live proof L1-11; s1-live2: CON 7 to 37 with the bonus of the server. | — |
| The end of a map or tray entry is a time of the synced wall clock of the client in ns. The Player state (slot 9, type 3935) of the owner, group 1, field 6 `sessionStartWallClockTimePoint` (Unix time in ns) and field 7 `sessionStartTimePoint` (the server time in ns), from one sample, give it: synced wall = field 6 + (server now − field 7). With no pair the synced wall clock is near 0 (a tray end in Unix time showed "20737d"). The HUD computes the time left of an entry when it applies it: the pair must come before the first status (the first record has slot 9 before slot 44). Bytes of the two fields: `02 C0 wall:u64 01 server:u64`. | Live proof L1-01 to L1-04; static proof (getter). | `0x6830A40` (getter) |
| A countdown at 0 does not remove an icon (the HUD shows "–"): the server sends the list again at the end of a status. | Live proof L1-05; s1-live2 (potion, rune, observer expiry). | — |
| An observer of a player: the compact map gives the icon at the name plate of a remote player (as for a creature); the full map alone shows no icon. A status that is not public does not show. | Live proof L2-01 to L2-03; s1-live2 (S1-11, S1-observer-icon-expiry.mp4). | `0x4044030` (compact reader) |
| The client does not apply the MoveSpeedMod or the SprintSpeedMod of a status of the full map of the owner to its run: a walk of 2 s is 11.2 m with no status, with the haste of the rune (+0.2), and with UncategorizedHaste_40pct (+0.4); a sprint 12.6 m; a slow of −0.6 changes nothing. The code that should read it: the reconcile and the removal call the stat refresh `0x4044B00` for an effect with a speed value; `0x40242C0` sums the MoveSpeedMod (+0x6C of the status definition) and the SprintSpeedMod (+0x70) of the applied effects with the limits of the categories; the activity ApplyMoveSpeedMultiplier of the run actions (install `0x2A91270`, tick `0x2A91860`) reads the stat. The flag `0x9EFCE74` that `0x40242C0` reads is the setting `javelin.disable-armor-status-effect-category-limits`. The speed comes from the speed table (the next row; stage 3.3b-S2a). | Live proof L1 (walk-*), s1-live2 (S1-rune-walk-*.mp4, S1-haste40-*.mp4). | `0x4044B00`, `0x40242C0`, `0x2A91270`, `0x2A91860` |
| The speed table of the player (stage 3.3b-S2a, criterion S2a-I10; research `status-speed.md`): component StatMultiplierTable, slot 43, type 1525. Group 1 (the owner), field 0: a full table `deltaCount:size = 0, hasSequence:u8 = 1, sequence:size64, count:size, entries`, entry `key:u8 value:i32 (big-endian, the multiplier times 10000) m_syncVitals:u8`. Keys: 0x34 MoveSpeedMod, 0x35 SprintSpeedMod (the name registry). An absent key is 1.0; the empty table and the values 10000 give the baseline again. The movement reads the table each tick of a run action, not the status map. In the live walks the key 0x35 scales the run and the sprint (1.4: the run 11.0 m to 15.8 m in 2 s, the sprint 12.6 m to 16.5 m), and the key 0x34 had no effect. With the encoder: a haste, its removal, its expiry in a walk, a slow (0.4: 5.3 m/s to 2.18 m/s), the real heart rune (1.2), and a first record after a new entry give the expected speed; the table of a different character does not go to the session. The client bounds a value with the Min and Max of StatMultiplierData (0 to 1.5 for the two speeds). | Live proof S2a-I10 (raw walks L1, L2; encoder walks; the bytes of the table in the first record of a new session, and in no later datagram). Static proof (research). | `0x3FE27C0`, `0x3FEA880` (getter: an absent key is 1.0), `0x3FEA370` (the bounds), `0x3FF0810` (the name registry; registration calls `0x3FF0F74`, `0x3FF0F89`), `0x2A91860` (the tick of the run action), `0x15CE270` (the client sorts the fragments of a record by slot) |
| In a live check with raw fragments, a full map entry of a status of the heart rune did not go away after its omission (also after its end): the removal `0x4039600` keeps an instance with a mark of 1 s or less at component +0x148 (a local prediction of an action of the client, Rune_Detonate). In the real flow of the key G the icon goes away at its end. | Live L2 (L2-04, L2-11 to L2-14); s1-live2 (S1-09). | `0x4039600` |
| The parser of a comparison text of AbilityData (TargetStatusEffectComparison, MyComparisonType, MyStaminaComparisonType, MyManaComparisonType, TargetComparisonType) gives enum 4 (LessThanOrEqual) for a text that it does not know, also for an empty text. | Static proof (research abilities-shared.md K15). | `0x43B6390` |
| StackMax below 1 is 1 in the loader of the status rows. | Static proof. | `0x44B65F5` (in the loader `0x44B4430`) |

**The data** (`ability-data.json` format 3, `tools/gamedata/abilities.sh`): the status rows and
the spell rows (format 2, 4.22), and two more lists: `statusCategories` (each row of
StatusEffectCategoryData with its ValueLimits: Haste MoveSpeedMod and SprintSpeedMod 0.4, Slow
−0.5, ArmorFortify PhysicalArmor and ElementalArmor 2.0, Fortify ABS 0.5, and 53 more) and
`abilityRows` (the rows of the ability tables of the players with a SelfApplyStatusEffect, with
their cells; each status must be a status row: 497 rows). The backend reads formats 1, 2, and 3;
a file with status effects and no status row is refused (finding F10). `perk-data.json` format 2:
the RequiredLevel of each consumable (`level`), the value columns of the affixes and of the
ability rows are required, a conflict of two rows of one id stops the tool, and a bucket stops
at the first empty perk (findings F5, F14 to F16; [Game data](development/game-data.md)).

**The rules** (project rules; the values of the data where it has a value; the source of each
rule is in [Status effects](systems/status-effects.md)):

| Rule | What the backend does |
| --- | --- |
| The one owner | `world::Statuses`: the statuses of a character with the character id as their key; a food, a potion, a dust, the status of a heart rune, the status of an affix of an equipped item, a caster effect of an ability row, and an end status come through it. Only it changes them. |
| The application | One status of a row for each character; a new application: one more stack to StackMax, the end by RefreshDuration and DurationMax, the larger potency; DontReplaceStack at StackMax changes nothing; ApplicationCooldown; a dead character takes no status (but AllowAddIfDead). The duration of the source, else BaseDuration, not more than DurationMax. A row with no duration is one application (a heal, a mana change, its CastSpell). |
| The end | At its end on the world clock (the due list), at a death (RemoveOnDeath), after StopOnHitCount hits, by a newer status of the source (RemoveStatusEffects, RemoveStatusEffectCategories), at the leave of the world (the gear, the rune activation), by a command. The OnEndStatusEffect comes, but not for a removal of a row with DontApplyOnEndEffectOnRemove. |
| The ticks | TickRate with Health, HealthModifierPercent, Mana, ManaModifierPercent: a heal (times the heal factor of the equip load) and a mana change at each tick, through the one heal path and the one mana path. |
| The values | MOD&lt;attribute&gt;, MaxHealthMod, PhysicalArmor, ElementalArmor, BaseDamageReduction, DMG&lt;type&gt;, ABS&lt;type&gt;, MoveSpeedMod, SprintSpeedMod, times the potency and the stacks, with the ValueLimits of the categories; the gear summary has them one time. |
| The heart rune | The status of an activation (source "rune"): its end status Rune_Detonate_CastAoE casts Rune_Detonate_AoE, the explosion of the rune system. A death or a leave removes it with no explosion (finding F4). |
| The caster effects | A hit evaluates the ability rows of the perks and of the learned nodes of the ability table of its weapon: OnHit, SelfApplyStatusEffect, the conditions of a perk row and InSequence, ActivationCooldown. A blank comparison is LessThanOrEqual. |
| The speed | From stage 3.3b-S2a: the gear refresh computes the move and sprint multipliers (1 plus the speed parts, in the limits of StatMultiplierData), the owner gets the speed table (slot 43; both at 1: the empty table, and no fragment in a first record). The position check accepts a window speed times the larger multiplier of the last table that the session sent (not less than 1; the value before a change counts for 1 s more). A haste that the client did not get does not widen the check (finding F1 of the review of stage 3.3b-S1). From stage 3.3b-S4: each older value counts for 1 s after its own change, not longer (a later change does not keep it), and a file with no limits gives the limits of the client, 0 to 1.5 (findings F1 and F2 of the review of stage 3.3b-S2a; cases RG1, RG2). |
| The views | The owner: the full map of each status that is not of the gear, an empty lightweight map, an empty tray; the Player state of the owner has the clock pair. An observer: the full map and the compact map of the public statuses (ShowInNameplates, AlwaysReplicate, ForceReplicateToRemotes; the compact map ShowInNameplates only). A first record with no status has no status fragment. |
| The restart | The statuses are not in the database: a restart of the backend ends them (rule of stage 1.3b; a decision of the user is open). |
| The cost | The expiry and the ticks look only at the due list (case ST11: a time limit). |

**The commands** (development routes; the admin interface has `apply_status` and
`remove_status`): `tools/world status CHARACTER` (`GET /v1/dev/status?character=ID`),
`tools/world status CHARACTER STATUS [SECONDS [COUNT]]` (`POST /v1/dev/status`), `tools/world
status CHARACTER remove STATUS|all` (`POST /v1/dev/status/remove`).

**Not known, not done** (stage 3.3b-S1):

- The speed of a status in the client: closed in stage 3.3b-S2a (the speed table). Not proved: what the key 0x34
  (MoveSpeedMod) changes in the client, and the observer map of the speed table (group 2).
- The tray of the owner: the backend sends it empty (two icons with the map).
- The other CastSpell of a status row (only the rune explosion), the effects of a status on a
  creature from a player status (OtherApplyStatusEffect stays as in 4.23), the rows with
  OnHitTaken, NumAroundMe, a grit, a stamina, or a critical hit condition.
- The statuses after a restart of the backend.

### 4.25 Spell entities: the vortex of Gravity Well, its record, its delayed events, its stop (live proof of stage 3.3b-S2a; the rules are project rules with the values of the game data)

Stage 3.3b-S2a. Research: `~/Games/soulwarden/work/research/abilities-shared.md` (Q2, the spell entity) and
`status-speed.md` (the speed table, 4.24). Live checks and the live proof:
`~/Games/soulwarden/work/evidence/abilities-spell/implement/live/LIVE-CHECKS.md` (L4 with the probe entity of run
`l4-spell`, S2a-I10, and the proof videos of run `s2a-live` in `../proof/`). Code: `world/spells.h` (the spell system),
`world/entity.h` (`SpellBody`), `server/rep/states/spell_state.cpp` (the Spell state and the delayed event states),
`server/rep/states/metadata_state.cpp` (the slice of a spell), `gamedata/spell_data.h` (`spell-data.json`),
`tools/gamedata/spells.py`. Document: [Spell entities](systems/spells.md). Test cases SP1 to SP9.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| A spell entity is an entity group of its own with the actor id of the id allocator. Its record has the metadata state (slot 0: the asset of the slice `greataxe_gravitywell_vortex.dynamicslice`, `D60BAED0-C08F-514F-9ECD-9BB5E20F7E9D`, sub id 2; the upgrade slice `CA631943-4786-504E-A5AA-77D07DD776B1`), the Spell state, and the delayed event states. The smallest record that shows the vortex: the metadata and the Spell state with the fields 0 and 1. No Ownership state (slot 2) and no TemporaryAffiliation state (slot 3) are necessary for the picture. | Live proof L4 (the probe, `L4-min-*`). | — |
| The Spell state (`MB::SpellComponentReplicatedState`, type 2912) at the slot of the Spell component of the slice (1 for the vortex). One group, 11 fields: 0 the key of the spell row (u32: the CRC-32 of the lower-case SpellID, `B5715A91` for `Spell_GreatAxe_GravityWell_Vortex`), 1 the start position (x f32, y f32, z u16 over −100 to 2000), 2 the start rotation (a compressed quaternion: `07` is the identity), 3 and 4 the child position and rotation (the same forms), 5 and 6 the previous attachment and the attachment (actor raw16, group u64; none: the zero actor and the group `00000000FFFFFFFF`), 7 the caster (its actor and group), 8 the spawn time (u64), 9 the spawn count (u32), 10 the radius scale (f32). With no start position nothing shows near the character. The spawn time changes nothing that shows: the backend sends the server time in microseconds (the unit of the time anchor of the movement state; a decision of an agent, not proved). | Live proof L4; static proof (research Q2). | `0x67185C0` (constructor), `0x683DE00` (apply: the row from the key), `0x5CEA190` (position reader), `0x5CEA140` and `0x87B250` (rotation), `0x6718390` (an empty reference) |
| The field masks of a state are chained: each mask byte (bit 7: one more mask byte) comes before the values of its 7 fields (`01 ff` + the fields 0 to 6 + `0f` + the fields 7 to 10). A record with both mask bytes first gives "Failure during read of ReplicatedStateBundle". | Live proof L4 (the research fixture failed; the form of the backend writer reads). | — |
| The delayed event state (`MB::DelayedEventComponentReplicatedState`, type 3139), one for each DelayedEventComponent of the slice (slots 4 to 9 for the vortex): `01 03 remaining:f16 completed:u8` (field 0 the time that is left in seconds, field 1 the completed mark). The client runs the delays of the slice from the init (`m_startDelayOnInit`) when no state comes: a late observer then sees the full timeline from its record. A state replaces the delay: a late observer with the remaining times sees the correct age (the vortex of age 2.5 s for 0.5 s, the burst at +0.8 s). A completed state executes its event at once, also with `m_executeEventOnReplicate` false: correct for a deactivation, wrong for an activation (the burst plays again). An activation with a remaining time longer than the rest of the life of the entity shows nothing before the stop. | Live proof L4 (`L4-delays-8s`, `L4-late-*`); s2a-live P3 (a late observer at age 1738 ms: the vortex to about 3.4 s, no restart). | `0x3066370` (reader); not traced: `0x6877580`, `0x30705A0`, `0x3077A20`, `0x689D450` |
| The events of the vortex slice (game data): slot 4 at 3.0 s deactivate (VFX_Foe), 5 at 3.0 s deactivate (VFX_Vortex), 6 at 3.1 s deactivate (the detection volume), 7 at 3.0 s activate (the burst entity, offense GreatAxe_GravityWell_Burst), 8 at 1.0 s deactivate (the root), 9 at 3.5 s trigger (the entity with the DestroyGDEComponent). The event types: 0 trigger, 2 activate, 3 deactivate. The upgrade slice has the same times with a different sequence of slots. The interest radius of the slice (`m_sliceSpawnRadius`) is 50 m. | Game data (`spell-data.json`); live proof L4 (the burst at 3 s, the end at 3.5 s). | — |
| The stop of the entity in the control block of a bundle removes the vortex at once, with no burst later and nothing that stays. The destroy event of the slice (3.5 s) does not remove the entity group in the client: the server must send the stop. A stop after the end gives no line in the game log. | Live proof L4 (`L4-cancel-*`); s2a-live P2 (the leave of the caster at age 1151 ms: the vortex goes away, no burst at 3 s). | — |
| The vortex of the server: the record at the landing of the projectile, the vortex picture, the burst 3.0 s later with the damage numbers of the caster (605.1 on each of three wolves), the end at 3.5 s; a second session that comes later sees the correct age; a session that leaves (80.6 m) and comes back gets the same entity with a new interest number, and one burst. | Live proof s2a-live (P1, P3, P4); test cases SP2 to SP6. | — |

**The data** (`spell-data.json` format 1, `tools/gamedata/spells.sh`): for each spell row with a SpellPrefabPath, the
slice (`slices/<path>.dynamicslice`): its asset and sub id, its root entity, its interest radius, the slot of its
Spell component, the slots and the types of its replicated components (Spell 2912, Ownership 3217,
TemporaryAffiliation 3563, DelayedEvent 3139, DetectionVolumeEvent), and its delayed events (slot, delay, type, the
target entity, the MomentaryOffense row of the target, and a mark for a target with a DestroyGDEComponent). 1,565
spells and 934 slices; 16 spell rows have no slice file and 143 slices have no Spell component (left out, with their
counts). With no file the backend starts, and a spell has its hits and no record. From stage 3.3b-S4 the loader
refuses a slot of type 3139 with no event row, and the tool stops for a delay that is not a finite 32-bit float
(findings F3 and F5 of the review of stage 3.3b-S2a; cases RG3, RG5).

**The rules** (project rules; the values of the data where it has a value; the source of each rule is in
[Spell entities](systems/spells.md)):

| Rule | What the backend does |
| --- | --- |
| The one owner | `world::Spells` makes, changes, and removes the spell entities (the kind `spell`): the id from the allocator, the caster, the place, the row, the start and the end on the world clock. |
| The times | The burst at the activation of the target with the damage row of the burst (3.0 s), the end at the trigger of the target with the DestroyGDEComponent (3.5 s); with no data the times of the ability rule. A delay of 0 or less is the start; a positive delay is 1 ms at least. |
| The hits | The phases of the cast (the vortex at the start, the burst) run at the place of the spell, one time each, while the spell is in the world. The area path of stage 3.3a does not run them. |
| The cancel | The leave of the caster ends its spells (a rule of the brief). The death of the caster ends a spell whose row has EndOnCasterDeath (game data; the vortex rows do not have it: the vortex stays and bursts). A command ends the spells of a character. |
| The record | The metadata, the Spell state (each field), and one delayed event state for each delayed event with the time that is left; a passed deactivation or trigger is completed, a passed activation waits until 1 s after the end. Only the observer view (the caster too). |
| The interest | To enter: the radius of the slice (50 m), not more than the distance of a character; to leave: plus 20 m. No slice data: no record. |
| The cost | A due list of the phases and the ends; the interest looks only near each character (case SP7: a time limit). |

**The commands** (development routes; the admin interface has `cast_ability` and `end_spells`): `tools/world cast
CHARACTER PLACE [--yaw DEGREES]` (`POST /v1/dev/abilities/cast`), `tools/world spells` (the member `spells` of `GET
/v1/dev/world`), `tools/world spells end CHARACTER` (`POST /v1/dev/spells/end`), `tools/world probe ...` (`POST
/v1/dev/world/probe`, `/v1/dev/world/probe/remove`; only with `--dev-commands`).

**Not known, not done** (stage 3.3b-S2a):

- The Ownership and the TemporaryAffiliation states of a spell (their values are not known; not sent).
- The unit of the spawn time (field 8).
- The Pylon, the trap, the Beacon: done in stage 3.3b-S2b (4.28). The projectiles: stage 3.3b-S3 (4.27). The pull of the
  vortex: done in stage 3.3b-S4 (4.26).

### 4.26 Forced movement: the pull and the push of a hit, the dash of the caster, the counter of Riposte (live proof of stage 3.3b-S4; the rules are project rules with the values of the game data)

Stage 3.3b-S4. Research: `~/Games/soulwarden/work/research/abilities-shared.md` (Q4 and the part "3.3b-S4"). Live checks
and the live proof: `~/Games/soulwarden/work/evidence/abilities-motion/implement/live/LIVE-CHECKS.md` (L7-A to L7-D,
L8-A, L8-B; runs `l7-motion` and `l8-riposte` with the backend of stage 3.3b-S2a) and the proof videos of run
`s4-proof` in `../proof/`. Code: `world/forced_motion.h` (the forced movement of the creature system),
`Creatures::impact`, `Combat::strike` (the parameter `source`), `Navigation::slide`, `world/abilities.h`
(`CasterMotion`, `Abilities::motion_allowance`, `Abilities::countered`), `world/position_report.h`
(`MotionAllowance`), `server/rep/states/movement_state.cpp` (`encode_observed_motion_state`),
`gamedata/combat_tables.h` (`DamageImpact`). Document: [Forced movement](systems/forced-movement.md). Test cases FM1
to FM8, CM1, CM2, RC1, RC2.

**The wire contracts and the client rules**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The client does not move a creature of the server by the impact of a hit. It plays the reaction of the creature (the action `Reaction`, 4.14): the body bends in the animation, and the place of the entity (the name plate) stays. The server must move the creature with the movement state of the creature (type 11, 4.14), with the action of the reaction. With the movement of the server the client shows the slide of the creature. | Live proof L7-A (backend with no movement: the wolves stay); proof P1 to P4 (the backend moves them). | — |
| The pull and the push of the client (`ReactionMotion`, the activity in the action `Reaction` of the creature action lists, `Default Scale 1.0`, `PromotedElite PushBackScale 0.15`; `Pushable`: `UsePushNoDecay`, 0.4 s). The loader of the damage rows reads ImpactDistanceX, Y, Z and ImpactDecayRate. The reaction context takes the "_AI" value of a row for an AI target when the row has one (FLT_MIN is "no value"). The displacement D = (dir * Y + side * X + up * Z) * Scale, dir the damage direction in the plane, side = dir x up; PushBackScale when D goes away from the source. Decay rate above 0 (mode 0): the travel at the time t of the action is \|D\| * (1 - r^(30 t)) to the end of the fragment; r = 1 or r near 0: all of D at once. Decay rate 0 or less (mode 1): to the point of the damage (away from it for a positive Y) at \|Y\| m/s, to 0.1 m from the point. The default direction is north (0, 1, 0). | Static proof (client code and the action lists of the creatures). | `0x677CEC0` (`CacheDamageDataTable`), `0x683D910` (apply of ReactionTracking, type 1927), `0x48A0690` (`ReactionMotionEnter`), `0x48A0D30` (`ReactionMotion::OnUpdate`), `0x488B8F0` (mode 1) |
| The client moves its own character in a dash with the root motion of the action (Charge: `AnimationDrivenMotion` of `GreatAxe_Engage`, MotionMultiplier 1.0, 1.3 with the conditionals `Attack_Translation*`): 13.77 m in 1.0 s with no creature in front, 14.73 m in 1.6 s with the swing, 6.68 m to a wolf 8 m in front; a step of 0.602 m at most in 1/30 s (18.07 m/s). It gets no position from the server. The position check of a run (8 m/s + 2 m in 1 s, 20 m/s for a step) refused 7 reports of one dash. | Live proof L7-B, L7-C; proof P5a (no refused report in the envelope). | — |
| The action layer 0 of the own character (memory read of the predicted state of the movement facet: 16 bytes for each layer, state, start count, sequence): at rest 30/N/1080 with the great axe, 30/N/4066 with the rapier; Charge 7/N/59 (the dash, 0.7 s), then 7/N/2312 (the swing); the Riposte loop 7/N/3324, then 7/N/21. An observer gets these numbers in the fields 13 to 15 of the movement state (4.9). With 7/59 and 7/2312, the rotation of the dash, and the places of the dash, the remote character of an observer runs in the direction of the dash. | Live proof L7-D, L8-A; proof P5b. | `0x6769980` (the player function) |
| The numbers of the layers are not indexes of the fragment list of the player (1037 fragments; Ability_GreatAxe_Engage 447, Ability_Rapier_Riposte_Out 455) and not indexes of its action table (530 actions; 451 Ability_Rapier_Riposte_Out). Their table is not known. | Static proof (L8-B). | — |
| An action with the mark ServerStarted (+0x17E) and without the mark +0x182 does not start by its condition (Ability_Rapier_Riposte_Out: ServerStarted, the activation `Not True`). Only the action layers of a movement state of the server start it. | Static proof; live L8-A (the client plays the loop, then its idle pose). | `0x2A66E50` |

**The data** (`combat-tables.json`, `tools/gamedata/combat.sh`): the member `impacts`, one entry for each damage row
with an impact distance (234 rows): `[row, X, Y, Z, decay, X_AI, Y_AI, Z_AI, decay_AI, UseAttackerPosForReaction]`.
The values of the abilities of this build: Maelstrom Y −1.5, decay 0.8 (`_Reach`: −2.0), `_NoReprieve` none; the
vortex pull `GreatAxe_GravityWell_Vortex` Y −7, decay 0 (the child entity VortexPull of the slice, sphere 4 m); the
burst Y +0.5, decay 0.3; Path of Destiny Y +2, decay 0.9, from the attacker; Shockwave +0.25, 0.8; Charge
`GreatAxe_Engage1` +0.25, 0.85; the swing of Gravity Well +0.5, 0.85; `1H_Rapier_Riposte_Stun` +1.5, 0.85; Wrecking Ball
none. A file from before has no member: a hit then moves no creature.

**The rules** (project rules; the values of the data where it has a value; the source of each rule is in
[Forced movement](systems/forced-movement.md)):

| Rule | What the backend does |
| --- | --- |
| The one owner | `world::ForcedMotion`, a part of the creature system, moves a creature that a hit moves. `Combat::strike` gives it the impact of the row of an accepted hit on a creature that lives after it. |
| The values | The "_AI" distances when one is not 0, the "_AI" decay when it is above 0; else the normal values. |
| The direction | From the source (the attacker, the center of an area or a spell; the attacker for UseAttackerPosForReaction) to the creature, in the plane; D = dir * Y + side * X; north for a creature at the source. Z is not used. |
| The travel | Decay above 0: \|D\| * (1 − decay^(30 t)); it ends when less than 2 cm is left (user test 2). Decay 0 or less: \|Y\| m/s to the source point (Y < 0), away from it (Y > 0); a pull stops 0.1 m from the point. At most the length of the reaction that the hit started (user test 2: the length of its fragment in the slice; 0.4 s, the action Pushable, for a row with NoReaction; a hit with no reaction moves nothing; 1.5 s with no length and for a pull that is no hit). The mode comes only from the decay (client code 0x48A0690). |
| The steps | A new place at most each 50 ms of the world clock, on the line of the navigation (`Navigation::slide`): a wall, a drop, or a cell with no floor stops the movement at the last place (proof P4: 0.85 m of 2 m at a pillar). |
| No movement | A creature on its way back (immune), a dead creature; a death ends the movement; a new impact replaces it. A root or a stun does not stop it. The behavior does not move the creature and starts no attack while it moves; after it, the creature goes on with its path (user test 2). |
| The dash of the caster | No second displacement of the server. The position check accepts the dash in the envelope of the accepted cast: 14.73 m × 1.3 more in the window, a step of 18.07 m/s × 1.3, from the press for 1.6 s and 1 s after. A refused press gives no envelope. |
| The observers of a dash | The state `motion` (the observer view of the movement state): the place, the rotation of the dash, the actions 7/59, 7/2312, then 30/1080 at rest; at each accepted report, at most each 50 ms. The owner gets nothing. A character with no client gets the dash from the server (13.77 m in 1 s, to 1 m in front of a creature). |
| The counter | The window `Rapier_Riposte_Timer` at an accepted press. A creature hit in the window: the window ends, and at 0.433 s the row `1H_Rapier_Riposte_Stun` hits the attacker one time (in the reach of its hit + 0.3 m, in sight), with the stun and the push of the row. No window, no counter. |
| The cost | The step looks only at the creatures that move now (case FM8: 200 movements in 4,200 creatures). |

**The commands** (development routes): `tools/world list` (the line `force` and the counters of the line
`abilities`), `tools/world creatures` (the mark `MOVED BY A HIT (ROW)`), `tools/world cast CHARACTER PLACE [--yaw
DEGREES]` (a remote test character: the dash of the server).

**Not known, not done** (stage 3.3b-S4):

- The start of the client action Ability_Rapier_Riposte_Out: the numbers of its layer are not known (L8-B). The owner
  sees its loop and its idle pose; the hit, the stun, and the push of the counter show.
- The PushBackScale of a promoted elite: done in stage U3 (the scale of the entry of ReactionMotion of the action of the hit: Default,
  PromotedElite for a kind with the status AI_Elite_Unbreakable_Grit_Buff in its buff buckets, ReactionLimiter_Active for the additive
  reaction in the limit; client code 0x48A0690 applies PushBackScale only when D goes away from the source). Live (run `u3-after6`): a
  heavy arrow pushes the promoted Wolf_Alpha_Named_08QP05 0.11 m (0.15 of 0.73 m) and the Solo+ Wolf_Alpha_16 0.73 m; a light arrow
  plays the additive reaction and pushes them 0 m and 0.35 m (before the stage: 1.00 m each for the heavy arrow, nothing for the
  light one). The push of an additive reaction does not hold the run of the creature (live check LC-Q5-2; [Forced
  movement](systems/forced-movement.md)). (The action `Pushable` is done from user test 2.)
- The part Z of an impact, and the part X in mode 1. No row of the abilities of this stage has them.
- The length of the fragment of a reaction: done from user test 2 (the reaction lengths of the creature data).
- The pull or the push of a character (a hit with an impact on a player).

### 4.27 Projectiles: the shot item of a client, the flight and the hit of the server (live proof of stage 3.3b-S3; the rules are project rules with the values of the game data)

Stage 3.3b-S3. Research: `~/Games/soulwarden/work/research/abilities-shared.md` (Q3, the part "3.3b-S3", L5),
`abilities-ranged.md` (Q2, Q3, L1, L2), `abilities-magic.md` (Q2). Live checks and the live proof:
`~/Games/soulwarden/work/evidence/abilities-projectiles/implement/live/LIVE-CHECKS.md` (runs `l5-shots` with the
backend of stage 3.3b-S4, `s3-proof` and `s3-staff` with the backend of this stage). Code: `world/projectiles.h`
(`world::Projectiles`, `capsule_of`, `sweep`), `gamedata/projectile_data.h`, `gamedata/creature_data.h`
(`CreatureCapsule`), `server/rep/client_input.h` (`InputShot`), `server/rep/rep_session.cpp` (`shoot`),
`Navigation::line_stop`. Document: [Projectiles](systems/projectiles.md). Test cases PJ1 to PJ11.

**The wire contracts and the client rules**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| A shot of a basic attack of a bow, a musket, a staff, or a gauntlet is one item of the second list of the input entry (192 bytes, 4.7). The bow sends it in the entry with the release bit (`attack_primary_release` 567): 1 to 9 steps after the press for a light shot, 16 steps or more for a charged one; the hip shot 10 steps after the release. The fire staff sends it 5 or 6 steps after the press, also while the button stays down. | Live L2, L5 (`live/L2-H-shot-inputs.txt`, `L5-B-firestaff-shot-inputs.txt`). | `0x256C980` (the ranged clip), `0x679DBB0` (the magic producer), writer `0x2A284D0` |
| The values of an item: the origin (wire 0, 1.0 m from the character and 1.46 m above its feet), a unit direction (12), the charge (172: 0 for a light shot, 100 for the charged arrow), the ammunition key (176: the CRC-32 of `defaultarrow` for a bow with an empty arrow slot; 0 for a magic shot), the attack (180: the CRC-32 of the lower-case RangedAttackName: `basic`, `basic_heavy`), and the row (184: the CRC-32 of the lower-case damage row: `bowattack1`, `bowattack2`, `bowattack_hipshot`; 0 for a magic shot). | Live L1, L2, L5. | `0x2A1A590` |
| The bow switches to the charged shot at a hold of 15 steps (0.5 s): 14 steps gave `basic` and `bowattack1` with the charge 0, 16 steps `basic_heavy`, `bowattack2`, and the charge 100. The game data has the marker `SetChargedAim` at 0.5 s of `Bow_Charge_UpperBody`. | Live L2 (5 holds of 450 to 750 ms). | — |
| The ammunition-free setting is on in the client of the supported build: the bow shoots with no arrow in its slot, and the item names DefaultArrow (the native fallback). | Live L1 (each of 46 bow shots). | `0x6171BAC` (the default), `0x256DD7B` (the fallback) |
| No report of a later hit. After a shot that hits a creature, a wall, or nothing, the client sends no item of the three lists and no other message (46 bow shots and 6 fire balls; the 2 s after each shot). The item has no number of its own: of the 46 shots only the geometry and the values of a hit near the muzzle differ (no byte that counts to 32: the local pool of the client is not in the item). | Live L2, L5 (`live/L2-G-35-shots.txt`). | Local tick `0x6896620`, stop `0x67A3BC0` |
| The hit near the muzzle: a creature in the first `m_projectileHitScanRange` meters of the line (2 m for the bow) is the receiver of the item itself: the reference of its hit volume at wire 92 (the actor, the entity group, the facet target), the point at wire 36, a token at 148, and the category 0 at 190 (255 with no receiver). A shot with no such creature has the receiver with the actor id 0 and G 0xFFFFFFFF. | Live L2 (a wolf 1.8 m in front). | `0x2577610` |
| The heavy attack of the fire staff needs the hold bit `attack_primary_hold` (191) in each step to 0.1667 s after the press (the condition `Attack_Heavy_NotHeld` of the fragment `Attack_Start_Charge` selects the light cast; `CanAttack_Heavy_Magic` at 0.1667 s needs the mana of `FireStaff_Heavy`). The client of a development run sends the bit only in the steps 1 and 3 after a press of the harness (with each graphics quality), and plays the light fire ball. | Live L5 (open: no heavy fire ball of the client in a development run). | — |
| The motion of a projectile of the client: the velocity and the acceleration of the projectile component, steps of `p += (v + a dt / 2) dt`, `v += a dt`; the acceleration starts when the projectile is the distance of a value of its ranged attack data from its start (`m_projectileAccelDelayDist`). The registration of the component: `m_baseSpeed` +0x100, `m_extraSpeed` +0x104, `m_timeForMaxSpeed` +0x108. The use of the two last values was not found. | Static proof. | `0x695CCC0` (motion), `0x68F2BA0` (registration) |

**The data** (`projectile-data.json`, `tools/gamedata/projectiles.sh`; `creatures.json`, `tools/gamedata/creatures.sh`):
the ranged attack profiles of `rangedattackdatabase.radb` (323 attacks: the range, the radius, the near range, the
penetration depth, the acceleration delay, the fall values, the bounces, the predicted acceleration, the directions of a
spread), the flight values of 85 projectile slices, 105 ammunition rows (DamageModifier 1.2 for each arrow and shot), 261
weapons with a profile (WeaponID, RangedAttackProfile, AmmoType), and 58 shots of basic attacks (the CAGE-RangedAttack
clips of the animation databases with a light or heavy row of the weapon category of the weapon; the projectile spells of
the magic weapons with the light or heavy row of the weapon). The values of this build: Bow/Basic range 100 m, radius
0.15 m, near range 2 m, acceleration after 5 m; `basic_arrow` 100 m/s, (0, 0, −9.8), 10 s; FireBall/Basic range 50 m,
radius 0.15 m, no near range; `spell_proj_fire` 100 m/s, no acceleration, 0.5 s; FireBall/Charged radius 0.33 m. The
creature data has the capsule of the CharacterComponent of 501 of 503 creature slices (the wolf: radius 0.7 m, a segment of
0.9 m on the axis of its body, the offset 0.1 m to the front).

**The rules** (project rules; the values of the data where it has a value; the source of each rule is in
[Projectiles](systems/projectiles.md)):

| Rule | What the backend does |
| --- | --- |
| The launch | The weapon in the hand gives the profile; the attack of the item must be an attack of the profile with no hit scan and no spread; a shot rule of the data must have the attack, the row, and the charge of the item (a magic shot: the row 0, and the row of its spell). The ammunition key must be the fallback of the ammunition type of the weapon (no equipped ammunition in the backend); a magic shot has none. The origin is 5 m or less from the character with no wall from 1.5 m above its feet; the direction is a unit vector (±0.01). A refused shot changes nothing. |
| The press | The newest press of the attack input at or before the item; a released press 30 steps or less before it; a heavy shot needs 14 steps of hold (bow) or 4 steps (magic). |
| The ledger | One projectile for each press: the key (character, session, input sequence, press). The entry stays 10 s after the life time. 16 projectiles of a character in the air at most. |
| The flight | On the world clock, in steps of 1/30 s: the speed (`m_baseSpeed`) and the acceleration (`m_accel`, after `m_projectileAccelDelayDist`) of the slice; the end at the range of the profile or at the life time of the slice. |
| The hit | The first creature (alive, not on its way back) near the segment (the position index) whose capsule the segment touches with the radius of the projectile and 0.25 m: one hit through `Combat::strike` with the row of the shot, the weapon and the gem of the launch, the damage modifier of the ammunition, and the point as the source; the projectile ends. A wall or the ground first (`Navigation::line_stop`): the projectile ends with no hit. |
| The near hit | A receiver of the item: a creature of the world with the reference of its slice, observed by the session, in the near range (with the radius and 0.25 m) of the origin, with no wall: the hit at the launch, no flight. A dead creature or one on its way back: the flight. |
| The cost | The ManaCost of the spell of a magic shot through `Abilities::change_mana` (0 for the basic attacks of the fire staff: the spells name no cost id; FireStaff_Primary and FireStaff_Heavy are 0). |
| The load | The flight looks only at the creatures near its segment and at the cells of its segment (case PJ10: 4,000 far creatures add no look). |

**The commands** (development routes): `tools/world shoot CHARACTER [--yaw D] [--pitch D] [--heavy]` (a shot of the server
for a character, also with no client), `tools/world projectiles`, the line `projectiles` of `tools/world list`.

**Not known, not done** (stage 3.3b-S3):

- The observers see no projectile: the server sends no projectile entity (type 16, `ProjectileReplicatedState`) and no
  message 327 (the bind of the visual of the owner). The owner sees its own visual; each observer sees the hit (the
  health, the reaction).
- The use of `m_extraSpeed` and `m_timeForMaxSpeed`: the server flies at `m_baseSpeed`.
- The precedence of `m_predictedProjectileAccel` of the profile (−9.8 for the fire ball) and `m_accel` of the slice (0): the
  server uses the slice.
- The grouped spread of the blunderbuss (2719, 2570), a hit scan (list 1), the penetration depth (0.2 for the bow), the
  bounces, the fall of the damage with the distance, a headshot (the posed hit volumes of a creature), the abilities of the
  ranged and magic weapons, an equipped ammunition (slots 12, 13).
- The heavy fire ball of a client in a development run (the hold bit, above).

### 4.28 Spell entities of three kinds: the Pylon, the trap, the Beacon (live proof of stage 3.3b-S2b; the rules are project rules with the values of the game data)

Stage 3.3b-S2b. Research: `~/Games/soulwarden/work/research/abilities-magic.md` (Q3 the cost of the Pylon, Q4 the Beacon),
`abilities-ranged.md` (Q4.10 the trap), `abilities-shared.md` (Q2). Live checks and the live proof:
`~/Games/soulwarden/work/evidence/abilities-spells2/implement/RESULT.md` (runs `s2b-l1` with the probe entity, `s2b-l2`
and the final run with the backend of the stage; the videos in `live/` and `proof/`). Code: `world/spells.h`
(`SpellRole`, the summon, the trap, the aura), `world/abilities.cpp` (`SpellPlan`, `spawn_summon`, `throw_spell`,
`spell_landed`), `world/projectiles.h` (`Projectiles::launch_spell`), `server/rep/states/spell_state.cpp` (the attachment,
the PositionInTheWorld state, the vitals of a spell, the events of a trigger), `server/rep/states/cooldown_state.cpp` (the
indefinite cooldown), `gamedata/spell_data.h` (format 2), `tools/gamedata/spells.py`, `tools/gamedata/projectiles.py`
(the ability shots). Document: [Spell entities](systems/spells.md). Test cases SB1 to SB10.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| A slice with no Spell component (the trap `trap_bear.dynamicslice`, asset `9CF92A75-C4C7-5F6A-94E3-A565B2328FB2`, sub id 2) shows only with its PositionInTheWorld state (type 13) at the slot of its PositionInTheWorld component (4): one group, field 0 the position (x f32, y f32, z u16 over −100 to 2000, as field 1 of the Spell state), field 1 the rotation (`07`: the identity). With only the metadata state the client makes the entity and shows nothing. | Live check (run `s2b-l1`, the probe: no trap with the metadata, the open trap with the PositionInTheWorld state). | `0x5CEA190` (position reader), `0x87B250` (rotation) |
| Field 6 of the Spell state (the attachment: the actor raw16 and the group u64 of an entity) makes the client move the picture of the spell with that entity (the Beacon `spell_life_staff_beacon.dynamicslice` on a character that travels 3 m: the ring moves with it). The server keeps the place of the spell for the interest and the area. | Live check (run `s2b-l1`, the probe on a remote test character); live proof (run `s2b-l2`, `l7-beacon-a`, `l7-beacon-c`: the Beacon on a character and on a wolf that runs). | — |
| The Vitals state (type 15) of a spell with a Vitals component (slot 1 of the Pylon `ice_pylon.dynamicslice` and of the trap): the health (f32), vitalsData (the flags with bit 0 dead, two times, a count), the change flags, and in the first record the maximum health, the tick rate 0, the vitals id (the CRC-32 of the lower-case row id of the base vitals: `IceMagicPylon`), invulnerability 0, display immune 0, the maximum health (u16). The client shows the name plate "ICE PYLON" and a health bar that follows the health of the server. | Live check (run `s2b-l1`, the probe, `l2-pylon-A`); live proof (run `s2b-l2`, `l5-pylon`: the bar goes down at each bite of a wolf). | — |
| An event of the init whose target the server triggered, sent completed, makes the client trigger that target at once: the Pylon slot 11 (the event of 45 s of the init on the entity `DestroyTrigger`) plays the death chain of the Pylon; the trap slot 11 (the event of 30 s of the entity `Lifetime` on the entity `Trigger`) plays the activation of the trap (the jaws close, the flash). An event that starts at a trigger (`m_startDelayOnTrigger`, no `m_startDelayOnInit`: the events of the owner `DestroyTrigger` of the Pylon) has no state before the trigger; from the trigger its state has the time that is left. | Live check (run `s2b-l1`, the probe: `l2-pylon-death`, `l3-trap-trigger`); live proof (run `s2b-l2`: `l5-pylon`, `l6-trap3`). | — |
| The cooldown state (4.22) of an ability with an indefinite cooldown: the entry of the ability with the end all ones (`FFFFFFFFFFFFFFFF`) and the minimum end 0 (the setter of the client with the duration −1 and MinTime 0). The backend sends it while the Pylon lives; the cooldown of the data (10 s) after its end. The HUD shows the button of the Pylon with no countdown while the Pylon lives, and the countdown of 10 s after its end. A second press while the Pylon lives: the client plays the cast animation, and the server refuses the press (no second Pylon, no payment). | Static proof (research abilities-magic.md Q3); live proof (final run: `proof/p1-pylon-hud.png`, `proof/p1b-second-press.mp4`). | `0x6923930` |
| The TemporaryAffiliation state (type 3563) at its slot of the Pylon slice (2): one group, field 0 the list (`00` no change entries, `00` no stamp, a count, then `type:u32 value:u32` for each entry). With no state, or with an empty list, the name plate of the Pylon has a red health bar for its caster. With one entry (type 1, value 1 or 2) the bar is blue, with or without an entry of the same type on the player (slot 17 of the player slice). The data of the slice applies no affiliation at the start (`m_affiliationsToApplyOnInstantiation` is empty): the original server set it at run time, with values that are not known. | Live check (final run, probe entities: `proof/a1` to `a5`; the game log has no read failure). | `0x405C370` (constructor), `0x406C020`, `0x4057BE0` (reader) |
| A spell of a different character: the observer sees the body, the light, and the end chain of the Pylon, the Beacon ring on itself, and the health of a creature that the Pylon hits (no damage number). The name plate of the Pylon of a different character does not show. | Live proof (final run: `proof/p5-observer-pylon.mp4`, `proof/p6-observer-beacon.mp4`). | — |
| The press of Ice Pylon gives no shot item and no message of the client. The press of Traps gives a shot item with the attack `traps` (the CRC-32 of the lower-case RangedAttackName) 8 input steps after the press; the press of Beacon a shot item with the attack `heavy` 4 input steps after the press. The client reports no hit of the toss or of the Beacon projectile. | Live check (run `s2b-l1`, the input of the captures: `press_to_shot.py`). | — |
| The status `Status_Ability_Lifestaff_Beacon` on the own character (the status state, 4.22 and 3.3b-S1): the icon in the tray, the green light on the body, the text "Healing Pulse" in the damage numbers (`ShowTextInDamageNumbers`). | Live proof (run `s2b-l2`, `l7-beacon-b`). | — |

**The data** (`spell-data.json` format 2, `tools/gamedata/spells.sh`): the members of format 1, and for each slice its
position slot (`positionSlot`), for each delayed event its owner entity and its marks `m_startDelayOnInit` and
`m_startDelayOnTrigger`, the Vitals component (`vitals`: the slot, the row of the base vitals, the target of
`m_onDeathEvent`), the AutoSpellComponent (`autoSpell`: `m_spellName`, `m_lifetime`, `m_lifetimeOnHit`,
`m_initialCastDelay`, the cooldown of `m_defaultRateOfFire`, the targets of `m_lifetimeExpireEvent` and `m_castEvent`,
`m_excludeTargetNWTags`), the DetectionVolumeComponents (`volumes`: the radius of a sphere, the time of the activation of
an entity that is not active at the start), the MomentaryOffenseComponents (`offenses`: the row, the radius), the events
of a DetectionVolumeEventComponent (`detectionEvents`), and the destroy time of each TimelineEntry (`timelines`); the list
`vitals` (the rows of the base vitals tables: HealthBaseMax, HealthOverridePercentSpellOwnerMaxHealth, the armor overrides,
the gear score). The values of this stage: the Pylon (slot 4 Spell, slot 1 Vitals `IceMagicPylon`: 0.2 of the maximum
health of the caster, armor 496; AutoSpell `IceMagic_Pylon_Projectile`, 15 s, 1 s, 2 s, `DestroyTrigger`, `FireTrigger`;
volume `Ice_Pylon` 20 m; the destroy 1.3 s after `DestroyTrigger`), the trap (slot 4 PositionInTheWorld, slot 1 Vitals
`Trap_Bear` 17; the volume 0.5 m from 0.1 s; the offense `Musket_Trap` 1.1 m; `Lifetime` triggers `Trigger` and `Dissolve`
at 30 s; the timeline of `Trigger` destroys at 0.508 s), the Beacon (slot 1 Spell; no end event of its own: the Duration
of its spell row, 10 s). The projectile data (`projectile-data.json`) has the 33 projectile spells of the abilities (the
11th value of a shot: the Pylon shot `IceMagic/Pylon` 100 m/s 0.5 s, the toss `Musket/Traps` 20 m/s with gravity 20 for
4 s, the Beacon `LifeStaff/Heavy` 100 m/s 0.5 s).

**The rules** (project rules; the values of the data where it has a value; the source of each rule is in
[Spell entities](systems/spells.md)):

| Rule | What the backend does |
| --- | --- |
| The Pylon | The spawn at 0.6 s, 2 m in front of the caster on the ground; the payment of 15 mana at the spawn; an indefinite cooldown from the press to its end; the health of the vitals row; the shots of its AutoSpell at the nearest creature of its volume that it sees, through the projectile path; a creature that a shot hurts takes the Pylon as its target and bites it; the death chain at the health 0 and at the end of its life (15 s), the removal 1.3 s after the trigger. |
| The trap | The toss of the shot item (or of the server), the trap on the ground at the landing; from 0.1 s each 100 ms a look for a creature in 0.5 m; one trigger: one hit with `Musket_Trap` and the root on each creature in 1.1 m, the trigger events, the removal 0.508 s later; with no creature the end at 30.508 s. |
| The Beacon | The projectile of the shot item (or of the server): a hit on a creature hurts it (1.46); the area on the entity that it hit (field 6) or on the ground; each 250 ms the place of the target and the friends in 3 m: the statuses of the row on a friend in it, off at its exit and at the end; the end at 10 s, at the leave of the target, at the leave of the caster. The friends: the caster (a group of one). |
| The record | The Spell state or the PositionInTheWorld state, the Vitals state of a summon or a trap, the delayed events of the init and of the triggers; one view for each session. |
| The cost | The due list (the shots, the steps of a trap and of an aura) and the position index near each spell (case SB8: a time limit). |

**The commands** (development routes): `tools/world spells` (the role, the health, the attachment, the friends in an
aura), `tools/world spells hurt ENTITY AMOUNT` (`POST /v1/dev/spells/hurt`; only with `--dev-commands`), `tools/world
cast CHARACTER 0 [--yaw DEGREES]` (a remote test character: the server throws the toss and the Beacon).

**Not known, not done** (stage 3.3b-S2b):

- The message `Pylon_Spawn` of the server (the client starts its own hold of the Pylon at it): not sent. The HUD shows no
  countdown while the Pylon lives, and the client plays the animation of a second press that the server refuses.
- The values of the TemporaryAffiliation of a summon (the types and the relation rule, research factions.md U9): the
  backend sends no affiliation state, and the own Pylon has a red bar. A made-up entry gives a blue bar (live check): a
  decision for the user.
- The picture of a projectile of the server for an observer and for the shots of the Pylon (a record of the projectile
  slice with a Spell state shows nothing: live check `l2-pylonproj` of run `s2b-l1`), as for the projectiles of 4.27.
- The Ownership and the TemporaryAffiliation states of a spell (not sent).
- The heal amount of a pulse of the Beacon (`HealthModifierDamageBased` 0.16 each 1 s): the heal base is stage 3.3b-S6.
- The friendly relation of two players (the groups): a decision of the user.
- The Pylon spells of the nodes (`_Regen`, `_Refresh`, `_Ultimate`, the burst `PylonBurst`), the frost volume of the Pylon,
  the interaction option of the trap (stage 2.5), and a creature that takes a trap as its target.

**The corrections of the code review** (made in stage 1.1b; test cases RK2 to RK12; project rules):

| Rule | What the backend does |
| --- | --- |
| A near-hit claim (F2) | The line of the launch from the origin in the near range: a wall on it before the contact with the capsule refuses the claim (`nearTooFar`), as for a flight. |
| The death of the attachment (F3) | The death of a creature or of a character ends each aura that follows it (`SpellEnd::targetDied`), and the aura step ends an aura on a dead entity. A projectile whose impact killed its target makes no aura. |
| Two auras with one status row (F4) | One status of the row on the friend, with the source of the first aura; the other aura is a second source of the row, with no new application and no new tick. The end of the aura of the source gives the status the source of the other aura; the end of the last source ends the status. |
| A status row shorter than the aura (F5) | A friend in the aura gets the row again when it ended (one application of the aura of its source; no new stack while it has the row). |
| A friend that leaves the world (F6) | The aura takes its statuses back before the entity goes away (the status of a character stays with its character id). |
| The record of an attached aura (F7) | A session observes an aura that follows an entity only while it observes that entity (field 6 names its actor); the aura goes out with its attachment. |
| A summon or a trap with no vitals row (F8) | The spell data refuses a slice whose vitals row is not in the list `vitals` and not in the list `vitalsNotInData` (a row of a different table that the tool names); such a slice makes no summon and no trap, and the Pylon pays nothing. |
| A status row of an aura that the data does not have (F9) | The press is refused (`noRule`) before the payment; the press gives the resolved rows to the landing. |
| A spell shot straight down (F10) | The direction of a shot of the trap or of the Beacon is valid when its length in three dimensions is 1; the horizontal fallback only for a direction that is not finite or has the length 0. |

### 4.29 The DLC: the entitlements of an account, the achievements of a character, the expansion (live proof of stage 1.1b; the rules are project rules from a decision of the user, with the values of the game data)

Stage 1.1b. Research: `~/Games/soulwarden/work/research/dlc-entitlements.md`. Live checks and the live proof:
`~/Games/soulwarden/work/evidence/dlc/implement/RESULT.md` (runs `dlc-l1a` with the dist backend and `tools/world
fragment`, `dlc-l1d` with the routes and no state, `dlc-l2` and the final runs with the backend of the stage). Code:
`world/ownership.h` (the grant policy), `server/rep/states/ownership_states.cpp` (the two states),
`server/http/entitlement_routes.cpp` (the routes), `gamedata/progression_tables.h` (format 2: the expansions, the
entitlements, the achievements, the rank limits with the expansion), `tools/gamedata/progression_tables.py`. Document:
[DLC and entitlements](systems/dlc-and-entitlements.md). Test cases DL1 to DL10.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The entitlement state (type 3133, `MB::EntitlementComponentReplicatedState`) at slot 19 of the player slice, owner only (the remote player slice has no such component): one group; field 0 the byte vector of the entitlements (bit `index & 7` of byte `index >> 3` for the EntitlementIndex of a row of the entitlement table; 575 bytes at most), field 1 the map of the balances (CRC to u32; the backend sends none), field 2 `entitlementsReceived` (true). A vector or a map is the full form of a container: `changes:size` 0, the version (`has:u8` 1 and `sequence:size64`), `count:size`, the elements. | Live check L1 (run `dlc-l1a`: the fragment of 198 bytes with the bit 1484, no read failure in the game log); test case DL10. | `0x302C5D0` (constructor), `0x3050A50` (vector reader), `0x3050940` (map reader), `0x303F960` (the bit of an entitlement), `0x279DC70` (container) |
| The entitlement bit of the expansion alone changes nothing that a player sees: the level window at 60 says "Rise of the Angry Earth needed to advance", the flail tree has "Requires Rise of the Angry Earth" and "Learn More", the trade skill limit is 200, and the mount tab says "Requires Rise of the Angry Earth". | Live check L1 (run `dlc-l1a`: `l1b-*`). | — |
| The achievement state (type 3210, `MB::AchievementComponentReplicatedState`) at slot 66 of the player slice, owner only: one group, field 0 the byte vector of the achievements (bit of the AchievementIndex: `Expansion_2023_Unlock` 11979, byte 1497, mask 08; 0x2000000 bytes at most). The bit of `Expansion_2023_Unlock` alone opens the expansion for the character: the level window shows "96,451 XP to Level 61" at level 60 (after the next change of the level), the flail tree shows "Rise of the Angry Earth" and the button "Assign", the trade skill limit is 250, Riding has no lock, and the mount tab shows the next requirement of the game ("Reach Level 20 and complete My Kingdom for a Horse"). A change of the bit during play opens the modal "Congratulations on purchasing the new expansion" one time (its text says "level cap 65"; the limit of the client is 70); the bit in the first record of a world entry opened no modal in the final runs. | Live check L1 (run `dlc-l1a`: `l1c-*`); live proof (runs `dlc-l2` and final: `i8-*`, `f-*`). | `0x317FEC0` (constructor), `0x279E550` (reader), `0x318D5E0` (the bit of an achievement), `0x684B340` (the expansion check: the achievement of each entitlement of the expansion enum), `0x6826B20` (the owned expansion row) |
| The level limit of the client: `0x3CED4B0` takes the MaxDisplayLevel of the owned expansion row minus 1. With the achievement in the first record it is 70 (the probe at `0x3CED5D0`: `rax=0x46`); with no achievement 60 (`rax=0x3C`). `javelin.max-player-display-level-override` is 0 (the override path did not run; the value at `0x149EF1CF4` read 0). | Live check L2 (runs `dlc-l1a` and `dlc-l2`). | `0x3CED4B0`, `0x3CED5D0`, `0x3CED56C`, `0x3CED62D` |
| The entitlement routes: the client sends the sync and then the list, after the token and before the character list; a reply 200 (`{}` for the sync, the list with the 19 aliases of the grant) is taken (no second request); a reply 404 (the backend before stage 1.1b) gives no error on the screen. The routes alone open nothing that a player sees. | Live check L1 (run `dlc-l1d`: the routes and no state, `l1d-*`; run `dlc-l1a` with 404). | `0x74D5100` (sync), `0x74CAA10`, `0x74CB580`, `0x74CC700` (list), `0x74C2770`, `0x74C2E60`, `0x74C15D0` (parsers) |
| Steam: the installed-DLC request of the client (`0x647AB30`, `BIsDlcInstalled` of Apps008 slot 7) did not run in three runs from the start to the world; Apps slots 10 and 11 (DLC enumeration) had no call (the hook logs each slot with no method). The hook answers true for slots 6 and 7 (no change in stage 1.1b). | Live check L8 (the trace point at `0x647AB30` in runs `dlc-l1a`, `dlc-l2`, and the final run; `steam: unimplemented` lines of the hook log). | `0x647AB30`, `0x647AB9B` |
| An equip of an item above the gear score of the owned expansion (MaxEquipGS: 800; 625 with no expansion): the client shows `@inv_cannotequipversionlevel` with no expansion; the item level of a gear score (the first level whose MaxEquippableGearScore takes it: 700 needs level 65) is a rule of the client (the tooltip "Requirement: Level 65", red at 60). | Static proof (`equipmentcommon.lua`, `0x5C4C960`, `0x4403360`); live proof (`i8-15`, `i8-16`: the sword of 700 at 60 and 65, equipped at 65). | `0x5C4C960`, `0x4403360` |

**The data** (`progression-tables.json` format 2, `tools/gamedata/progression.sh`): `expansions` (each row of the expansion
table: id, MaxDisplayLevel, MaxCraftGS, MaxEquipGS, MaxTradeskillLevel, EntitlementId: `None` 60 625 625 200, `Expansion2023`
70 700 800 250 `Ent_Xpack_RiseOfTheAngryEarth_Owner`), `entitlements` (the 19 entitlements of the grant: the expansion,
index 1484, and the nine Steam roots of the DLC packs with the 9 entitlements that they give; each with its index, reward
type, rewards, and achievement), `achievements` (`Expansion_2023_Unlock` 11979), and the fifth value of each progression
row (Expansion01MaxLevel: 250 for 18 trade skills, Riding 250, AzothStaff 0, each weapon 0).

**The rules** (project rules from the decision of the user of 2026-10-15; the source of each rule is in
[DLC and entitlements](systems/dlc-and-entitlements.md)): each account owns each entitlement of the list (rows of
`entitlements`, written at the login, at the sync, and at a world entry); each character owns the achievement of each
entitlement of its account that names one (rows of `achievements`, before its first record); the reward of an entitlement
of the type InventoryItem (AmuletIsabellaT1) comes one time to each character (a row of `entitlement_rewards` in the
transaction of the item); the limits of a player are the limits of the owned expansion (the level 70, the rank of a
progression row with an expansion value, the equip gear score 800). A route with the token of a different account gets 403
and changes nothing.

**Not known, not done** (stage 1.1b): the content behind the expansion (the region Elysian Wilds and its story, the
expeditions, the mounts, the recipes, the quests: their own stages); the use of the rewards of the DLC packs other than
the amulet (the skins, the emotes, the crests, the housing item, the title, the art book: their systems); the balances
of field 1; the effect of `entitlementsReceived` false; the Steam app id of the expansion (not needed).

### 4.30 Critical hits, a hit from behind, a headshot, and the passive rules (live proof of stage 3.3b-S5; the rules are project rules from a decision of the user, with the values of the game data)

Stage 3.3b-S5. Research: `~/Games/soulwarden/work/research/abilities-shared.md` (Q5, K15). Live check L6 and the live
proof: `~/Games/soulwarden/work/evidence/abilities-crit/implement/RESULT.md` (run `crit-l6` with the dist backend and
`tools/world message`, run `crit-live1` with the backend of the stage). Code: `world/combat.h` (`Combat::critical`,
`Combat::source_angle`, `damage_flags`), `world/passives.h` (the evaluator of the passive rules), `world/data_rules.h`
(the comparator), `world/projectiles.cpp` (`head_contact`), `gamedata/combat_tables.h` (`criticals`, `hitRules`),
`gamedata/creature_data.h` (`behind`, `head`), `gamedata/ability_data.h` (format 4). Documents:
[Combat and health](systems/combat-and-health.md), [Passive rules](systems/passive-rules.md). Test cases CH1 to CH10, PS1 to
PS7.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The flags of the damage number (type 2071, 4.15; two bytes, low byte first). The client shows the special number (orange and larger) for bit 6 Crit, or for bit 8 Headshot together with bit 0 Ranged and bit 5 CanCrit. A number with no flag, with Ranged alone, with Headshot alone, or with IsFromStatusEffect (bit 9) alone is the normal number (white, or yellow with an arrow for a weakness of the target). | Live check L6 (run `crit-l6`, `tools/world message` on a wolf: `0000` white, `0040` orange, `0060` orange, `0100` white, `0121` orange, `0001` white, `0161` orange, `0200` white); static proof. Live proof: a critical hit of a roll (667, 718), a hit from behind (603; the same hit from the front 502), a headshot of the bow (367; a shot on the body 283), all orange. | `0x686CC50` (handler), `0x694A7F0` (reader), `0x68E2400` (reflection) |
| The client reports no critical hit, no hit from behind, and no headshot: the melee contact has a point and no bone, and the shot item has no head flag (4.27). The server decides each of the four facts: a critical roll, a hit from behind, a critical hit from behind, a contact with the head volume. | Static proof (research Q5: the three input lists); stage 3.3b-S3 (no report after a shot). | `0x2A1A590` (the tail bytes of a contact) |
| The comparator of an AbilityData column with no text (TargetComparisonType, MyComparisonType, NumberOfHitsComparisonType, NumAroundComparisonType, and the stamina and mana kinds) is LessThanOrEqual. | Static proof (the parser maps each other text to enum 4). | `0x43B6390` |

**The data** (game data; the tools of [Game data](development/game-data.md)):

- `combat-tables.json`: `criticals`, for each weapon its CritChance and CritDamageMultiplier (587 weapons; the great axe 0.07
  and 1.35, the war hammer 0.06 and 1.2, the bow 0.08 and 1.3); `hitRules`, for each damage row CanCrit, NoHeadshot,
  NoBackstab, DmgCoefHead, and DmgCoefCrit (414 rows; DmgCoefCrit on 11 rows).
- `creatures.json`: for each slice with a DamageReceiver the two angles of a hit from behind
  (m_isHitFromBehindMinAngle, m_isHitByCritFromBehindMinAngle: the Grey Wolf 2.0944 and 2.3562, the player 1.5708 and
  2.3562; 503 slices), and the head volume (the HitVolume with m_isHeadshot) as a capsule in the frame of the creature: the
  bone of the volume in the bind pose of the skeleton of its character (the chunk CompiledBones 0x2000 of the `.chr`
  file), the center, the axis, the height, and the radius of the shape (374 slices; the wolf: the bone `bind_neck_2_jnt`,
  radius 0.2 m).
- `ability-data.json` format 4: each row of the ability tables of the players (2,025 rows, the passive rules), and the
  spell selections of the action lists (`actionSpells`, `actionNodes`: a CastSpell in an IfThenElse with a test
  IfAbilityActive of a node).

**The rules** (project rules; the source of each rule is in [Combat and health](systems/combat-and-health.md)):

- The chance of a hit is the CritChance of the weapon in the hand plus the CritChance of the fixed node rules and of the
  rules of the hit, plus the CritChanceModifier of the statuses of the attacker (0 to 1). Each hit with a chance above 0 takes
  one independent number of the generator of the world (`World::random_unit`): the hit is critical when the number is below
  the chance (a decision of the user, 2026-10-15). A tick of a status effect takes no number.
- The multiplier of a critical hit is DmgCoefCrit of the row (when it has one) or the CritDamageMultiplier of the weapon, plus
  one additive pool of the bonuses: the CritDamage of the rules, the CritDamageModifier of the statuses, and HeadshotDamage
  for a headshot (a decision of the user; DmgCoefCrit in the place of the weapon value: a decision of an agent). The bonus
  multiplies the damage that the target takes, both parts of a conversion.
- A hit from behind: the angle between the front of the creature (its yaw) and the source of the hit is not less than
  m_isHitFromBehindMinAngle of its slice. A hit at m_isHitByCritFromBehindMinAngle or more of a row with no NoBackstab is a
  critical hit with no roll (the data and the decision of the user: where the data makes such a hit critical, it is
  critical).
- A headshot: the line of a projectile through the body of the creature meets its head volume, and the row has no
  NoHeadshot. A headshot is a critical hit with no roll. A melee contact has no headshot.
- The flags: Ranged for each hit of a projectile, Crit and CanCrit for each critical hit, Headshot for each headshot,
  DamageCausedDeath for a kill. A combat file with no `criticals` (a tool from before stage 3.3b-S5) gives no critical hit
  and no roll.

**Not known, not done** (stage 3.3b-S5): the roll of the original server (a pity rule, the setting of 5000 ms of the
component CriticalHits), the full order of the critical multiplier, the empower, and the mitigation; the head in the pose of
an animation (the backend uses the bind pose: a shot at the chest of a wolf that looks at the shooter can meet its head); a
hit from behind and a headshot on a player; NoLegShots; the meaning of CanCrit 1 of a row (true only for the rows of the rapier
and of the heart rune). From stage 3.3b-S6 an explicit CanCrit 0 of a row (42 rows: the burst of Gravity Well) is a row
with no critical hit, and a blank cell can be critical (code review of stage 3.3b-S5, finding F2; `combat-tables.json`
format 2).

### 4.31 Heals: the number of the owner, the notice of the healer, the aim of a heal (live proof of stage 3.3b-S6; the rules are project rules from a decision of the user, with the values of the game data)

Stage 3.3b-S6. Live checks L3 and L9 and the live proof: `~/Games/soulwarden/work/evidence/abilities-heal/implement/`
(`live/L3.md`, `live/L9.md`, `RESULT.md`; runs `heal-l3` with the dist backend of stage 3.3b-S5 and `tools/world fragment`
and `tools/world message`, `heal-live1` and `heal-live2` with the backend of the stage). Code: `world/health.h`
(`Health::heal`, `heal_base`, `outgoing`, `received`), `world/statuses.cpp`, `world/abilities.cpp` (`HealPlan`, `confirm`,
`heal_target`, `cast_heal`), `server/rep/states/vitals_state.cpp`, `server/rep/health_messages.h`,
`server/rep/client_input.cpp`. Documents: [Combat and health](systems/combat-and-health.md),
[Abilities](systems/abilities.md), [Status effects](systems/status-effects.md), [Spell entities](systems/spells.md). Test
cases HE1 to HE11.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| Bit 1 of `healthChangeFlags` of the vitals state (slot 41, type 15; 4.18) in the owner view: the client shows a green "+N" at the feet of its character for about 2 s. N is the new health less the health that the client had (rounded down). The same state again gives no second number; each positive change of a state with bit 1 gives a number. Bit 0 alone changes the HUD with no number. The observer view shows no number with bit 1. | Live check L3 (run `heal-l3`, `tools/world fragment`: flags 3 gave "+53" and "+300"; flags 1 no number; the observer no number). Live proof (run `heal-live2`): "+412" for Divine Embrace, "+55" for each tick of Sacred Ground and of the Beacon. | Not looked for (behavioral proof). |
| Message 4298 (`VitalsComponentClientFacet_OnHealedPlayer`), to the session of the healer only: `target:u64` (the vitals facet id of the healer), `G:u64` (the group id of the healer), `healedTargetId:u64` (the id of the entity Stats of the healed character: 0xE6448D4D53852704 XOR 0xDAEE4E2555650AB7 XOR its group id), `amount:f32`. The client shows a green "+N" at the feet of the healed friend for the healer. The group id, the vitals facet id of the friend, and the Stats id of the healer itself as the healed target show nothing. | Live check L3 (run `heal-l3`, `tools/world message`: "+123" only with the Stats id). Live proof (run `heal-live2`): Divine Embrace on the friend `Remote` gave "+412" at its feet; the white bar under its name grew. | The reflection name of the message (static proof); the handler was not looked for. |
| The aim of Divine Embrace and Sacred Ground: the key (action bits 402 and 479, 4.22) starts the aim of the client ("LCTRL Target Self") and no cast. The cast bar (1.0 s for Divine Embrace) starts at the confirm: the self cast input `self_target` (action bit 430, LCTRL) or a click (`attack_primary`, action bit 555). | Live check of run `heal-live1` (the cast bar started only at LCTRL or at the click) and run `heal-live2`; the action list `playeractions_ability_lifemagic` (the actions `Ability_Life_Staff_TouchOfLife`, `_Cast`, `_SelfCast`, `Ability_Life_Staff_HealingWave`, `_Loop`, `_SelfLoop`: the key with `self_target` held casts at once on the caster). | The input table (`input-bits.json` of the research). |

**The data** (game data):

- The heal base: the base damage of the weapon at its gear score and the Focus scaling of the weapon (ScalingFocus: the
  life staff 0.0095) with the ModifierValueSum of the Focus table (`combat-tables.json`; [Combat and
  health](systems/combat-and-health.md)).
- The heal rows: Status_Ability_Lifestaff_DivineEmbrace (HealthModifierDamageBased 1.2, one tick, UseHealScalingValue),
  Status_Ability_Lifestaff_SacredGround and Status_Ability_Lifestaff_Beacon (0.16 each 1 s), the potency statuses
  (EffectPotencyMods), the heal values HealScalingValueMultiplier, HealMod, ConsumableHealMod, NonConsumableHealMod,
  DmgPctToHealthHealMod (`ability-data.json`).

**The rules** (project rules; the source of each rule is in [Combat and health](systems/combat-and-health.md)):

- The heal base of an ability (a decision of the user, 2026-10-15): the base damage of the weapon at its gear score times
  1 + ScalingFocus times the damage value of Focus + the level damage times the level above 1 (stage 3.3b-M1, a decision of the
  user through the orchestrator). Live check L9: the weapon damage of the attribute window has the same base and curve, and
  also the level part (in the heal base from stage 3.3b-M1) and the other attributes (not in the heal base).
- The amount: the base times HealthModifierDamageBased, the potency, the stacks, the potency factor, the outgoing factor
  of the healer (1 + HealScalingValueMultiplier of its statuses and of its rules with OnHealed + the equip load part), and
  the received factor of the target (1 + HealMod + the part of the kind), in this order (a decision of the user: multiply).
- One heal path (`Health::heal`): the real gain after the cap, bit 1 one time for the owner, message 4298 one time for a
  healer that is a different character. A heal of a dead, a full, or a hostile character changes nothing.
- Each heal with a number is its own owner vitals state (code review of stage 3.3b-S6, finding S6-R7; stage 3.3b-M1): when the
  health changed with no number before it (the rest rule, a food), the session first sends that health with no bit 1, then
  the health after the heal with bit 1, then the health now with no bit 1 when it changed after the heal. The number of the
  client is then the gain of that heal (case RW7 decodes the owner states; the client reads the change from the state before).

**Not known, not done** (stage 3.3b-S6): the point of the click of an aim (the server heals the nearest friend in range
and in sight, and puts Sacred Ground at the caster); the client reader of ShowUiDamageNumbersOnHeal (bit 1 of byte +0x368
of the status row, loader 0x44B6EFF); the groups (a friend is only a test fixture); an effect after a heal (OnHealed with
a status or a cooldown change).

### 4.32 The basic attacks of a melee weapon: the attack graph of the client, the fragment of a contact, the strike (live proof of stage 3.3b-M1; the rules are project rules with the values of the game data)

Stage 3.3b-M1. Research file `~/Games/soulwarden/work/research/abilities-melee.md` (Q2, the attachment databases, the
combo graphs, the charged branches). Live checks I1 and the live proof: `~/Games/soulwarden/work/evidence/abilities-combos/implement/`
(`i1/NOTES.md`, `proof/`, `RESULT.md`; runs `m1-i1` to `m1-i1d` with the backend of stage 3.3b-S6, runs `m1-proof` and
`m1-proof2` with the backend of the stage). Code: `world/attack_sequence.h`, `world/combat.h` (`Combat::step`, `claim_strike`,
`row_for`), `gamedata/weapon_actions.h`, `server/rep/client_input.cpp` (the dodge bit), `tools/gamedata/weapon_actions.py`.
Documents: [Combat and health](systems/combat-and-health.md), [Game data](development/game-data.md). Test cases MB1 to MB9.

**What the client sends** (behavioral proof): no message for an attack and no fragment id; the input steps (the attack input
bit 555 with 203, its hold 191, its release 567; the dodge 322, left Shift) and the melee contacts of 4.15. The server finds
the fragment of each contact from the input steps and the data.

**The attack graph** (game data; the action lists `playeractions_*.actionlist`, the animation databases
`player_<weapon>.adb`, `player_1h_melee.adb` or `player_2h_melee.adb`, `playermaleanims_1h_meleeattach.adb` or `_2h_`; the
transition reader 0x257CB60, 0x257CC10, 0x257D2A0, 0x257D0E0 and the damage reader 0x2566A50 of the research):

| Element | Rule | Evidence |
| --- | --- | --- |
| The clock of a fragment | (k - s + 1) / 30 seconds at the input step k of a fragment that started at the step s | The action layer of the client (the predicted movement state: state, start step, action number, fragment time at +12; `tools/research/player_position.py` with `SOULWARDEN_TRACE=6769980`): t = 0.0333 at the start step. Live check I1 |
| From rest | The attack input starts Attack_Start_Charge (CanAttack_Heavy) | Live: the action 5055 (great axe) at the press step |
| Attack_Start_Charge | Instant: Attack_Heavy_NotHeld 0 to 0.333 s gives the light swing (the first step with no attack input and no hold: the release step); Attack_Primary_Released 0.333 to 0.667 s gives Attack_Heavy (the partial charge); no condition from 0.667 s gives Attack_Heavy_Charged (the full charge; 0.5 s for the flail) | Live: the release after 0.45 s gives Attack_Heavy (great axe 550, war hammer 2319) at the release step; a hold of 0.9 s gives Attack_Heavy_Charged 19 steps after the press (great axe 2991 in a chain, war hammer 586) |
| A buffered transition | A press (not a hold) in its window starts its fragment at the end of the window; a press before the window is lost | Live: a second click 0.15 s after the first (war hammer, its window from 0.333 s): no second swing; a click at 0.5 s: the second swing at 0.667 s |
| An instant transition | The first step in its window with the input of its condition: the attack input or its hold (CanAttack_Heavy), the attack input (CanAttack_Primary_Rapier) | Live: a long hold of 3 s repeats the charged swing (war hammer: Start_Charge at 1.0 s of the charged swing, 586 again 19 steps later; great axe: the charged swing again at +59); a release in the buffered window of the second charged swing: no third |
| The end of a fragment | The end of its animation: the last clip of its first animation layer from its StartTime (seconds) at its speed; a clip of the same animation as the clip before continues its time. The client ends the action one step after | Live: great axe first swing 49 steps (1.617 s), second swing 59 steps (1.933 s), war hammer first swing 44 steps (1.447 s), charged swing 64 steps (2.1 s) |
| A strike | The damage clips of one damage key of the fragment (body and attachment, each shape) are one strike: one contact for each target | Live: one contact for each swing on a wolf; the rapier second step two contacts (atk1 at its step 9, atk2 at its step 16 or 17) |
| A dodge | Bit 322 in a CanDodgeBuffer window: the dodge at the end of the window; in a CanDodgeCancel window after the start of the fragment: at once; in the CanDodgeCancel window from the start: no dodge | Live (great axe, its windows cancel 0 to 0.167 s, buffer to 0.533 s, cancel after): left Shift at 0.6 s: the dodge (4221) at once; at 0.35 s: at 0.567 s; at 0.03 s to 0.15 s (three tries): no dodge |

**The contacts of the live checks** (the step of each contact in its fragment, the window of the data): great axe light 9
and 10 (0.3 to 0.4 s), its second swing 11 (0.367 to 0.467 s), its partial heavy 14, its charged swing 11; war hammer light
12, its second swing 16, its heavy and charged swing 14; rapier 10, 9 and 16, 8; sword 11, 5, 5; hatchet 7, 5, 9, 10; greatsword
14, 15, 11; flail 5, 7. The spear gave no contact in these runs.

**The rules of the server** (project rules on the data; the source of each rule is in [Combat and health](systems/combat-and-health.md)):
the sequence of each character follows the graph with the input steps of its session; a contact is a hit of a strike when the
clock of a visit at its input step is in the window of the strike (1 step early, 2 steps late) and on the world clock; the row
of the strike by the learned nodes of its clip conditions; one hit of each target for each strike, 32 targets at most; the
claim limit 64 in 1 s; a press within 0.25 s of the length of a fragment keeps the other fragment for the first contact; the
ends (a weapon change, an accepted ability, a travel, a death, a reconnect, a dodge of the table). No message of the server:
the client plays its own attack.

**The weapon switch and the press after CanTransition** (stage U3, research Q6; game data: the marks CanUnsheathe_Buffer,
CanUnsheathe, and CanTransition of the ProcLayer of each fragment, the grid rules 62 Attack -> Sheathe and 116 Sheathe ->
Attack, BackTicks 20 of the key input; live proof of the run `u3-before2` with `SOULWARDEN_TRACE=6769980`, the action layer 0 of
the own character: the war hammer charge 5054, swing 4602, C2 5046, Strike2 486, rest 4031; the great axe charge 5055, swing 467,
C2 5049, Strike2 484, rest 4041):

- SW-L1 (at rest, the war hammer in the hand, the key 1 and then a click): the great axe swing 467 starts at the key step + 3.
- SW-L2 (the key 0.13 s into a war hammer swing, a press 0.37 s after the key): nothing at once; the great axe rest 4041 at 1.1 s
  after the swing start (the mark CanUnsheathe 1.133 s), the great axe swing 467 at the switch + 3 from the kept press of 21 steps
  before.
- SW-L3 (the key in a held charge with no switch mark): the key is lost; the client keeps the war hammer in its bar.
- CT-L1 (a second press 1.033 s to 1.33 s after the start of a war hammer swing; 1.2 s for the great axe): the client always plays
  the chain (C2, then Strike2); the server accepts the hits of Strike2.

The server follows these rules (`Combat::step_switch`, `switch_rules`; [Combat and health](systems/combat-and-health.md)); it sends
no message for a switch (the paperdoll state of the weapon in the hand is not sent yet).

**Not known, not done**: the contact of the spear in this client; the stamina (no model: the reachable stamina payments are 0,
the greatsword pays 10 in its offense stance); the stances of the greatsword and the oils (FragTags); the length of a fragment
with two clips of one animation that ends by a different rule (war hammer Attack_Heavy: 44 steps, the rule of the table gives
51; its instant window to Attack_Start_Charge gives the same next fragment).

### 4.33 The other abilities of the great axe and the war hammer: the fragments of the abilities, the hits of an ability for the client, the block press (live proof of stage 3.3b-M2; the rules are project rules with the values of the game data)

Stage 3.3b-M2. Live checks I1 (run `m2-i1`, the backend of stage 3.3b-M1) and the live proof (runs `m2-live1` to `m2-live5`,
`m2-wb-baseline`, `m2-list`): `~/Games/soulwarden/work/evidence/abilities-gawh/implement/` (`i1/NOTES.md`, `live/`,
`RESULT.md`). Code: `world/abilities.h` (`Abilities::press_entity`, `strike_hit`, `fragment_event`, `follow_strike`,
`count_hit`), `world/attack_sequence.h` (`start_ability`, the terms of a transition), `server/rep/states/skill_states.cpp`
(slot 46), `server/rep/client_input.cpp` (bit 484), `gamedata/weapon_actions.h` (format 2), `tools/gamedata/weapon_actions.py`.
Documents: [Abilities](systems/abilities.md), [Passive rules](systems/passive-rules.md), [Spell entities](systems/spells.md).
Test cases GH1 to GH19.

**The contacts of the abilities** (live check I1; input steps of 1/30 s from the press; the time of the strike of the data):

| Ability (nodes) | Contacts | Data |
| --- | --- | --- |
| Reap (The Collector, Fatal Attraction, Gravity) | +9 (atk1, 0.3 s), +32 (atk3, 0.967 s); none for atk2 (the pull) | `Ability_GreatAxe_JudgementsReach_Upgrade` |
| Execute | +20 (0.667 s) | `Ability_GreatAxe_Executioner` (the base fragment) |
| Whirlwind (Unending Winds) | +15, +28, +39, +49, and with the hit counts of the server +58, +69, +80 | `Ability_GreatAxe_Whirlwind_Upgrade`: atk1 to atk7 |
| Armor Breaker | +14 (0.467 s) | `Ability_WarHammer_BlockBreaker` |
| Mighty Gavel | +18 (0.6 s) | `Ability_WarHammer_MightyGavel` (CAGE-CastSpell `WarHammer_Spell_MightyGavel` at 0.638 s: no message and no shot item of the client) |
| Clear Out | +23 (0.767 s) | `Ability_WarHammer_ClearOut` |
| Charge (Frenzied Momentum, Unpredictable Strike) | +12 at a wolf 6 m in front (the uppercut, `GreatAxe_Engage1`); with the block press at 0.23 s: the swing (`GreatAxe_Engage_Swing1`) | `Ability_GreatAxe_Engage`, `_Uppercut`, `_Swing` |

- The pull of Reap (atk2, rows with the damage factor 0 or 0.0001) gives no contact in three casts (live check I1). The server
  tests its shapes (behavioral proof, run `m2-live4`: the wolf 4.3 m away moved 4.5 m with the row
  `GreatAxe_JudgementsReach_upgrade2_RP2_reduced`, and it got `Status_Passive_GreatAxe_Gravity_Root` and then `_Root_Hold`).
- The caster moves in its action (Reap 1.5 m, Mighty Gavel 1.6 m, Clear Out 0.7 m; Whirlwind with Gusting Winds 7.95 m in 1
  s): the position check refused no report (live check I1).
- Wrecking Ball gave no contact in the street of the test place (runs `m2-live4` and `m2-wb-baseline` with the backend of stage
  3.3b-M1: the leap lands at the wolf). Stage 3.3b-M2b found the cause: the development character had no Breathing Room in these
  runs, and the base clip of Wrecking Ball seldom meets a creature (section 4.34).

**The hits of an ability for the client** (static proof and live proof). The condition `IfAbilityNumHits` of the action lists
(`AbilityName`, `NumHits`, `ComparisonType`; reader of the condition 0x2BED200) reads the hit counts of the ability state:

```text
MB::AbilityComponentReplicatedState, slot 46 (constructor 0x66EA770), group 0
  field 1  hitDataNumHits     list of u8   (container type 0x80B9790, reader 0x279DC70: the element read 0x87A190)
  field 2  hitDataAbilityIds  list of u32  (container type 0x821A0C0, reader 0x3779060: the element read 0x87A220)
  the full list form of 4.13: 00 (no change entries), 01 sequence:size64, count:size, the elements
```

- The two lists are parallel: the count of the ability with the id at the same place. The id is the CRC-32 of the lower-case
  ability id text (`Ability_Greataxe_Whirlwind`).
- Live proof (run `m2-live3`): Whirlwind with Unending Winds and the counts of the server played seven spins with seven hits.
  With no count (runs `m2-i1` and `m2-live2`) the client left the spin at about 2.0 s also after four accepted hits
  (`Whirlwind_LessThan_1` at 1.933 s).
- The server counts the accepted hits of the strikes of the last cast of each ability whose row has TrackHitCount (game data),
  from 0 at the press. Not known: when the server of the game set the count to 0, and fields 3 and 4.

**The block press** (live proof, run `m2-live4`): the input bit 484 (`block_press_virtual` of the input table; the right
pointer button) in the window 0.1 s to 0.4 s of the charge of Charge with Unpredictable Strike starts the swing
(`Ability_GreatAxe_Engage_Swing`; the condition `IfInput block_press_virtual` of the action list). The client then reports its
contact in the window of the swing.

**The step of a press** (user test 2, UT2-1; live proof of the run `u2-before` with the release of stage 3.6a and of the run
`u2-after`; static proof of the research `user-test-2.md`): the conditions CanAttack_<ability> of the great axe and the war hammer
read the press of the input step with no BackTicks (the evaluator 0x2BEBDA0, the bit read 0x2A22F90). A buffered clip latches the
press of a step in its window (0x257D2A0) and starts the action at the end of the window (0x257D5B0, 0x257D0E0); an instant clip
starts it at the press (0x257D780). Live (layer 0 of the own character, `fragment_sampler.py`): a press of Wrecking Ball at the last
step of a buffered window of Attack_Primary, of Attack_Start_Charge, and of Attack_Primary_Strike2 (the case of the log of the
user) played at the end of the window (6 of 6 tries; the step of the client was the step of the server); a press in an instant
window of the charge and of the light swing played at the press; a press at 0.4 s of Shockwave (its window of Wrecking Ball from
0.8 s) did not play: the release started it 1033 ms later and ended the fragment of Shockwave. The backend refuses such a press
(`casting`). A client whose character carries more than its weight limit ("Encumbered") plays no ability at all, and the server
of the release cast each press (live: three presses at rest, no action; after the removal of the materials the next press played):
the backend had no weight rule (done in stage U3: the server refuses such a press, `encumbered`; 4.10). The refusal of the log of the user at input 5204 (a press 176 ms after a hit of the ghost on
the character) has no cause in the server: the log lines of UT2-13 give the next one its visits.

**The collision of Charge** (live proof): the client starts the uppercut at a collision with a creature (`character_collided`).
The server sees no collision; the contact of the uppercut strike shows it (the server starts the uppercut at the step of the
contact).

**The data** (game data; format 2 of `weapon-actions.json`): the start windows CanAttack_<ability> of each fragment; the
selections of an action (ChangeActionFragment); the CastSpell of an action with the mark of its condition; the CAGE-CastSpell
clips and the CAGE-Named marks of a fragment; the conditions of the transitions inside an ability. A Blend with ExitTime -1
starts its clip at the end of the clip before it (the sprint attack of the hatchet; the old rule of the tool gave the length
-0.6 s).

**Not known, not done**: the removal of a projectile by the shield of Maelstrom (no creature shoots: the creature data tool keeps
only the melee damage events of an attack, stage 3.3b-M2b); the picture of the
ground of Mighty Gavel in the client (the stills of the live run do not show it clearly); the Upgrade fragment of Executioner
(no action selects it); the stamina, grit, and the block of the nodes of the two trees.

### 4.34 Wrecking Ball and the homing of an ability: the clips of the strike, the target of a Homing clip (live proof of stage 3.3b-M2b; static proof for the client code)

Stage 3.3b-M2b. Evidence: `~/Games/soulwarden/work/evidence/abilities-wrecking-ball/implement/` (`RESULT.md`, `notes/presses.txt`,
`notes/homing-research.md`, `notes/homing-trace-counts.txt`, `live/`). Document: [Abilities](systems/abilities.md). Test case GH20.

**The two clips of the strike of Wrecking Ball** (game data: `playermaleanims_2h_meleeattach.adb`, fragment
`Ability_WarHammer_WreckingBall` with the tags `2H_Melee+Warhammer`; the named conditions of
`playeractions_ability_warhammer.actionlist`). Both clips have the damage key atk1 (0.8 s to 0.967 s) and the row
`Warhammer_Damage_WreckingBall`:

| Condition | Nodes | Shape |
| --- | --- | --- |
| `WreckingBallBase` | Wrecking Ball, not Breathing Room | capsule: radius 0.7 m, half height 0.6 m, 0.3 m along the joint `master_root` of the weapon attachment |
| `WreckingBallAOE` | Wrecking Ball and Breathing Room (`Upgrade_WarHammer_Wreck_Pushback`) | sphere: radius 1.5 m, scale 2, 0.5 m above the root of the character |

- The game text of Breathing Room: "Expands the range of Wrecking Ball to a 1.5m radius". The row
  `Warhammer_Damage_WreckingBall_Push` is only in the DamageTableRow list of Safety Measures (`Upgrade_WarHammer_Wreck_Fortify`):
  no clip uses it.
- Live proof (runs `wb-d1`, `wb-33a`, `wb-13b`, `wb-s1`, `wb-s4`, `wb-base`, `wb-br1`, `wb-trace`; `notes/presses.txt`): with no
  Breathing Room the client sent a contact in 2 of 27 presses (a wolf 1.6 m to 5.6 m in front, with the camera level and down);
  with Breathing Room in each press (5 of 5: the street of the test place to the west, the default spawn to the north, after a
  walk). The backends of stages 3.3a, 1.3b, 3.3b-S1, 3.3b-S4, and 3.3b-M2 gave the same result with the same character: no stage
  of the backend changed the contact.
- A ring of seven wolves 1.3 m around the landing point (run `wb-base`): the base clip hit only the wolf 1.3 m behind the
  landing point and to the right; the wolves in front of the landing point got no contact. The capsule follows the animated
  joint of the attachment, not a point in front of the character.
- The leap goes in the direction of the body: after a travel to the north (run `wb-d1`: 2.4 m north with the camera to the west).

**The target of a Homing clip** (static proof: `notes/homing-research.md` of the evidence; live proof: run `wb-trace`). The
fragment of Wrecking Ball has two Homing clips (0.0 s: Radius 3, MaxAngle 60, TurnRate 0.6; 0.3 s: Radius 4, MaxAngle 55,
TurnRate 0.2; MoveToTarget false).

- The client chooses the target itself, from the creatures whose homing flag is on. The server gives the flag in the movement
  state of each entity (`dataBits` bit 0x02, below); the homing components of the player and of the creature have no replicated
  slot. `HomingComponentClientFacet_SetHomingOverrideTarget` (type 1790) only forces a target.
- The scan of the player facet (0x68A3330) asks each `HomingTargetComponentClientFacet` near the player. Its handler (0x67A8A10)
  adds the facet when the transform (+0x78) is not null, the entity is not dead (and not at death's door, with the flags of the
  slice), the targetable flag (+0x70) is not 0, the entity is not the caster, and the distance is in the scan range. Then the
  relation test (0x5B29100: faction, company, group, duel), the invisibility test, and the wedge test (0x5C6A000: the horizontal
  distance less than Radius, the height less than Height, MaxAngle as the half angle). The best score wins; the scan repeats in
  each frame.
- The wolf slices have the component (component id `5FB6C5C9C0C00D41`; client facet `m_isNotTargetableWhenDead` and
  `m_isNotTargetableWhenDeathsDoor` true; server facet `m_notTargetableOnInit` false). The constructor of the client facet
  (0x6702C50) sets +0x70 to 1.
- Live, before the correction (`SOULWARDEN_TRACE=67A8A10,68A3330,5B29100,5C6A000,684B000`): in one press of Wrecking Ball the scan ran 26 times and the
  handler 78 times (three facets); the relation test and the wedge test ran 0 times. The flag +0x70 was 0 in the three facets. A
  write of 1 (`tools/hx write`) stayed only in the facet that the handler refuses as the caster; in the two facets of the
  creatures the flag was 0 again at once. The leap did not turn to a wolf 3 m away at 35 degrees, and Mighty Gavel (a Homing clip
  with MoveToTarget) did not move to a wolf at 30 degrees.
- The defaults of the homing settings (registration 0x616C670): `javelin.homing-validate-target-position` true,
  `homing-target-validation-timeout-ms` 1000, `homing-fail-validation-on-timeout` false; no reader in the client (rules of the
  server of the game). The target field of the input entry (reader 0x416EB00) is not the homing target: it has no entity id.
- The writer of the flag (static proof, `notes/homing-research.md` part 2, and live trace): the setter of the
  HomingTargetComponent (vtable 0x8547BB0, slot +0x110, 0x6928AB0) writes facet +0x70; the action list component calls it
  (actor slot +0x490, 0x48496E0). The apply function of the action list state (0x2A2BC20) calls it at each state that it applies,
  with bit 1 of the 16-bit value `(moreDataBits << 8) | dataBits` (copy 0x2A2AF07, getter 0x2A27A90); before it, it gives bit 0
  (getter 0x2A279B0) to the camera lock target (actor slot +0x480, 0x4848AD0: the CameraLockTargetComponent, vtable 0x8130658).
  Live trace (run `wb-trace2`, `SOULWARDEN_TRACE=48496E0s`): 400 calls, each with the value 0, each from 0x2A2BC20, for about
  eight entities. The server of the game sets the homing bit from `m_notTargetableOnInit` of the server facet (0x68689D0 calls
  the same actor slot with its negation). Game data: each of the 503 creature slices of `creatures.json` has
  `m_notTargetableOnInit` false (1,006 values: two target components in each slice).
- The correction (stage 3.3b-M2b, `server/rep/states/movement_state.cpp`, test cases GH21 and CR2): each movement state of a
  creature has `dataBits` 0x03. Live (runs `wb-homing1`; `live/fix-*` of the evidence): the leap of Wrecking Ball turned 0.55 m to
  a wolf 3 m away at 35 degrees (before: straight), and Mighty Gavel moved 0.88 m and 0.27 m to the side to a wolf at 30 degrees
  (before: 2.07 m straight). The base clip of Wrecking Ball still gave no contact in 4 presses with the homing: open.

### 4.35 Interactions: the request of a client, the reply, the cancels, the state of an object, the lore pages (live proof of stage 2.5a; static proof for the client code)

Research: `~/Games/soulwarden/work/research/interaction-stage.md` (two passes) and `interaction.md`. The system: `world/interactions.h`,
[Interactions](systems/interactions.md). Live checks L1 to L3 of stage 2.5a: `~/Games/soulwarden/work/evidence/interaction/implement/`
(`LIVE-CHECKS.md`, `RESULT.md`).

**The ids of a level object** (game data and client code; research Q1; live check L1). A placement of a layer
(`*.capitals.json`) has a UUID. Its actor id: the 16 bytes of the UUID, the two halves of 8 bytes XOR together, the 8 result bytes in
the reverse order, the low 3 bits of the first byte set to 3, then 8 zero bytes (a managed actor: the client makes the entity group
from its level data; managed-id test 0x6165F20). G is the first 8 bytes, little-endian. The rule of 4.2 gives the instance ids and the
facet targets: K = (the root entity of the slice) XOR G. Example (lore page `lore_00_aeternum_title_07_02` at 8838.85 4246.60 136.45,
UUID `bdf81e34-8c29-69ca-08d8-d51e50b2600c`): actor `c3099bdc2acb20b50000000000000000`, G `b520cb2adc9b09c3`, the Interact component
`d7d687636de982df` of the entity `Interactable` (slot 2), target `8cf752924a848ca2`, option entity `b520cb2edc9b09c3`. The client sent
these values (live).

**The request** (input report 1589, mask bit 8 of an entry; writer 0x35E67E0, reader 0x35EBC30; research K1). Big-endian numbers;
`size` and `size64` are the prefix codes of the hub; a text is a size and its bytes; a flag is 0 or 1:

```text
present:u8                                    0: no request (the block ends)
actor:16 bytes G:u64 target:u64               the object: the Interact component (record +0x10, +0x20, +0x28)
action:u8 baseAction:u8 optionEntity:u64 u32  +0x30, +0x31, +0x38, +0x40
text u32 u32 f32 committed:flag               +0x48, +0x70, +0x74, +0x78, +0x80
text88 text text text holdTime:f32            +0x88 (the display key of the object: "@00_Aeternum_Title_07_02"), +0xB0, +0xD8, +0x100, +0x7C
hasVectors:flag [6 f32]                        +0x81, +0x130, +0x140
flag u32 requiredItemCount:u8 optionId:u32    +0x150, +0x154, +0x158, +0x15C (the CRC-32 of the lower-case option name)
u32 tag:u32 u32 u32                           +0x128, +0x44, +0x160, +0x164
requiredCurrency:size64 u32 requiredLevel:size text   +0x168, +0x170, +0x1A0, +0x178
u32 u8 flag u32 text                          +0x1A4, +0x1A8, +0x1A9, +0x1E8, +0x1C0
u32 u64 u32 flag                              +0x1F0, +0x1F8, +0x200, +0x204
```

- 131 bytes with empty texts and no vectors. Live: 155 bytes (the text of +0x88). ReadLore: action 47, base action 47, option
  `ef696db1`, committed 1. AddLoreToJournal (the key T): action 50, base action 0, option `81e1dbd6`, committed 0. OpenConversation:
  action 63, base 63, option `507cac68`.
- The client executes the option at the key, before any reply (0x4916540, 0x4918560, 0x4917310, 0x4910750 in this order, live):
  the journal opens; a one-shot shows its local notice ("NEW CHAPTER DISCOVERED", "DOCUMENT DISCOVERED").
- An older entry with no bit 8 has the block of the entry before it (the newer one); the newest entry with no bit 8 has no request.
  The client sends the block in each report while its request is current. The block goes away when the client clears its records:
  the close of the journal (Escape), a reply, a cancel.
- The backend reads the block (`InputInteraction`); a text above 1,024 bytes or a flag that is not 0 or 1 is not a block of the
  client (the entry gives no step: a rule of this project).

**The messages** (component messages of the Interactor facet of the character: `target = G + 6C5718DBFE6DF612`, `entity = G`):

| Type | Direction | Body | Client code | Proof |
| --- | --- | --- | --- | --- |
| 3743 `InteractorComponentClientFacet_ReceiveInteractionResult` | server to owner | `target:u64 entity:u64 reference:32 bytes action:u8 result:u8 text:size(0) u32(0) 16 bytes(0)` (71 bytes); result 1 accepted, 2 refused | writer 0x35E6C80, reader 0x35EC230, handler 0x49175F0 (it compares the 32 bytes with its record; a different reference is ignored) | live: result 1 for the read (0x4917310 from 0x491768F; no second window), result 1 for the one-shot (the request clears: 0x490EE30 from 0x49179F7), result 2 for the read (the records clear; the journal stays open), result 1 for the conversation (no change), result 2 for the conversation (it closes) |
| 2473 `InteractorComponentServerFacet_RequestCancelCommittedInteractionFromClient` | client to server | `target:u64 entity:u64 actor:16 bytes G:u64 target:u64` (48 bytes) | sender 0x490AD30 | live: after 6705 at the end of a conversation; after 3746; **not** at the close of the journal |
| 3746 `InteractorComponentClientFacet_CancelCurrentInteraction` | server to owner | `target:u64 entity:u64` | handler 0x490EDA0 calls 0x4916150 | live: in an accepted read the client sends 2473 and clears its records; the journal stays open |

**The states** (tools/st; research K3, K8):

| Type | Slot | Fields | View | Proof |
| --- | --- | --- | --- | --- |
| 3752 `MB::InteractorComponentReplicatedState` (constructor 0x49193B0, apply 0x49199D0) | 26 of the player slice and of the remote player slice | group 0 field 0 `m_cachedCommittedInteractGDEID` u64 (reader 0x279E300); group 1 field 0 `m_enabled` (the backend sends none) | owner and each observer: the G of the object of the committed interaction, `00000000FFFFFFFF` after its end | live: `01 01 <G>` gives facet +0x408 the G (0x49199D0 on the Interactor facet); `01 01 00000000FFFFFFFF` the empty value |
| 2930 `MB::InteractReplicatedState` (constructor 0x35E2C70, apply 0x35CB710) | the slot of each Interact component of the slice of the object (game data: 2 for a lore page, 4 for the commander, 5 and 6 for the storage sheds of the settlement) | field 0 `Enabled` (1 byte, reader 0x17B3CB0), field 1 `HasInteractors` (u32, reader 0x17B3620), field 2 `CooldownUpdates` (the backend sends none) | each observer of the object | live: Enabled false hides the prompt (the next object gets the focus), true restores it after a new focus; HasInteractors 1 on an unlimited page changes nothing that a player sees |

**The record of a level object** (live check L2). A record with a new interest number, the metadata state (the slice asset of the
placement with its sub id, and the managed actor id), and the state 2930 for the slot: the client applies it to the object that it
made from the level data (0x35CB710 on its Interact facet; no line "Received repeated metadata"). The stop of the interest number
does not remove the object, and the client keeps the last state of the record.

**The lore page** (game data; live check L3). The slice of a lore page has `m_loreId` (`00_Aeternum_Title_07_02`); the row of
`javelindata_loreitems` names its achievement (`Lore_unlock_00_Aeternum_Title_07_02`), and `javelindata_achievements` its bit
(AchievementIndex 2032, Category Journal, GameEventId Read_LoreItem). The achievement state (slot 66, type 3210, 4.29) with the bit:
the journal (Documents) shows the page, also a page that the client did not read in that run (live: bit 3519, "Tales of Everfall
1/50"), and the prompt shows "Already Read" in place of "Add To Journal" at the next focus. The full form of the vector with a
higher sequence during play works (live). A restart of the backend and a new world entry: the first record has the bit, and the
journal has the page.

**The rules of the backend** (stage 2.5a): the checks of [Interactions](systems/interactions.md) (the reference and the option of the
data, the range `m_interactRangeSq` in 3D, the exact position, Enabled, the capacity, the requirements, a handler). The lore page
(ReadLore, AddLoreToJournal) keeps the achievement of the page; the options of a presentation of the client (OpenConversation, the
chair, the bed, the animations, the cinematic video) are accepted with no effect; each other option is refused (3743 result 2).

**Not proved:** the arc and the line of sight of an object (no checked object has them; the backend refuses an object that needs
them); the grammar of `CooldownUpdates`; the chairs (a bench showed an "E" mark and sent no request at the key E in one try). The
server objects (the stations of a settlement): section 4.36.

### 4.36 Server children: the stations of a settlement, the camps, the timed use, the ends of a use (live proof of stage 2.5b; static proof for the client code)

Research: `~/Games/soulwarden/work/research/interaction-stage.md` (Q1, Q2, the stage plan 2.5b). The system:
[Interactions](systems/interactions.md). Live checks L4 to L7 and the final run of stage 2.5b:
`~/Games/soulwarden/work/evidence/stations/implement/` (`LIVE-CHECKS.md`, `RESULT.md`). Addresses are image-relative (client build
1.400.6031.6004151).

**The child of a spawner** (game data; live check L4). The settlement `Settlement_NorthWindsward_Complete` (placement
`7d371958-377e-3c98-fff2-741c1bdf109d`) has the PrefabSpawner `Public_Blacksmith` (entity `0e220c168abe8deb`,
m_isClientAuthoratative false, m_spawnOnInitialize false) with the slice `settlement_station_blacksmithing_t5` (asset
`a6f8abbf-aed0-5da0-a950-ce98a77635b1:2`, root `4dcb587965f8256e`). The place of the child is the placement matrix times the
m_worldTM of the spawner entity: 8857.867 4260.186 138.093, rotation (0, 0, -0.130849, 0.991402). The client shows nothing for a
child before a record of the server.

| Fact | Value | Proof |
| --- | --- | --- |
| The smallest record of a station | The metadata state (the asset of the slice, an actor of the allocator) and the PositionInTheWorld state (slot 2 of the station slice): `01 07 <x f32> <y f32> <height u16 over -100 to 2000> <rotation> 00`, here `0107460a67784585217c1d06036f3f00`. With these two the prompt "FORGE TIER 5 / E Craft" and the crafting screen work. The BuildableController state (slot 1, type 1594, `01 01 05`: Complete) and the Interact states (slots 4, 5) are not necessary (0x47B9190, 0x47A43B0, 0x35CB710 run) | Live L4 (fresh probes: metadata + place: prompt and window; + Complete; + Interact) |
| The body of a station | The station slice has no mesh. Its entity `Blacksmith` has a PrefabSpawner (m_isClientAuthoratative false, m_spawnOnInitialize true, m_isVegetationSliceAsset true) of `craftingstation_t5_blacksmith_complete` (asset `bd64c85f-a57c-5f6d-98c4-d1b8883e474e:2`, PositionInTheWorld at slot 1): a second server child. Its record (metadata, place) shows the forge | Live L4 (`L4-03-visual-child.png`) |
| The rotation of the PositionInTheWorld state | `header:u8`, then a `u16` for each of x, y, z with no header bit; the bits 0x01, 0x02, 0x04 give 0, 0x08, 0x10, 0x20 give 1, 0x40 makes w negative; v / 32767.5 - 1; the writer truncates (c + 1) * 32767.5. The station: header `03`, z `6f3f` | Static 0x87B250 (research `entities.md`, `conversations.md`); live L4 |
| The ids of a child | G is the entity id of the allocator actor; K = root XOR G; the target of InteractBound2 is (`4dcb581b65f8256e` XOR K) + `2925259dec5f855e`; the option entity is the instance of `Functional` (the Assembly entity) | Live L4 (the block of the client) |
| The station facts in the client | 0x2E6A440 gets the AZStd::string of the Assembly key (`Blacksmith5`); after 0x2E61E00 the crafting facet has at +0x3B8 the key, at +0x450 the fee flag (from Assembly +0x180: 01), at +0x398 the Assembly reference (the actor, G, and the target: the instance of `Functional` plus the component `698f02d45d873f1a`) | Live L4 and the final run (memory reads; the same values as `StationContext` of the backend) |
| The end of a crafting screen | Escape: the client sends 2473 with the reference, then the block goes away. 3746: the screen closes and the client sends 2473 | Live L4, L7 |
| A key with no row | `Carpentry2` (no row of craftingstations.craftstationdb): the client opens "WOODSHOP TIER 5" with its recipe list | Live (final run) |
| A camp | `permanentcamp_master` (asset `f7967c2b-1fda-56bb-8ae8-041d1872dc06:2`; Position slot 1, Ownership slot 4, Interact slot 5; Assembly Camp5, fee false; no mesh): "CAMP" and no option with no Ownership state; the Ownership state (type 3217, group 0 field 4 currentPermissionRule `01 10 01`, the m_initialPermissionRule of the slice) gives "Camping not unlocked yet." (0x42AB230 shows `@ui_camping_error_not_unlocked` when a request of the player at 0x42AD5D9 gives 0); with a rank of the categorical progression `Camping` (slot 65): "Set Camp Recall Point" (BindToCampOption, action 73, one-shot). The rule 0 hides the option. OpenCraftingOption stays hidden (not solved) | Live L5 |
| The player camp | `defaultcamps/campt5` (asset `be7e7f37-d6d9-5065-9324-940d1be5894a:2`; BuildableController slot 1, Position 2, Ownership 7, Interact 9; Camp5): with Complete a tent, "CAMP TIER 5", the same one option; with its m_initialState 3 a blue ghost of the tent | Live L5, final run |

**The timed use** (game data, client code; live check L6, final run). The powder keg (`deployable_powderkeg`, asset
`30b63473-6fd6-53ee-a89f-3a9c0045e0f7:2`; Ownership slot 3, Buildable 4, Position 6, Interact 10 with Trigger, capacity 1, a box
trigger of 2 x 2 x 3 m): with m_initialPermissionRule 4 "No public access."; with the rule 1 "E Ignite Fuse". Trigger: action 33,
committed, Delay Time 5 s (uidatabase.uidb).

| Fact | Value | Proof |
| --- | --- | --- |
| The press | 0x42A93E0, 0x4916540, 0x4918560, 0x49106F0 in one frame; the request has hold time 5.000 (the Delay Time) | Live L6 (traces) |
| The progress | The character kneels at the keg; a white frame fills around the key E (a small part at 0.3 s, half at 2.4 s, almost full at 4.8 s) | Live L6 (video) |
| The end of the time | 0x4910750 (the object execution) 4.99 s to 5.00 s after the press. With no reply the client keeps its request until the release and then sends 2473; with 3743 result 1 it clears its request after the execution (the block goes away 5.03 s after the press) | Live L6, final run |
| A release before the end | 0x4916150 and 2473 at the release; no 0x4910750 | Live L6, final run |
| 3746 during the hold | 0x490EDA0, 0x4916150, 2473 at once; no 0x4910750; the progress stops | Live L7, final run |
| Damage during the hold | The client goes on: 0x4910750 at 5 s. The server rule of the backend (3746) ends it | Live L7, final run |
| A walk during the hold | The character stays at the keg; the execution comes at 5 s | Live L7 |
| The removal of the keg during the hold | 3746 and the stop of its record: the keg goes away, the character stands up, no execution | Final run (video) |

**A conversation that the server ends** (live check L7, final run): 3746 closes the conversation; the client sends 2473 and 6705, and
the answer 6709 frees the input and the camera (a walk of 6 m in 1.5 s after it). The backend sends no extra 6709 for this end.

**The rules of the backend** (stage 2.5b, [Interactions](systems/interactions.md)): the spawn rule of the station children (150 m,
200 m); the record (metadata, PositionInTheWorld, the authored BuildableController state, the authored Ownership rule, the Interact
states); the StationContext; the full Delay Time of the option on the world clock from the accepted request, a check at its end, one
completion; the ends: the removal of the source, the range, damage for the action 64 and the timed options; the service grant.

**Not proved:** the option OpenCraftingOption of a camp (hidden in each try, live check L5); the rule of the server of the game for
the time of a timed use (the client timer is the Delay Time with no factor 0.6; the default `javelin.delayed-interactiontime-server-scalar`
is 0.6 at 0x7F40084, research K5); the ids of a person that a client spawner makes (the artificer at the forge of Oxboro: its
conversation is refused); the town state that chooses the station children of the server of the game (m_spawnOnInitialize false).

**The corrections of the code review of stage 2.5b** (made in stage 3.4f-A; test cases RY1 to RY8; project rules):

| Rule | What the backend does |
| --- | --- |
| The end of a timed use at its full time (ST-R1) | A report with no block (the client cleared its request after its execution) that the world takes at the full time or later completes the use first, through the path of its timer, then ends it. An end before the time stays a release. |
| A cancel before a delayed press (ST-R2) | A 2473 that matches no use marks its reference with the sequence number of its carrier datagram (the datagrams of a client have one sequence for all channels). A request of that reference from an earlier datagram starts no use (`rep: interaction request of input N is older than the cancel of its client`). A request of a later datagram ends the mark; the mark ends 3 s after the cancel at the latest (the numbers have 16 bits). |
| The range of a service grant (ST-R3) | Each accepted position of a character with a grant tests the range of the bound of its source: out of the range, the grant ends (a walk back does not give it back). |
| A use of one time of a different source (ST-R4) | Each accepted use of a different source ends the grant, also a use of one time (AddLoreToJournal). |
| The slots of a template (ST-R5) | The loader refuses a template whose PositionInTheWorld, BuildableController, Ownership, or Vitals slot is the slot of a different component or of an Interact bound. |
| The tool (ST-R6 to ST-R8) | `tools/gamedata/interactions.py` stops (and keeps the old file) for a named placement with no position object with x, y, and z; for a concrete child asset of a PrefabSpawner with no path in the catalog (except the three assets of the retail data that no record of the catalog has: `ASSETS_NOT_IN_GAME`, the spawners Lotus_A, Lotus_B, Lotus_C, five uses); and it keys each placement with one form of its UUID (lower case), so two spellings at two places stop it. |

### 4.37 The projectile of a creature: the Projectile state, its flight, its hit on a character (live proof of stage 3.4f-A; static proof for the client code; the rules are project rules with the values of the game data)

Stage 3.4f-A. Research: `~/Games/soulwarden/work/research/creature-ranged.md` (Q2 with R0, Q3, Q5, the stage plan 3.4f-A, the live
checks L1 and L2). Live checks and the live proof: `~/Games/soulwarden/work/evidence/creature-ranged/implement/` (`live/LIVE-CHECKS.md`,
`RESULT.md`). Code: `server/rep/states/projectile_state.cpp`, `world/projectiles.h` (`Projectiles::launch_creature`, `CreatureShot`,
`creature_shot_rules`), `world/behavior.h` (the rule Shoot, `behavior_rules::kShot*`), `gamedata/creature_data.h` (`RangedRelease`,
`ProjectileSliceData`, `kBasicShotAction`), `tools/gamedata/creature_behavior.py`, `tools/gamedata/creature_data.py`. Documents:
[Projectiles](systems/projectiles.md), [Creature behavior](systems/creature-behavior.md). Test cases CRG1 to CRG6, CRG9.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The action of a shot (ALC state type 11, the scope record of a creature, 4.14) alone shows the windup of the creature (FireBow: the bow up, the draw, the release pose) and no arrow. | Live L1 (`live/l1-action-only.mp4`). | Apply `0x2A2BC20` |
| A projectile of the server is an entity of its own: the metadata state (slot 0: the asset of the projectile slice, sub id 2; `basic_ai_arrow` `6158a382-90e5-5f05-ba91-4fd8991c1c5c`, an actor of the allocator) and the Projectile state (`MB::ProjectileReplicatedState`, type 16) at the slot of the Projectile component of the slice (game data: 1 for `basic_ai_arrow`, 2 for `arcane_ai_arrow`). One group, twenty fields, chained masks. The client flies the arrow from the anchor with the velocity; one arrow shows, with its trail. The metadata and the Projectile state in one first record work. | Live L2b, L2f; static (research R0). | Constructor `0x6715370`, apply `0x683D750` -> `0x68D5B60`, `0x68D66F0` (first), `0x68D6CF0` (later) |
| The launch state: field 0 the anchor (x f32, y f32, z u16 over -100 to 2000), 2 the velocity (three half floats, m/s), 4 the time anchor (u64, the server time in microseconds: the world clock), 5 the start of the acceleration (u16 ms: 0), 9 the flags (0), 10 the owner (u64, the entity id of the creature), 11 the attack index (size code: the place of the attack in all attacks of all profiles of the ranged attack data, profiles and then attacks in the byte order of their names: `AI_Bow/Basic` 7, `AI_Bow/Basic_Arcane` 8), 13 the ammunition key (size code: the CRC-32 of the lower-case AmmoID; `arrow_ai` `869C80AA`; 0 with no ammunition), 14 the damage table key (u32: the CRC-32 of the lower-case unique name of the damage table; `HumanBowDamageTable` `910CFA8A`), 15 the row (u16: the place of the row among the rows with a DamageID; `Damned_2H_Ranged_Bow` 0). 51 bytes. The other fields keep their constructor values (no height delta, no gravity override, no bounce, no weapon effect, no pierce, no magnet). | Live L2e (the reduced state flies as the full state of 71 bytes, L2c); static R0. | Size code reader `0x87B5C0`, attack lookup `0x5C52550`, row cache `0x677CEC0` |
| The acceleration of the flight is the acceleration of the slice (`basic_ai_arrow` 0, 0, -2), not the predicted acceleration of the profile (-9.8): a slow arrow of 12 m/s falls 1.4 m in 0.95 s. The gravity override 0 is no override. | Live L2b. | Motion `0x695CCC0` |
| The collision state: fields 0 (the contact place), 4 (its time), 7 (the hit entity: `00000000FFFFFFFF`, none), 8 (the hit volume: 0), 9 (the flags: 0x01, collided). The client stops the arrow at the contact and its trail fades; with no such state the arrow flies on. | Live L2c, L2d. | `0x6781820` (the collided bit holds the anchor), remote tick `0x689A710` |
| The stop of the record in the flight ends the arrow at its place. | Live L2g. | — |
| The size code of the client: each tail of the two-, three-, four-, and five-byte forms is little-endian (the five-byte form reads one little-endian u32 and shifts it by 3). The fixtures of the research wrote the five-byte tail big-endian; `hub::Writer::size` and the live checks use little-endian. | Static (Ghidra read of `0x87B5C0` in this stage); live L2. | `0x87B5C0` |

**The data** (`creatures.json` of format 3 with three new members, `tools/gamedata/creatures.sh`): each slice can have `ranged` (the
ranged attacks: an action, its time, and its releases with their joins), the file has `projectiles` (each projectile slice: the asset,
the root, the slot of the Projectile component, the flight values) and `playerCapsule` (the capsule of the CharacterComponent of
`slices/player.dynamicslice`: radius 0.41 m, segment 0.8 m). The values of this build: 225 ranged attacks of 95 slices (459 releases),
69 projectile slices; the basic shot FireBow in 20 slices (the Evil Knight archer: index 7, `HumanBowDamageTable`, row 0, `Arrow_AI`,
`basic_ai_arrow` slot 1, release at 1.333 s; the Skeleton Archer: index 8, `SkeletonArcherDamageTable`, row 2, `Arcane_AI_Arrow`,
`arcane_ai_arrow` slot 2, 1.333 s; the Dryad Archer: index 7, `FeralArcherDamageTable`, row 0, `basic_ai_arrow_dryad`, 0.65 s). A file
of an older tool has none of the members: no creature shoots, and the backend works.

**The rules** (project rules with the values of the game data; the source of each rule is in [Creature behavior](systems/creature-behavior.md)
and [Projectiles](systems/projectiles.md)):

| Rule | What the backend does |
| --- | --- |
| The attack | The basic shot of an archer: the ranged attack of the action FireBow of the slice (one release, the ammunition of the slice). The other ranged attacks of the data wait. |
| The band and the choice | The melee attack wins in its reach; the shot in 20 m with a free line; a walk to 18 m from farther; a walk back to 14 m from nearer than 8 m on a path in the leash, one time in 6 s. |
| The release | At the time of the release of the data from the start of the action, when the action is still the action of the shot, the creature lives, and the target is there. The aim at the release: the middle of the capsule of the target, the lead with the velocity of the target (AILeadTarget, ProjectileSpeed, at most AILeadTargetMaxAngle), the fall of the slice, at most AIAimMaxAngle from the front. |
| The repeat | The next shot 3 s after the start of the last one, or at the end of its action when later. |
| The flight and the hit | On the world clock in steps of 1/30 s with the speed and the acceleration of the slice, the range of the attack, the life time of the slice. The first character on the segment (alive, not ignored; the capsule of the player slice, the radius of the attack) takes one damage result of the creature rule (`Health::creature_hit` with the row of the release). A wall or the ground first: no damage. |
| The record | One entity for each shot: the launch state for a session with no record of it (also in the flight: the time anchor of the release), the collision state at a contact, the stop 0.25 s after a hit, after the stuck life time of the slice (8 s) at a wall, at once at the end of the range or of the life time, and at the death or the leave of the creature (no damage). A session that comes near after the end gets no record. |
| The load | The flights look only at the characters near each segment and at the cells of the segment; 8 shots of a creature in the air at most (case CRG9: 200 flights, 50 archers, 1,000 ticks in 14 ms; no change with 32,000 far points). |

**The live proof** (stage 3.4f-A, a development run with the backend of the stage; `proof/` of the evidence folder; night light):

| Check | Result |
| --- | --- |
| P1 The hit (`p1-hit.mp4`) | A Varangian Archer (`Evil_Knight_Bow_17`, a kind of the start area: 8 spawn points 250 m to 500 m from the default spawn) at 10 m: the windup, the release 1.33 s after the start of the action, one arrow with its trail, the health of the character goes down only at the contact (65 damage for each hit, the HUD 4,869 to 4,804), the next shot 3.1 s later. |
| P2 The miss (`p2-miss.mp4`) | At 18 m the character goes to the side at the release: the arrow flies past it, 28.1 m, and the ground stops it. No damage. |
| P3 The wall (`p3-wall.mp4`) | A shot at a second character behind the brick wall: the arrow stops at the wall after 6.0 m and stays in it for some seconds. No damage. The archer then walks to find a free line. |
| P4 The view of a second character (`p4-observer.mp4`) | The development character looks at a shot at a different character: each arrow flies and stops at it, and its health bar gets shorter after each hit (four hits, 250.1 damage each). |
| P5 The arcane arrow (`p5-arcane.mp4`) | An Ancient Guardian Archer (`Skeleton_Archer:4`): `arcane_ai_arrow` at slot 2 flies with its purple trail; 393.0 damage of the type Arcane for each hit. |
| P6 The Dryad Archer (`p6-dryad.mp4`) | A Bronzeroot Archer (`Dryad_Archer_31`): the release 0.65 s after the start of the action, `basic_ai_arrow_dryad`; in the frames the arrow leaves at the end of the draw of the bow (frames of about 0.05 s to 0.15 s). |

**The commands** (development routes): `tools/world creature shoot CREATURE CHARACTER` (`POST /v1/dev/creatures/shoot`: the basic shot
now), the lines `creature shots` of `tools/world projectiles` and `tools/world list`.

**Not known, not done** (stage 3.4f-A):

- The impact effect at a character (the arrow stops at the character, and the stop of the record 0.25 s later removes it; no effect
  of a hit on the body shows in the night runs), the meaning of the contacted entity and of the hit volume (field 7, 8), the bits 0x02
  and 0x04 of the flags, the use of `m_extraSpeed` and `m_timeForMaxSpeed`.
- The other ranged attacks of the data (the charged and the fast shots, the shots of the casters), the dodge and the block of a
  character, the miss distribution of the clips (`AIMissMinDistance`, `AIMissMaxDistance`: kept, not used), the hostile statuses of a
  row (AI_Rend of the charged shot of the skeleton archer), a shot at a summon (a creature with no melee attack takes a summon as its
  target, turns to it, and does not attack it; it loses it at the end of the summon).
- The tags of the fragments of the Skeleton Archer and the Dryad Archer: the tool selects their FireBow variant with the default tags
  of the slice (`m_defaultTags`: Bow+Skeleton+Bowman, Bow+Dryad+Bowman); the backend sends the weapon tags (as for the melee attacks).
  The release time of the Dryad Archer (0.65 s) is a time of that variant; the live proof P6 shows no early or late arrow (a check to
  about 0.1 s, at night).

### 4.38 The stamina of a player, the dodge, and the cast of a creature (live proof of stage 3.4f-B; static proof for the client code; the rules are project rules with the values of the game data)

Stage 3.4f-B. Research: `~/Games/soulwarden/work/research/creature-ranged.md` (part B, task D, the live check L6). Live checks and the
live proof: `~/Games/soulwarden/work/evidence/creature-ranged-b/implement/` (`live/LIVE-CHECKS.md`, `RESULT.md`, `proof/`). Code:
`world/stamina.h`, `gamedata/stamina_data.h`, `server/rep/states/stamina_state.cpp`, `server/rep/states/stat_multiplier_state.cpp`,
`world/behavior.h` (the band of a caster), `gamedata/creature_data.h` (`kBasicCastAction`), `tools/gamedata/stamina.py`. Documents:
[Stamina and dodge](systems/stamina-and-dodge.md), [Creature behavior](systems/creature-behavior.md). Test cases SM1 to SM4, CRB1,
CRG9 to CRG13.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The Stamina state (`MB::StaminaComponentReplicatedState`, type 4297) at slot 42 of the player slice (component Stamina): one group, six fields, each a 32-bit float, big-endian: 0 cur, 1 max, 2 winded (the time that is left), 3 regen (the delay that is left), 4 multMax, 5 multRegen. The apply function stores the six values in the server copy of the component (+0xD0 to +0xE4) and multMax also in the multiplier of the maximum (+0xC0). It does not set the predicted value (+0xB0): a state with 20 of 100, or with 0, does not change the HUD or the dodge of the owner client. | Live LS2, LS3, LS8; static. | Constructor `0x671A230`, apply `0x683E5D0`, reader `0x2A42F30` |
| The message 473 (ForceSyncServerStateRemote) sets the predicted values only for a remote player: its handler returns for the local player. | Static. | Handler `0x67A69C0` |
| The client of the owner predicts its stamina: a dodge pays 45 at 0.1 s of its fragment and sets the delay of the regeneration to 1.0 s; at the mark CanTransition (0.5 s) the delay is 1.0 s again; then 1.111 in each client tick of 1/30 s (33.33 in one second). The value stops at 0 (no debt); at 0 the winded time is 2.0 s; a dodge needs a value above 0 (a dodge at 5 is accepted). A payment of cost 0 (a light attack of the great axe) does not stop the regeneration. The HUD reads the current and the maximum value of the component. | Live LS1, LS4 to LS7 (memory reads of the component). | Stamina change `0x6892480`, HUD `0x691F9B0` |
| The maximum of the client of the development character was 95 (also with no item equipped); the slice has `m_initMax` 100. Cause (stage 3.4f-C): the constructor of the component puts 95.0 into the predicted values (+0xB0, +0xB4), the server copy (+0xD0, +0xD4), and `m_initMax`; the init sets only the predicted values to `m_initMax` (100). The handler of each server movement state of the local client runs the reconcile of each rewindable: the reconcile of the stamina copies the server copy into the entry of the stamina history of that step, and a rewind copies the entry into the predicted values. A first record with no Stamina state left the server copy at 95/95: the live trace shows the predicted values 100/100 at the first reconcile and 95/95 at the second. The first record of the owner now has the Stamina state (100/100/0/0/1/1); the memory read gives 100/100 at +0xB0 and +0xD0, also after two dodges and the refill. | Live LS5, LS8; live trace and memory read of stage 3.4f-C (run `c-proof2`), live proof P3 of stage 3.4f-C; static (`STATIC-STAMINA-95.md` of the evidence of stage 3.4f-C). | Constructor `0x6719EB0`, init `0x6869E70`, reconcile `0x68AC2B0`, rewind `0x6917C70`, movement state handler `0x486D660`, GetMaxStamina `0x67F45A0` |
| The component reads the multipliers of its maximum and of its regeneration from the speed table of the owner (StatMultiplierTable, slot 43, 4.24): the keys 0x0B (StaminaRate) and 0x0C (MaxStamina); an absent key is 1.0. The backend sends 0x0B (1 + the StaminaRate of the statuses, times 10000) when it is not 1. | Static; test case SM3 (the bytes). | `0x6888790` |
| The dodge input: one input step with the bit 322 for each press of the dodge key, also for a press that the client refuses (the fourth press at 0 stamina). | Live LS9. | — |
| The cast of a Skeleton Mage: the action `Cast_One_Projectile` (ALC state, 4.14) shows the windup (the staff up with a purple light); the Projectile state of 4.37 with the slice `mage_projectile_slow` (asset `F7ECC33B-2C2D-5897-9492-049E8A30462A`, sub 2, slot 1), the attack index 125 (`AI_Mage_Sphere/Basic`), the ammunition key 0 (no ammunition: the slice override of the profile), the damage table key of `SkeletonMageDamageTable` (`CC5D9B49`), the row 0, and the speed 20 m/s flies one purple ball to the character; the collision state stops it there. | Live L6 (a probe), live proof P1 (the backend). | As 4.37 |

**The data.** `stamina-data.json` (format 1, `tools/gamedata/stamina.sh`): the Stamina component of `slices/player.dynamicslice`
(`m_min` 0, `m_initMax` 100, `m_regenRate` 33.33, `m_blockRegenRate` 10, `m_maxWindedTime` 2), BaseVitals_Player
(`StaminaRegenDelay` 1, `StaminaRegenDelayOnEmpty` 1, `StaminaFullyDepletedDelay` 1), the load factors of EncumbranceData (1 for
each class), the action Dodge of `playeractions_dodge.actionlist` (its fragment for each load class, its cost ids, the delay of its
exit 1 s; six conditional costs of a weapon rule of the hatchet and the spear that the backend does not apply), the action Exhausted
of `playeractions_global.actionlist` (from 0 to 100 percent), and the times of the dodge fragments of `player.adb` (pay 0.1 s,
immune 0 to 0.5 s, CanTransition 0.5 s, the windows CanDodgeCancel 0.267 s to 0.5 s buffered and from 0.5 s instant). `creatures.json`:
a slice with no ranged FireBow and a ranged `Cast_One_Projectile` has the cast as its basic shot (10 slices of 88 kinds: the Skeleton
Mage, the Ancient Guardian mage, the Dynasty summoner, the Dryad shaman, the Damned priest). `ability-data.json` format 6: the
hit-taken actions of the player lists (`actionHitStatuses`).

**The rules** (the source of each rule is in [Stamina and dodge](systems/stamina-and-dodge.md) and [Creature behavior](systems/creature-behavior.md)):

| Rule | What the backend does |
| --- | --- |
| The stamina | The value of the server with the rules of the client: payments, gains, the regeneration after its delay, no debt, winded and exhausted at 0. The owner gets the Stamina state as the record. |
| The dodge | A request for each step with the bit 322; the checks (alive, not locked, not overburdened, not exhausted, stamina above 0, a window of the playing dodge); the payment at 0.1 s; immune from 0 to 0.5 s of the world time of the input step. |
| A hit in a dodge | No damage, no mark, no effect; the passive rules with OnHitTakenWhileInvulnerable; a creature shot flies on and never hits that character again. |
| The caster | The cast as the basic shot; the band 6 m to 16 m (a walk into the band from farther, a walk back to 11 m from nearer than 6 m); the next cast 2.5 s after the start of the last one. |

**The live proof** (stage 3.4f-B, a development run with the backend of the stage, daylight; `proof/` of the evidence folder):

| Check | Result |
| --- | --- |
| P1 The cast (`p1-mage-hit.mp4`) | A Celestial Mage (`Skeleton_Mage:2`) at 10 m: the windup with a purple light, the release 0.5 s after the start of the action, one purple ball, the hit at its contact (56.3 Arcane damage), the next cast 2.67 s later. |
| P2 A dodge through the ball (`p2-dodge-ball.mp4`) | A dodge 0.7 s after the start of the cast: the ball reaches the body in the roll, the server avoids the hit at 0.26 s of the dodge (no damage), and the ball stops at a wall 1.5 m behind the character. The HUD bar goes from 99 to 52 percent and comes back. A first dodge took the character out of the line: no contact. |
| P3 A dodge through an arrow (`p3-dodge-arrow.mp4`) | A Varangian Archer at 10 m, a dodge 1.2 s after the start of each shot: two arrows avoided at 0.28 s of each dodge, no damage; one arrow flew on 18.5 m to a wall. |
| P4 The shield of Maelstrom (`p4-shield.mp4`) | Maelstrom with Absorb 1.0 s after the start of a shot: the shield takes the arrow (7.4 m, no damage); two later arrows in the cooldown of Maelstrom hit the character (65 damage each). |
| P5 The empty stamina (`p5-empty-regen.mp4`) | Four presses 0.73 s apart: the server pays 45, 45, 10 (100 to 0, exhausted), refuses the fourth press (exhausted), and its stamina is full about 4.4 s after the third payment (the regeneration starts 1 s after the mark CanTransition and needs 3 s for 100); the HUD bar empties, shows the red flash of the refused press, and is full at the same time. |

**Not known, not done** (stage 3.4f-B):

- The source of the maximum 95 of the client of the development character (no item, perk, affix, attribute row, or status of the data
  gives it). The server uses 100 of the data. Closed in stage 3.4f-C: the default of the server copy of the client component (row
  above).
- A red cloud near the line of the shot about 0.2 s after the avoided ball of P2 (an impact effect at the wall, or an effect of the
  client for a hit in the immune window: not proved).
- The observer view of the Stamina state (no fragment is sent), the MaxStamina key (0x0C: no source in this stage), the block, grit,
  the other casts of the data (a charged or a triple cast), the hostile statuses of a row, the heals and the buffs of a creature.

### 4.39 The ground areas of the creatures: the mushroom, the strike, the statuses of an area (live proof of stage 3.4f-C; static proof for the client code; the rules are project rules with the values of the game data)

Stage 3.4f-C. Research: `~/Games/soulwarden/work/research/creature-ranged.md` (Q4, part C). Live checks, static studies, and the live proof:
`~/Games/soulwarden/work/evidence/creature-areas/implement/` (`live/LIVE-CHECKS.md`, `static/STATIC-AREAS.md`,
`static/STATIC-CAST.md`, `static/STATIC-STAMINA-95.md`, `proof/`, `RESULT.md`). Code: `world/spells.h` (`make_area`, `area_step`,
`strike_step`), `world/behavior.h` (`think_area`, `area_place`), `world/statuses.h`, `world/health.h` (`status_damage`),
`gamedata/creature_data.h` (`CreatureAreaAttack`, `AreaCast`), `gamedata/spell_data.h` (format 4), `server/rep/states/spell_state.cpp`,
`tools/gamedata/spells.py` (`AreaSpells`), `tools/gamedata/creature_behavior.py`. Documents: [Spell entities](systems/spells.md),
[Creature behavior](systems/creature-behavior.md), [Status effects](systems/status-effects.md), [Combat and health](systems/combat-and-health.md).
Test cases CRA1 to CRA9.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The mushroom (`Dryad_Bomb`, slice `dryad_mushroom_bomb`, asset `377CDC5C-795E-58C4-A055-54A193D80FD7`, sub 2): the metadata state and the PositionInTheWorld state (type 13) at slot 1 are sufficient. The slice has no Spell component. The client plays the tell (`PoisonAOE_Tell`) from the record and the cloud (`PoisionAOE_cloud`) at its activation event (1 s); it ends the pictures at the Destroy event of the slice (26 s) by itself. | Live L8 (M1, M2, M5), live proof P1, P4. | — |
| The strike (`Undead_Shaman_Lightning_Strike`, slice `undead_shaman_lightning_strike`, asset `66BE4F91-CE70-5C93-A219-333ACD03770A`, sub 2): the metadata state and the Spell state (type 2912) at slot 1 with the key `crc32` of the lower-case SpellID, the place, the identity rotation, no attachment, the caster reference of the creature (field 7), the spawn time, count 0, radius scale 1. The client plays the omen, then the bolt at the activation of the entity `Damage` (0.9 s). No Ownership state and no TemporaryAffiliation state is necessary. | Live L8 (S1), live proof P2. | Spell facet activation `0x6877580` |
| A late observer of a status area: a passed activation of the cloud is completed; the client then shows the lasting cloud at once. With the hold of a burst (not completed) the late observer sees no cloud. A late observer of a strike: a passed activation waits (the hold); a completed one plays a second bolt. | Live L8 (M4a, M4b, S3a, S3b). | — |
| The status of an area on the owner: the status state of 4.24 with `AI_Poison_55` (ShowInUITray) shows the icon and its countdown above the health bar; the damage number of a tick has the DamageType of the row (Nature). | Live L8 (P1), live proof P1. | — |
| Four more spells of the same shapes show correctly with the same records: `Mage_Ranged_AOE` (status, slot 1; `AI_Snare_6sec`, `AI_Weaken`), `Inquisitor_Cleric_Holy_Pillar`, `corrupted_priest_ground_aoe`, `AI_Soulharvester_GeyserFinisher` (strike, slot 1). Their late observer is not checked live (the rule of the shape). | Live K1 to K4 (stage 3.4f-C, run `c-check4`). | — |

**The client code** (static; the server code of the area statuses is partly in the client image, the code of the spell facet of the
server is not):

| Fact | Client code (image RVA) | Evidence |
| --- | --- | --- |
| The clip CAGE-CastSpell calls `MagicComponent::CastSpell` with the clip values (AIAimAtTarget, ProjectileSpeed, AIMissMinDistance, AIMissMaxDistance, AILeadTargetMaxAngle, AIAimMaxAngle, AILeadTarget, UseAllAvailableTargets). With AIAimAtTarget and UseAllAvailableTargets: one spell at the aim point of each target of the AI. With AIAimAtTarget false: at the caster, or at the shot of the ranged attack data. | OnEnter `0x25509B0`, CastSpell `0x679DBB0`, aim point `0x5C35450` | Static (STATIC-CAST.md, 1.1 to 1.5) |
| The server facet of the spell and the code that makes the spell from the request are not in the client image (the creator slot is a stub). Thus the clamp of the place to CastDistance, the raycast down, CheckObstructions, and the end at the death of the caster are not in the client: they are project rules with the cells of the data. | Stub `0x16A1340`, `0x294860` | Static |
| The row flags: EndOnCasterDeath bit 0xA, ClearOnExit 0xD, ClearOnEnd 0xE, CheckObstructions 0x11 of the flags at +0x0C of the spell row. | Row reader `0x449EE20` | Static |
| A spell row with a shape gives the first DetectionVolumeComponent of the slice a query shape (Sphere: the Radius); a slice with no Spell component keeps its authored volume (the mushroom: the cylinder 1.5 m × 4 m). | `0x6877580`, `0x66633A0` | Static |
| The area statuses: the first application `m_initialDelaySeconds` after the entry, then each `m_reapplicationDelaySeconds`; `m_removeOnExit` takes them at the exit and cancels the next application; the enter handler makes no line test. | Entry `0x3FE5B40`, ApplyStatusEffects `0x3FE6500`, exit `0x3FF2830` | Static |
| The core damage of a status source: `(attribute part + 1 + level part) × base damage`, one time; for an attacker that is not a player the level part is 0; the area copies the damage of its caster into the source. The vitals test of a status: the damage-based part is `HealthModifierDamageBased × core damage`, plus Health. The tick of the server is not in the client. | `0x5C3C930`, `0x3FE6500`, `0x6892D40`, sign rule `0x449D740` | Static (STATIC-AREAS.md, Q1) |
| The damage receiver refuses an attack on an invulnerable target with no IgnoreInvulnerable (damage row bit 2) and no ForceAttackToHit. The status row has its own IgnoreInvulnerable (flag 49). The add test of a status has no invulnerability test. | `0x692E4A0`, `0x683F0C0`, loader `0x44B4430`, add test `0x401FB90` | Static (STATIC-AREAS.md, Q2) |
| The shield of Maelstrom is a body with the filter ProjectileOnly: it does not act on a detection volume. | Body builder `0x169F4F0` | Static and game data (STATIC-AREAS.md, Q4) |

**The data.** `spell-data.json` format 4 (`tools/gamedata/spells.sh`): for each detection volume its shape (sphere, cylinder, none),
its shape radius, its height, its offset above the root, its start and its end (the activation and the deactivation events of its
entity or of a parent); for each area status its initial delay and its reapply delay. `creatures.json` (`areas` of a slice): the area
attacks (6 spells of 9 slices, 70 kinds, 255 spawn points; the list `areaSpells` of `tools/gamedata/creature_slices.json` has the
spells whose record has a live check).

**The rules** (the source of each rule is in [Spell entities](systems/spells.md), [Creature behavior](systems/creature-behavior.md),
and [Status effects](systems/status-effects.md)):

| Rule | What the backend does |
| --- | --- |
| The cast | At the times of the CAGE-CastSpell events of the area action; one spell for each cast (one target); the place at the target with the lead and the angle limits, at most CastDistance away, on the floor. Cancels as for a release. |
| The choice | The area attack before the shot and the melee attack when it is ready: in the CastDistance with a free line; 10 s (5 s for a creature with only area attacks) after the last one; after the pause of the last attack. |
| The status area | Its volume from its activation; the statuses at the entry plus 0.5 s, then each reapply delay; the exit and the end take them; no line test. |
| The strike | One hit for each character in the sphere of the Radius of the row in the damage window (the creature hit rule; the dodge decides it); a wall only with CheckObstructions. |
| The poison tick | `(-HealthModifierPercent × maximum health - HealthModifierDamageBased × core damage) × potency × stacks`, no armor, no damage in the immune window of a dodge. |
| The ends | The life of the slice or of the row; the death of the caster with EndOnCasterDeath; the leave of the caster. |

**The live proof** (stage 3.4f-C, a development run with the backend of the stage; `proof/` of the evidence folder):

| Check | Result |
| --- | --- |
| P1 The mushroom (`p1-mushroom.mp4`) | A Dryad Shaman (level 57) casts `Shaman_Bomb`: three mushrooms at 1.2 s, 2.2 s, 3.13 s; the tell, the cloud at 1 s; the poison icon on the HUD; a tick of 242.3 Nature damage each 2 s; the exit ends the poison; the areas end at 26 s. |
| P2 The strike (`p2-strike.mp4`) | An Undead Shaman (level 16) casts each 5 s: the omen, the bolt at 0.9 s, one hit of 121.9 Lightning damage for each strike. The death of the shaman 0.4 s after a cast ends that strike before its bolt (EndOnCasterDeath): no bolt, no damage. |
| P3 The stamina (`p3-stamina-bar.png`) | The first record with the Stamina state: the bar in four equal parts (100), 10 of 100 after two dodges, 100 of 100 again 6 s later (memory read). |
| P4 A natural fight (`p4-fight.mp4`) | A Dryad Shaman (level 30) with no command: `Shaman_Bomb` (three mushrooms), shots, `AOE_Ranged_Spell` (one mushroom) 12.2 s later, shots, `Shaman_Bomb` again; ticks of 144.2. |

**Not known, not done** (stage 3.4f-C):

- The tick code of the original server: the sum of the terms, the potency, the stacks, and the armor of a tick are project rules.
- The count of the hits of a strike (one for each character) and the clamp to CastDistance: project rules.
- The other area spells of the data (the self-destructs, the shield of the officer, the shockwave, the casts with CastDistance 0):
  their new rules are not in this stage ([Spell entities](systems/spells.md)). Stage 3.4f-D (4.40) adds the shockwave and the three
  casts with CastDistance 0.
- The heals and the buffs of a creature (stage 3.4f-D, 4.40), the summons and the encounters (3.4f-E), the block, and grit.

### 4.40 The heals and the buffs of a creature, and the areas at the caster (live proof of stage 3.4f-D; static proof for the client code and the game data; the triggers are project rules)

Stage 3.4f-D. Research: `~/Games/soulwarden/work/research/creature-ranged.md` (Q4, part D). Live checks, static files, and the live
proof: `~/Games/soulwarden/work/evidence/creature-support/implement/` (`live/LIVE-NOTES.txt`, `static/`, `proof/`, `RESULT.md`). Code:
`world/creatures.h` (`give_status`, `heal`, the due list of the effects), `world/entity.h` (`CreatureEffect`, `Creature::damage_factor`,
`speed_factor`), `world/behavior.h` (`think_status`, `place_rule`, `area_range`), `gamedata/creature_data.h` (`CreatureStatusAction`,
`StatusMark`, `CreatureSlice::lowHealth`), `server/rep/states/spell_state.cpp` (the rotation), `tools/gamedata/creature_behavior.py`
(`read_status_actions`), `tools/gamedata/creature_data.py` (`marker_statuses`, the low-health part). Documents: [Creatures](systems/creatures.md),
[Creature behavior](systems/creature-behavior.md), [Spell entities](systems/spells.md). Test cases CRH1 to CRH9.

**The wire contracts** (no new state; live checks D-L1 to D-L3 with the backend of stage 3.4f-C, before the code):

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The heal of a creature is a vitals state (type 15) at the Vitals slot of its slice with the new HealthAmount and `healthChangeFlags` bit 0 (4.14): the bar of the name plate goes up, the same as it goes down. No heal number for an observer. | Live D-L1 (health steps of 4.5 percent by command), live proof P1 (8 ticks of 819.1 of a Cleric). | — |
| The icon of a heal or a buff of a creature is an entry of the compact map of the status state of the creature (4.22) for a row with ShowInNameplates: `AI_InqCleric_Rekindle` (a white plus with its countdown), `AI_Inquisitor_RampantFlame`, `AI_Evil_Knight_Wolf_Haste` (a runner). The end of the entry takes the icon. | Live D-L1, D-L2, live proof P1 to P3. | — |
| The actions of the status actions are scope records of the movement state (4.14): `InqCleric_Rekindle_Self` (a red glow on the Cleric), `Howl` (the head up and a white ring on the ground: the variant Evil_Knight). | Live D-L1, D-L2. | — |
| A strike at the caster (`Ghost_Plagued_PlagueNova`, `Legion_Signifer_CorruptedBlast`, `Undead_Officer_AnchorSlam`, `AI_EvilKnight_Shockwave`): the Spell state at slot 1 with the caster at the place of the caster, and the delayed event of the end of the window (0.5 s, 0.3 s, 0.3 s, 0.2 s). The orange cloud of the nova, the red and black blast, the dust of the slam, the golden wave. The same action with no record shows no cloud (a control). | Live D-L3 (probes), live proof P4, P5. | — |
| The rotation of a creature area: fields 2 and 4 of the Spell state (and field 1 of the PositionInTheWorld state) have the direction of the caster: the header `03` (x and y are 0) and the u16 of z, for (0, 0, sin(yaw/2), cos(yaw/2)) with w not negative. Before stage 3.4f-D each spell had the identity `07`. The shockwave of the Evil Knight is a wave in one direction. | Live D-L3 (the identity rotation sent the wave along the world axis), case CRH5. | Rotation reader `0x5CEA140` → `0x87B250`; the request rotation `0x676AAF0` |

**The client code and the game data** (static):

| Fact | Source | Evidence |
| --- | --- | --- |
| The place of an AI cast: with no ranged attack data in the row (RangedAttackProfile and RangedAttackName), the target loop of UseAllAvailableTargets keeps the place of the caster (the aim point `0x5C35450` runs only with ranged attack data); AIAimAtTarget false: the caster, or a shot of the ranged attack data (`0x5BFBAF0`). The rotation of the request is the rotation of the caster. The four spells of this stage have no ranged attack data; the six of stage 3.4f-C have `AI_Ranged_AOE`. | `0x679DBB0` (read again in stage 3.4f-D), `0x676AAF0` | Static (decompile of `0x679DBB0`; STATIC-CAST.md 1.4, 1.7) |
| The self heal of the Inquisitor Cleric: the action `InqCleric_Rekindle_Self` (action list `inquisitor_cleric_combat`) puts `AI_InqCleric_Rekindle` on the Cleric itself at the mark `Inquisitor_Cleric_Rekindle_Self` (a CAGE-Named event at 0.633 s of the fragment, `npc_damned_subadb_inquisitor_anims.adb`). The row: 8 s, TickRate 1, HealthModifierPercent 0.045, OnTickStatusEffect `AI_Inquisitor_RampantFlame` with KeepOnTickEffectOnEnd, StackMax 1, ShowInNameplates. The task of the behavior tree that starts the action is not in the game files. | Game data | Static (action list, ADB, status table) |
| The Howl of the Evil Knight hound: the action `Howl` (action list `wolf_combat`) puts `AI_Evil_Knight_Wolf_Haste` on the hound at the mark `ApplyHaste` (1.2 s, only the variant with the tag Evil_Knight of `npc_wolf_anims.adb`; the Grey Wolf plays the damage variant). The row: 10 s, DMG 0.1 of each damage type (DMGStandard 0.1), MoveSpeedMod 0.2, SprintSpeedMod 0.2, the category Buff, ShowInNameplates. | Game data | Static |
| `m_lowHealthPercentThreshold` is a field of the Vitals data of a slice (with `m_subscriberUpdateThrottleDuration`; the Cleric 0.15, the hound 0.85); the target curve `healally` has the input LowHealth (a test, with InCombat and TargetAllegiance). | Reflection `0x3B6F550`; `sharedassets/ai/targeting/healally.curveconfiglist` | Static (field read of the slices) |
| The dispel of a row: RemoveStatusEffects and RemoveStatusEffectCategories (`VoidGauntlet_NullChamber_Nullify_AoE`: Buff; `ResetAI`: ClearOnReset). | Game data | Static (status tables) |

**The rules** (the source of each rule is in [Creatures](systems/creatures.md) and [Creature behavior](systems/creature-behavior.md)):

| Rule | What the backend does |
| --- | --- |
| The status of a creature | One path (`Creatures::give_status`): the effect of the row until its end (the later end for the same row again), the heal of its ticks through `Creatures::heal`, the DMG<type> part of a hit of the creature, the strongest haste times the strongest slow, the icon of a row with ShowInNameplates, the dispel of the row; the effects end at the death of the creature. A due list of the ends and of the ticks. |
| The status action | Before the other attacks when ready: a heal at the low-health part of the slice or below it, a buff that the creature does not have; 20 s after the start of the last one; the statuses at the marks of the data, on the creature itself; a different action before the mark (a stun, a knockdown) gives none. Project rules: the behavior tree is not in the game files. |
| The place of a cast | The client rule of `0x679DBB0` with the clip and the row; at the caster: the Radius of the row is the distance of the cast; the rotation of the caster in the record. |

**The live proof** (stage 3.4f-D, `proof/` of the evidence folder):

| Check | Result |
| --- | --- |
| P1 A Cleric heals itself (`p1-cleric-heal.mp4`) | A Sentinel Cleric (level 68) at 12 percent in a fight starts `InqCleric_Rekindle_Self`; 8 ticks of 819.1 (4.5 percent of 18,203.1): 2,184 to 8,737; the icons of the heal and of RampantFlame; the heal icon goes at 8 s; Holy Pillar casts between. |
| P2 The Howl and its hits (`p2-hound-buff.mp4`) | A Vorongard Hound (level 25) howls when it takes the character as its target; the icon of the haste for 10 s; its bite does 32.9 with the buff and 29.9 after its end (1.1 times); it howls again 20.9 s after the first howl. |
| P3 The dispel (`p3-dispel.mp4`) | `VoidGauntlet_NullChamber_Nullify_AoE` (a command) ends the haste: the icon goes. |
| P4, P5 Areas at the caster (`p4-plague-nova.mp4`, `p5-shockwave.mp4`) | A Fenwallow Plaguegeist casts the nova at its place 3.2 m from the character: the orange cloud, 526.8 Nature damage; a Vorongard Crusher casts the shockwave at its place: 108.7 Strike damage. |

**Not known, not done** (stage 3.4f-D):

- The behavior tree of the original server (when a creature heals or buffs itself, and its repeat): project rules.
- The heal of a different creature (`InqCleric_Rekindle` with the beam and the Direct spell `Inquisitor_Cleric_Rekindle` at a friend of the
  AI, `InqCleric_Rekindle_Area`): the friends of a creature and the records of the beam are not checked; not in this stage.
- The other actions of the data that put statuses on their creature (523 actions of the creature action lists, `static/` of the
  evidence folder; for example the potion of the Damned villagers, which also sets the tag Frenzy of other attack variants): only the
  list `statusActions` (2 actions) has a live check.
- The self-destructs (`Ghost_Charred_SelfDestruct`, `Invasion_Bomber_SelfDestruct`): the death of the caster is a DamageSelf hit of the
  fragment, and the rewards of a creature that kills itself have no rule in the data or the client.

### 4.41 The ordinary chests: the open, its progress, the loot claim of a character (live proof of stage 3.5b.1; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage 3.5b.1. Research: `~/Games/soulwarden/work/research/chests-ground-loot.md` (Q1, Q2), `gathering-stage.md` (the Gathering state).
Live checks and the live proof: `~/Games/soulwarden/work/evidence/chests/implement/` (`LIVE-CHECKS.md`, `probe/`, `proof/`, `RESULT.md`).
Code: `world/chests.h`, `server/rep/states/chest_states.cpp`, `gamedata/chest_data.h`, `tools/gamedata/chests.py`. Document:
[Chests](systems/chests.md). Test cases LC1 to LC9.

**The wire contracts** (live checks L1 to L4, L8, L9; a probe backend and `tools/world fragment` before the code):

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The open of a chest is the interaction request of 4.35: the input block of input 1589 with the actor of the chest (the UUID rule of the placement), G and the facet target of its bound (the XOR rule), action 14, base 14, the option GatherOption, committed. The reply 3743 result 1 alone makes the character kneel and shows no progress. | Live L1 (the supply chest `Abandoned_T1_S`, the values of the interaction data). | Request builder `0x4918560`, consumer `0x4910750`, validation apply `0x49175F0` |
| The Gathering state (type 81, player slot 36, owner only), group 1: status u8 (0 None, 1 Success, 2 Failure, 3 Done), remaining f32, speed f32, count u8, reference u32, water reference u32, active bool, G u64 of the target (`0x00000000FFFFFFFF`: none), sync reference u32. The client applies fields 0 to 3 only at a new reference (field 4), and fields 6 and 7 only at a new sync reference (field 8). Success with remaining T and speed 1 shows the progress ring of the prompt for T seconds; the ring time is remaining / speed (speed 2.5 made 2.5 s run in about 0.8 s). Done makes the client stand up and send 2473 at once. After Failure the next E starts a new use with a full ring. | Live L3, L4. | Ctor `0x4904A90`, apply `0x4905130`, `0x48FF4D0`; status enum `0x49024D0` |
| The LootTracker state (type 982, player slot 33, owner only), field 0: a map in its full form: changes size 0, version present u8 0, count size, and for each entry 58 bytes: actor 16, state u32, kind u32, resetTime u64, lootedTime u64, privileged u8, x, y, z f32, tag u32, selector u8. The states (InstancedLootState): 0 Initial, 1 Rolled, 2 Looted, 3 RegeneratedForMission, 4 NotOnRequiredQuest, 5 Locked, 6 Unlocked. Initial, Rolled, Unlocked: the prompt shows. Looted: the prompt goes at once, and the client sends 2473 at once during a use. Locked and NotOnRequiredQuest: the prompt goes (an unmet gate). The map with no entry gives the prompt back at once; Initial after Looted only after a new focus. A map with two entries works. | Live L2, L9. | Readers `0x3727A00`, `0x3727C60`, apply `0x3759360`, compare `0x37760A0`; InstancedLootState reflection `0x36FD370` |
| The client does not end a claim at its resetTime (the raw value in server microseconds, nanoseconds, or Unix seconds, milliseconds, microseconds, nanoseconds: the prompt stays away after the time). The server ends a claim with the map with no entry. | Live L8. | — |
| The loot notice: an item change of the bag with the cause 1 (ItemGathered, 4.21) shows "+N NAME" with its icon. | Live L3, run 3 ("+11 Pure Solvent"). | — |
| The GatherableController state (slot 3, type 12) of the chest object changes nothing for an instanced chest (Depleted: the prompt stays). | Live L2. | Apply `0x3371590` returns when the instanced reference is valid |
| An elite chest: a 5 s ring, the celebration effect of the depletion, the loot list of the HUD for more items ("3 More Items"), and after the Looted claim the prompt "X ELITE SUPPLY CHEST" with no light of the loot. | Live L9 (`ChestEliteT4`, Brightwood). | — |

**The game data** (static; `chests.py`, `loot_tables.py`):

| Fact | Source |
| --- | --- |
| A chest: a placement with a variant name of `Gatherable_LootContainers` (the slice `master_lootcontainer`: Variation 1, PositionInTheWorld 2, GatherableController 3, Interact 4, Marker 5); the row names GatherableEntryID and LootTableComponent (the root). | `javelindata_variations_gatherables_lootcontainers.datasheet`, the layer capitals |
| The gatherable row: BaseGatherTime (2.5 s supply, 5 s elite), MinRespawnRate and MaxRespawnRate (82,800 s for `LootLargeT3ElitePOI`, 0 for a supply chest), the restrictions. | GatherableData tables |
| The reset of an instanced chest 60 minutes and its count 60 (`Instanced Loot Chest Reset Time Mins`, `Max Instanced Loot Chest Count`). | `playerbaseattributes.pbadb` (reflection `0x5CAD110`) |
| The territories of a chest: the record of the layer metadata (GatherableLocations) 0.06 m or less from the placement whose GatherableId is the CRC-32 of the gatherable row of the variation (stage 3.5b.2, finding CH-R7: 48 lock chests had a different record first); LootTags and AIVariantLevelOverride of the TerritoryDefinition rows of each table of this type (stage 3.5b.2, finding CH-R6: the records name the points of interest of `pointofinterestdefinitions/` and the arenas; with only the two tables 5,092 references had no row, and 1,228 chests had no LootTags of their point of interest, for example `EPOI`, `Corrupted`, `Elite`). | `.metadata` of the layers, the TerritoryDefinition tables (`javelindata_territorydefinitions`, `javelindata_areadefinitions`, `pointofinterestdefinitions/javelindata_poidefinitions_*`, `arenas/javelindata_arenadefinitions`, ...) |
| The container floor of the gear score: ContainerLootGSFloor of each level. | VitalsLevels |
| The action Gather ends at a hit (IfDamagedByPowerLevel 0 or more); it has no condition of the weight of the bag. | `playeractions_gather.actionlist` |

**The rules** (the source of each rule is in [Chests](systems/chests.md)):

| Rule | What the backend does |
| --- | --- |
| The open | GatherOption of an ordinary chest with no Looted claim of the character: the Gathering state Success with the BaseGatherTime of the data and the speed 1; the timed use of the base with the same time (stage 3.5b.2, finding CH-R2: a release at or after the time completes first). A release, 2473, a walk out of the range, a travel, the death, or damage before the end: Failure and no item. |
| The completion | The base checks the use again; the roll of the root of the chest (the level of the character, the content level and the loot tags of its territories, the container floor); one transaction: the items (cause 1) and the Looted claim with its reset; the Gathering state Done; the LootTracker of the owner with the claim. |
| A full bag | The whole result must fit (500 slots), and the catalog must have each item (stage 3.5b.2, finding CH-R3): else no item, the roll stays in a Rolled claim, the chest stays closed, 3746; the next open gives the kept roll. No weight check (the client has none for the action). |
| The reset | The reset values of the gatherable row, else 60 minutes; on the wall clock in the store (a restart keeps it); the server removes the claim and sends the map with no entry. |
| The map | The claims 192 m or less from the character and a Locked entry for each unavailable chest 192 m or less from it; made again at a new cell of 64 m. Each active claim stays in the store also above 60. |

**The live proof** (stage 3.5b.1, `proof/` of the evidence folder, the backend of the stage and the game data of the stage):

| Check | Result |
| --- | --- |
| P1 The open of a supply chest (`p1-supply-open.mp4`, daylight) | The supply cache `Abandoned_T1_S` (Everfall): "E SUPPLY CACHE", the ring for 2.5 s, the notices "+8 Obsidian Sandpaper" and "+12 Wireweave", then no prompt. The client sent 2473 9 ms after the completion. |
| P2 The items in the bag (`p2-bag-tooltip-wireweave.png`) | The reagents of the bag; the tooltip "Wireweave, Uncommon, Tier 5, Source: Supply Containers". |
| P3 A second open (`a-02`, `a-04`) | The looted elite chest shows "X ELITE SUPPLY CHEST", and E sends no request. With a probe map that gives the E prompt back, the request gets 3743 result 2 ("the character has a claim on the chest"). |
| P4 A restart (`a-01`, `f-00`) | The elite claim of an earlier backend and the supply claim stay after a restart of the backend and of the client (the LootTracker of the first record); the override of the reset (20 s) gives the prompt back at its time (`p4-reset.mp4`). |
| P5 A second owner (`p5-second-owner.mp4`) | A second account and character open the same chest during the claim of the first one: "+11 Wireweave". |
| P6 An unmet gate (`e-01`, `e-02`) | The quest chest `MSQ_18018_WerebearChest` with the Locked entry: no prompt and no light of the loot; a probe map with no entry gives the light of the loot. A chest behind a glyph gate shows "X" also with no entry (the client). |
| P7 A cancel | A walk 1.2 s after E: 2473, no item, no claim. |

**Not known, not done** (stage 3.5b.1):

- The rule of the original server for a full bag (the client has no notice for it): a project rule.
- The rule of the original server for more than 60 claims: no client eviction was found; a project rule.
- The loot limits (LootLimits), HWMMult, and LootBiasing of the roller; the umbral currency and the territory standing of the elite event.
- The personal loot bag of a creature (AILoot, stage 3.5b.2); the locks, keys, quests, and events (stage 3.5b.3); the gathering nodes
  (stage 3.6).

### 4.42 The personal loot bags: the bag of a kill and the discard of a player (live proof of stage 3.5b.2; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage 3.5b.2. Research: `~/Games/soulwarden/work/research/chests-ground-loot.md` (Q3, Q4, the criteria I9 to I15), `storage.md` (the
GlobalStorage map). Live checks and the live proof: `~/Games/soulwarden/work/evidence/loot-bags/implement/` (`LIVE-CHECKS.md`, `probe/`,
`proof/`, `final/`, `RESULT.md`). Code: `world/loot_bags.h`, `server/rep/states/loot_bag_states.cpp`, `server/rep/item_requests.h`,
`tools/gamedata/interactions.py`. Document: [Loot bags](systems/loot-bags.md). Test cases GL1 to GL7.

**The wire contracts** (live checks L5 to L7: a probe backend and `tools/world fragment` before the code):

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The record of a bag: the metadata state of `slices/instanced_loot_container.dynamicslice` (asset 9844F2DE-C3B1-5BD3-98FA-8768B5445906, sub 2), PositionInTheWorld (slot 1), Interact (slot 5: component `2953d2fecfa764bd` on the entity of the slice; TakeAllOption action 28 and OpenStorageOption action 26, committed, on the option entity Storage), LootDrop (slot 6). With no LootTracker entry of the character nothing shows. | Live L5. | — |
| The LootTracker entry of a bag (slot 33, type 982, the 58-byte entry of 4.41): Rolled (1) and the kind AILoot (1) or Discarded (5) with the place show the bag and its light beam; Looted (2) hides the prompt and the beam at once (+0.33 s), a dissolve shows (+0.9 s), and the client ends its use (2473, 7 ms). | Live L5, L6. | InstancedLootType reflection `0x36FD750`, InstancedLootState `0x36FD370` |
| LootDrop (type 2027), group 0 field 0 m_rarityLevel u32: the rarity of the light beam (the particle of the slice). | Live L5 (2: the beam). | Ctor `0x2D356F0`, apply `0x2D3F460`, reader `0x17B3620`; particle selection `0x36F6BC0` |
| GlobalStorage (type 2938, player slot 77, owner only), field 0 m_globalItemMap in the full form: changes size 0, version u8 0, count size, and for each key: actor 16, item count size, the items (the item of 4.10). The key of a bag gives the item list under its prompt ("Iron Ore 9"); the panel does not read it. | Live L5. | Ctor `0x2CF3C30`, apply `0x2D09280`, reader `0x2D1B110` |
| The selected Container (type 1755, player slot 76, owner only; the full vector of 4.10): the panel of the open bag ("3 items", Take All on). The open clears it in the client: a state before the request is not in the panel. | Live L5, L6. | Container reader `0x40E8F30` |
| OpenStorageOption (T, or Tab near a bag: OpenNearbyLootContainers) is the interaction request of 4.35 (action 26); the panel opens at once. Escape closes it with 2473. | Live L5. | — |
| 2939 TakeAllItems (48 bytes): the call (a RootPlayer component), the group, and the reference actor 16, group u64, target u64 of the player Container (`GlobalStorage/91b8850b-3bdf-1d1f-16bb-4b7cb8ddc4e1`): the reference names the player Container, not the bag. 2942 TakeItem (68 bytes): the same, the instance 16, the quantity u32 (LSHIFT and a click: the stack; LCTRL: the split slider). 2801 GiveItem (68 bytes): a drag of a bag item into the panel. A right click or a drag out of the panel sends nothing. | Live L5. | Field readers `0x2E6AB10`, `0x2D93D60`, `0x2D93E30` (the quantity 1 to 10,000), `0x692CF20` |
| E (Take All of the prompt) sends TakeAllOption (action 28) and no 2939; the client waits for the server. | Live L5. | — |
| The client does not end a bag at KillSelfDelay (180 s) of the slice, and a DelayedEvent state (slot 2, type 3139) has no visible effect. The stop of the record removes the bag at once; with the panel open the use ends (3746), the panel closes, and a late 2473 comes. | Live L6. | — |
| 1838 of a drag of a stack to "Drop items" and 826 (24 bytes: paperdoll slot u32, quantity u32) of a drag of an equipped item (4.10). With no answer the client keeps the equipped item. | Live L7. | — |

**The game data** (static; `interactions.py` format 3, member `lootContainers.personal`):

| Fact | Source |
| --- | --- |
| The personal container: the slice `instanced_loot_container` (InstancedLootContainer: AILoot, m_isInstancedLoot; Storage unique for a player; LootDrop slot 6; 5 rarity particles). | The slice of the game copy |
| The lifetime: a DelayedEvent with KillSelfDelay 180 s at the init; FastKillSelfDelay 1.25 s at completelyLooted (the end of the slice); HideMeshDelay 1.5 s at looted. | The slice (DelayedEvent slots 2, 3, 4) |
| The rarity of an item from its perk count. | `playerbaseattributes.pbadb`, client `0x5C528D0` (research itemization.md K6) |

**The rules** (the source of each rule is in [Loot bags](systems/loot-bags.md)):

| Rule | What the backend does |
| --- | --- |
| The bag of a kill | The items of each character with the credit go into its own part of one bag at the place of the creature; the experience, the mastery, and the coin at the kill. Only the eligible characters observe the bag. |
| The pickup | TakeAllOption, 2939, and 2942 move the stored instances (the id, the gear score, the durability, the flags, the perks) into the bag of the character in one transaction; a full bag takes what fits. 2801 moves an item of the bag of the character into the open bag. |
| The discard | 1838 and 826: a new Discarded bag at the feet of the character with the instances (a locked item does not move). |
| The lifetime | 180 s from the bag, and 1.25 s after the last item of the last part; on the wall clock in the store. The server removes the record. |
| The corrections of stage 2.2 (code review of stage 3.5b.2) | An item with Nonremovable or NotDroppable of the item table stays with the character (1838, 826, 2801; the client ground drop target refuses nonremovable equipment, research chests-ground-loot.md, static). A part has 500 stacks at most (the Container reader, static: research storage.md V17). The GlobalStorage map is one fragment below 256,000 bytes of one bundle (client `0x646C2B0`, static): the encoder writes whole keys in the sequence of the selection (the open bag, then the nearest bags) and stops before the key that makes the map field larger than 255,000 bytes; no live run sent such a large map. The fast end is in the store. A request at either end changes nothing. Test cases RLB1 to RLB10. |

**The live proof** (stage 3.5b.2, the backend of the stage and the game data of the run):

| Check | Result |
| --- | --- |
| B1 A kill, the bag, the panel, Take All, the inventory (`final/kill-bag-panel-takeall.mp4`, `final/kill-video-sheet.png`) | A Grey Wolf: the bag with the green beam, T: the panel "LOOT 1 item", Take All: 2939 moved 1 item, the item in the inventory, the bag ended 1.3 s later. |
| B2 A take of one item (`proof/t-00` to `t-05`) | A give (2801) of an ore stack into the bag, then LSHIFT and a click: the stack came back, the musket stayed in the bag; then Take All. |
| B3 The end of a bag (`proof/frames-end/`, `end-sheet.png`) | The bag with no pickup: the beam is gone 180 s after the bag (between -0.22 s and +1.42 s of the time). |
| B4 A discard and its recovery (`proof/frames-discard/`, `discard-sheet.png`) | The bound Miner Goggles into "Drop items": a Discarded bag at the feet, the panel, the take: the same instance id, gear score 75, durability 750, the bind flag. |

**Not known, not done** (stage 3.5b.2):

- The rule of the original server for the rarity of a bag of two parts and for the ForceRarity column: a project rule (rarity 0; the
  perk count).
- The shared bag of a death, the permissions of a group, and the trade between players (decision of the user: not in this part).
- The meaning of the panel label ("LOOT", "DROPPED ITEMS", "ITEMS") for each kind and order of the states: only the observed cases.

### 4.43 The world time: the request, the record, the phase, the clock correction (live proof of stage 2.2; static proof for the client code; the cycle is the default of the client, the start and the store are decisions of the orchestrator)

The world has one day and night cycle ([World time and weather](systems/world-time-and-weather.md)). The client calculates the
hour itself from one record and its copy of the server clock: the server sends no hour stream. The research is
`~/Games/soulwarden/work/research/world-time.md` (section 2) and `world-time-stage.md` (sections 3 to 9).

**The messages.** All numbers of a body are big-endian. The Time component (`RootPlayer`, component D41EB32C0F154004) has no
replicated state and no slot; its facet target is G + D41EB32C0F154004 (the rule of 4.2 for the root entity). The remote player
slice has no Time component.

| Type | Direction | Body | Client code | Evidence |
| ---: | --- | --- | --- | --- |
| 4248 `TimeComponentServerFacet_SyncToClient` | client to server | target:u64 entity:u64 (16 bytes) | callback 0x4087CE0, 0x408C2B0, sender 0x407A3C0 | [S], live L0: one request for each world entry, after the mission load of the world (0x6BDD9B0) |
| 7036 `TimeComponentClientFacet_SyncTimeAndPhase` | server to client | target:u64 entity:u64 record:60 phase:u32 (80 bytes; frame `54 00 01 bc 6d`, 85 bytes) | reader 0x406F740, 0x69F2530; handler 0x408C520, 0x408C550, 0x644ECC0, 0x408C490 | [S], live L1, L2, L4 |
| 4247 `TimeComponentClientFacet_SyncDayPhase` | server to client | target:u64 entity:u64 phase:u32 | handler 0x408C490 (component +0xE0) | [S], live L4, L6: the stored phase 1, then 2 at 06:26 |
| 335 `REPClient::TimeSynchMsg` | server to client | serverTime:u64 (nanoseconds) | dispatcher 0x6B6F190, 0x6AFEDB0 | [S], live L8 |

```text
record := a:f32 b:f32 offset:f32 dawnTime:u32 dawnDuration:u32 duskTime:u32 duskDuration:u32 dayLength:u32 nightLength:u32
          anchor:u64 dawnStart:f32 dayStart:f32 duskStart:f32 nightStart:f32                          60 bytes
backend:  -1      -1    (below)    21600          60              68400         60              1800          900
          (below)   0.18181818    0.20404040    0.82626263    0.84848485
```

- The anchor and the offset (client code 0x6A0ADC0). `anchor = max(1, now - cycle)`, `offset = ((position - (now - anchor)) mod
  cycle) / (365 * cycle)`, with `now` the server clock (`WorldClock::server_time_ns`, the clock of the registration reply and of
  335). The client adds the float32 time from the anchor and `offset` times 365 cycles, and it takes the remainder of the cycle. The
  float error is below 1 ms for two cycles (test case WT1: 5,820 compares, the largest error 0.93 ms). An anchor of 0 or later than
  the clock of the client is not correct (research 2.1). The session sends a new record one cycle after the last one.
- The four edges. `dawnStart` = (dawnTime / night game seconds x nightLength) / cycle and `nightStart` = that + dayLength / cycle:
  the hour formula of the client (0x6A09E60) needs these values. `dayStart` and `duskStart` select only the phase (0x6A08270): the
  backend sends 60 s after `dawnStart` and 60 s before `nightStart` (decision D4).
- The vector of the research (G 0x1000, server time 1e14, noon): `d41eb32c0f155004 0000000000001000 bf800000 bf800000 3aafc8ad
  00005460 0000003c 00010b30 0000003c 00000708 00000384 0000587e6befc800 3e3a2e8c 3e50eff8 3f5385f3 3f59364e 00000002` (test case
  WT1).
- What the client does with a record (live L1, run 1 and 3): the handler keeps the record (component +0x98, game +0x730), sets the
  engine hour at once, and sets the engine dawn and dusk hours to 6.0, 6.3667, 19.0, 19.3667 (the mission values before the record:
  5.9167 and 18.0833). Two reads of the hour some seconds apart gave 25.9 to 26.2 game seconds in one real second by day and 43.9
  to 44.1 by night (live L3), and the hour agreed with the model of the client within 0.005 hours (the time of the read).
- With no record the hour is constant (research 2.7): 0.0 after a character creation (`TimeStart` of the mission: a dark world)
  and 16.7 after a menu login in the runs of this stage (live L0). The pause flag, the override flag, and the editor byte were 0.
  The setting byte of `javelin.enable-time-comp-sync-to-local-client` (image RVA 0x9EFF37C) read 0, and the bus path 0x408C3A0 had
  no hit: the record comes only from 7036.
- The entry paths (live L2): a short travel (22 m), a far travel (634 m, with the loading screen), and a respawn keep the record
  and the motion of the hour, with no new request. A leave to the menu and a new world entry (a new session) and the first entry
  after a creation load the level again: the record is gone (pointer 0, the mission hour), and the client asks again with the ids
  of the new session. Each path had a noon and a midnight picture with a nonzero offset at noon.
- The jump (live L4): a new record changes the sky in one frame; the exposure of the renderer adapts for about 1.5 s.
- 335 (live L8): with the server clock, the client corrected its clock by 82 ms (it ran faster than the backend clock); +0.5 s
  went to the slow correction (steps of 2 ms); +5 s moved the clock at once, and the correct value moved it back. The backend
  sends 335 each 3 s from 3 s after the registration reply (the server sender of the image, 0x6A3D9B0: `r8d=0xBB8`).
- The checks of a request (rule MP-5): a session in the world, 16 body bytes, the actor route of the own character, its entity
  field, and the target of its Time facet. A request of an observed remote player, of a different facet, or with a different body
  size gets no reply (test case WT3).

**The weather.** No message. The level has local volumes (research 4.3 to 4.5): the client selects them by the position of the
camera. Live L7 at noon of the world: the rain volume of Weaver's Fen (12098.8 5167.4 40.4) shows storm clouds and rain, and its
sky blends in about 10 s after a travel into it; 61 m outside it the swamp sky has no rain. The snowstorm preset of Great Cleave
(8963.4 8155.9 541.0) gives a cold blue light with some snow. In the Dunwood keep interior (3249.6 5904.6 243.4) the engine hour
is 0.0 (the TimeOfDayOverride of the volume) while the world is at 12:13; outside it the hour is the world hour.

**The creature points with a time constraint.** No message: the spawn rule of the server (4.14) and the normal record and stop of
a creature. Live L6 at the night-dawn-dusk point of Brightwood Isle (9555.1748 5517.1914): the Ghost "Alchemical Experiment" is
there at 00:40 and at 06:20, and it and nine more ghosts of such points leave at noon and at 06:26:00 (the natural edge from dawn
to day), with no death and no reward.

**Not known, not done.** The fields `a` and `b` of the record (no reader found); the PlayerTime component (fields 0 to 2, 4.24
has the wall clock pair); the level-information u64 (it stays 0); a stop of the cycle of the client (no wire field); the pause of
the time of day in an instance (`PauseTimeOfDay` of 15 game-mode maps; stage 7.1); the weather of an event (the stages of the
events).

### 4.44 The gathering: the natural nodes, the node state, the work, the skinning (live proof of stage 3.6a; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage 3.6a. Research: `~/Games/soulwarden/work/research/gathering-stage.md` (Q1 to Q3, Q5, the criteria I1 to I10). Live checks and
the live proof: `~/Games/soulwarden/work/evidence/gathering/implement/` (`LIVE-CHECKS.md`, `probe/`, `RESULT.md`, the pictures and
videos). Code: `world/gathering.h`, `server/rep/states/gatherable_state.cpp`, `gamedata/gathering_data.h`, `tools/gamedata/gathering.py`.
Document: [Gathering](systems/gathering.md). Test cases GAT1 to GAT10.

**The wire contracts** (live checks L2 to L8: a probe backend, `tools/world fragment`, and `tools/world probe` before the code; the
stage run S1 with the backend of the stage):

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The identity of a natural node: the client gives each record of a `region.distribution` file the offline id G = ((counter & 0x7FFFF) << 13) \| ((regionY & 0x1F) << 8) \| ((regionX & 0x1F) << 3) \| 1 (the setting `javelin.offline-id-version` not 1, the live client); the counter is 1 for record 0 and counts the primary records, then the two tails. The actor of the node is G little-endian and 8 zero bytes. It is not the packed position id of 0x6160B60 (the research). | Live L2: seven nodes of r_+04_+02 (record 18627 G 0x09188221, 18628 0x0918A221, 18614 0x0916E221, 18613 0x0916C221, 18615 0x09170221, 18596 0x0914A221, 18598 0x0914E221), each the node of the focus. Test case GAT2. | Offline id `0x69D5090`, called by the record converter `0x6A01D50` for the three record arrays of a region |
| The request of a gather is the interaction request of 4.35: the actor and G of the node, the facet target of its Interact bound (the XOR rule of 4.2: K = root XOR G, instance = entity of the bound XOR K, target = instance + component), action 14, base 14, the option GatherOption (CRC `a6e4c280`), the option entity, committed. A capital node has the UUID rule of a placement (Broccoli_a `9b81ddc9-...`: G 0x1A5D3DBA0DAC5F2B). | Live L2 (target `b0b2beac0f9664ab` of record 18627: master_minerals entity `0x00530130DB40C5A5` XOR K = `0x0000000609188221`, + component `B0B2BEA6067DE28A`). Test cases GAT2, GAT3. | Request builder `0x4918560` |
| The capacity of a bound (MaxInteractors) does not apply to GatherResource (action 14): two characters can use one node. | Static. Test case GAT8. | Capacity check of `0x35C2E60` at `0x35C3858` |
| GatherableControllerReplicatedState (type 12, the slot of the GatherableController of the slice: 3 for the master slices of the resources and for most creatures), group 0: field 0 gatherableState u8 (Healthy 4, Depleted 8); field 1 replenishTime u64 (not sent). Depleted on the managed actor of a node hides the node and its marker; Healthy shows the same node again. The client does not restore a node at replenishTime (now + 15 s in the server clock of 335 and in Unix milliseconds: no change after 22 s): the server sends Healthy at the respawn. | Live L2, L5 (`L2-12`, `L5-02`, `L5-05`); stage run S1 (a stone, 74 s). Test case GAT2. | Ctor `0x5B9EA90`, readers `0x17B3C60`, `0x87A190`, enum `0x3378D60`, apply `0x3371590` |
| The Gathering state (type 81, slot 36, owner only; the layout of 4.41): Success, the remaining amount (field 1), the speed (field 2), the count 1, the G of the node (field 7): the ring of the prompt runs remaining / speed seconds, and the character kneels (a stone, a bush) or swings the tool (a tree: wood chips at each impact). Done ends the use of the client (2473 after 7 to 40 ms). Failure ends a use; None with active false does not (the character kneels, the ring stays). | Live L3, L4; stage run S1. Test cases GAT2, GAT4. | Ctor `0x4904A90`, apply `0x4905130`, status enum `0x49024D0` |
| A row with the restriction ItemRequired and no item (BushSmall, BushWithered): the client shows the prompt "E BUSH" and sends the request (no gate of the client). | Stage run F (`F-01-bush-small.png`, the log of the request of record 18708). | — |
| The tool of a node is in the paperdoll: 33 the logging axe, 34 the sickle, 35 the skinning knife, 36 the pick. A tool in the bag does not count: the prompt says "YOUNG TREE  No equipped Axe" ("BOULDER  No equipped Pick" with no pick), and E sends nothing. With the axe in slot 33 the prompt is "E YOUNG TREE" after a new focus. | Live L6 (`L6-01` to `L6-05`); stage run F (`F-03-refusal-no-pick.png`). Test case GAT3. | `0x48FF240` (FindFirstGatheringTool of the paperdoll) |
| The "Gather Speed" of the tooltip of a tool instance is MinGatherEFF to MaxGatherEFF of its weapon row, linear over the gear score range of the item: 2hAxeT2 200 % at 200, 350 % at 300; 2hAxeT5 625 % at 500, 738 % at 550, 850 % at 600. The rank part (GatheringEfficiency) shows in the trade skill window as "N %" (`tradeskills_gathering.lua`). No client code composes the parts. | Live L7 (`L7-03`, `L7-06`). | — |
| A tree falls at its depletion with the backend of the stage (the cut trunk falls, the stump stays), also at a second depletion of the same node on a record that the client had for minutes. The probe of L8 (Depleted by `tools/world fragment` on a probe record with the managed actor) gave no fall in six variants. Static: the exit events of the old state (the Healthy exit triggers the spawner Spawn_CutTrunk of master_tree) fire only with the transition flag of 0x3369430 (a facet time +0x2C8, an optional value at +0x2D0/+0x2D1); the difference between the probe and the backend is not known. | Live L8 (`L8-*`, `video-L8*.mp4`); stage runs S1 (`video-S1-tree-falls.mp4`) and F (`video-F-tree-falls.mp4`, generation 2). | Apply `0x3371590`, transition `0x3369430`, facet activation `0x3375830` |
| The skinning of a body: the GatherableController of the creature slice (slot 3) Healthy at the death, and the Interact bound GatherBound (slot 20) with GatherOption: with the knife in slot 35 the prompt "E CHALKY RABBIT" shows on the body, and the gather works ("+32 Tracking & Skinning", "+2 Rawhide", "+1 Game Meat"). The probe of L8 (the state on a body of an earlier record) gave no prompt. | Live L8 (open in the probe); stage run S1 (`S1-07-skin-sheet.png`, `S1-09-skin-reward.png`, `video-S1-skinning.mp4`). Test case GAT10. | — |
| A broken tool (durability 0 of a maximum above 0) is not a tool for a gather. | Static. Test case GAT3. | `0x5C65650` |
| The loot notice: an item change of the bag with the cause 1 (ItemGathered) shows "+N NAME"; the trade skill experience shows "+N SKILL". | Live L3; stage run S1. | — |

**The game data** (static; `gathering.py`, `loot_tables.py`):

| Fact | Source |
| --- | --- |
| A gathering slice: one GatherableController (class `0b382af4-7946-4698-9223-206d1187b612`, m_replicationIndex: the slot of type 12), a VariationDataComponent with a table of the ordinary resource tables, and an Interact bound with GatherOption. The variation row names the gatherable row (GatherableEntryID). | The slices of the game copy, the VariationData tables |
| The gatherable row: GatheringType (the tool family), Restriction, Tradeskill, RequiredTradeskillLevel, BaseGatherTime, MinRespawnRate and MaxRespawnRate, FinalLootTable, GameEventID, ResetGatherTimeOnEndInteraction, and the flags and columns of special nodes. | The GatherableData tables (Gatherables, QuestGatherables, GatherablesDunwood, GatherablesCatacombs, Gatherables_IsleOfNight) |
| The trade skill ranks: RollBonus, GatheringEfficiency, GatheringMultiplier of each Level. | TradeskillRankData (Mining, Logging, Harvesting, Skinning) |
| The experience of a gather: CategoricalProgressionId and CategoricalProgressionReward of the event of the row. | GameEventData |
| A tool: GatheringTypes, MinGatherEFF, MaxGatherEFF, GatheringEfficiency, GatheringMultiplier of the weapon row (ItemStatsRef of the master item). | WeaponItemDefinitions, MasterItemDefinitions |
| The EFF, ROL, MULT, and INS parts: the cells `EFF<skill>` and others of a status row (a food), of the status of an attribute threshold (`ATB_Str_Bonus_25_2_Effect`: EFFMining 0.1), of a perk of a tool. | The status, ability, affix, and attribute tables |
| The action Gather ends at a hit (IfDamagedByPowerLevel 0 or more). | `playeractions_gather.actionlist` |
| The retail census: 2,728,908 records of 49 distribution files, 2,450,322 of them gathering nodes; 33,071 capital nodes; 117 creature slices with skinning; 295 gatherable rows; 127 gathering slices; 230 tools. | `gathering.sh` of the stage |

**The rules** (the source of each rule is in [Gathering](systems/gathering.md)):

| Rule | What the backend does |
| --- | --- |
| The request | GatherOption of a node: the base checks the reference (a natural node and a body through the resolver of the gathering system), the option, the 3D range of the bound; the capacity does not apply. Then the row (supported), the node (not depleted; a body: dead), the tool (its paperdoll slot, not broken), the rank, the loot root, the event. Each refusal changes nothing (3743 result 2). |
| The work | The amount BaseGatherTime (or the rest of an earlier work on the node), the speed tool x (1 + rank + EFF), the Gathering state Success, the timed use of the base for amount / speed. A release, 2473, a walk out of the range, a travel, the death, damage, or a source that went: Failure and no item. |
| The completion | The node not depleted (the first valid completion of two characters wins; the other gets Failure and 3746), the tool and the rank again, the roll of the root with the luck (rank RollBonus + ROL), the experience of the event; one transaction: the items, the experience, the row of the node. |
| A full bag | The whole result must fit, and the catalog must have each item: else no item, no experience, no depletion, Failure, 3746; the roll stays for the next completion on the node (no new roll; the whole quantity). |
| The depletion and the respawn | Depleted (type 12) for each observer; Done for the owner. One uniform draw of the respawn bounds at the depletion, on the run clock of the world (a restart keeps the remaining time; the downtime does not count). Healthy at its time for each observer. |
| The skinning | The body of a creature with a skinning row is Healthy at the death; a skinned body stays Depleted until the creature system removes it; a new creature is a new node. |

**The live proof** (stage 3.6a, the backend of the stage and the game data of the run): the list is in `RESULT.md` of the evidence
folder (a tree that falls, a mining node, a plant, a skinning, the materials with a tooltip, the trade skill experience, a refusal, the
same node after its respawn).

**Not known, not done** (stage 3.6a):

- The action of a gather for an observer (the animation and the tool of a different player: type 11, not derived): a second client
  sees the node change but not the gather action of the first character.
- The rule of the original server for the composition of the speed (the tool, the rank, the EFF parts) and for the yield multiplier
  (GatheringMultiplier, MULT) and the instant gather (INS): the speed is a decision of an agent; the yield is not applied.
- The use of field 1 of type 12 (replenishTime): the client does not restore a node at that time.
- Fishing (3.6b); the durability loss (3.7); the quest, event, and special nodes (the rows with a rule that the backend cannot check).

### 4.45 The build preset: the upgrade gear score and the objective achievement of an artifact (live proof of stage B0; static proof for the parser of the client; the preset is project data from the decisions of the user)

Stage B0, the first part of the bow build of the user. Build sheet: `~/Games/soulwarden/work/research/bow-build.md`. Live checks U1,
U2, U13 and the live proof: `~/Games/soulwarden/work/evidence/bow-build/b0/implement/RESULT.md` (test builds outside `dist`). Code:
`world/build_presets.h`, `world/inventory.h` (`give_limit`, `plan_build`), `gamedata/item_catalog.h` (format 3),
`world/ownership.h` (`write_achievements`), `tools/gamedata/item_catalog.py`. Document: [Build presets](systems/build-presets.md).
Test cases BP1 to BP9. The preset uses no new message and no new state: the item, attribute, skill, ability, and achievement states
of the earlier sections, from their encoders.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The client shows the perks of the places 2 to 5 of an artifact only when the character has the achievement of its objective: the bit of the AchievementIndex of `Achievement_Objective_<item>` in the achievement state (type 3210, slot 66, 4.29; the Boltcaster 12324, the Winged Leather Shoes 12314, the Endless Thirst earring 12338). With no bit each of these places shows "Equip to view task."; the place 1 shows. | Live proof (check U1: the bow, the shoes, and the earring, each with and with no bit; the bits came with `tools/world fragment NAME 66 3210`, and the next tooltip showed the change with no new world entry) | Not traced |
| The tooltip of an artifact shows the fixed perks of its definition (Electrified, Bolt, Shirking Shock of the Boltcaster) also when the instance has only the places 4 and 5, as for each item (4.23, live check I2). | Live proof (check U1) | The rule of 4.23 |
| An artifact with the gear score 800 (above the 700 of its row): the tooltip and the paperdoll show 800, the gear score of the character counts it, and the equip takes it (MaxEquipGS 800 of the owned expansion, 4.29). | Live proof (check U2: the bow, the shoes, the earring) | The equip check of 4.29 |
| The parser of a master item row keeps the cell MaxCraftUpgradeGS only when it is not empty (a value with a presence byte); the 73 artifacts have 800, no other row has a value. | Static proof (the parser; the census of the game data) | `0x442DD20` (the store at +0x1997) |
| The use of a weapon consumable (HoningStoneT5) from the bag: the action menu of the bag has "Use" and "Equip to Slot 1" to "Equip to Slot 4" (the quick slots); "Use" sends the use request 2917 with the bag slot (4.23), and the backend gives the status of the item (2400 s). | Live proof (check U13) | The request of 4.23 |

**The data** (`item-catalog.json` format 3, [Game data](development/game-data.md)): the column `maxCraftUpgradeGS` of each item and
the member `artifactObjectives` (`[item id, objective id, achievement id, achievement index]` for each of the 73 rows of the type
Artifact of the tables `Objectives`: the item of the TaskEquipItem in its task tree, the AchievementIndex of the table
`AchievementData`).

**The rules** (the source of each rule is in [Build presets](systems/build-presets.md)): the gear score limit of a give is the larger
of the largest gear score of the item row and its upgrade limit, the upgrade limit not above MaxEquipGS (a value outside the range
gets the nearest value); the preset writes the achievement of the objective of each artifact of the build in its transaction.

**Not known, not done** (stage B0): the code of the client that hides the places 2 to 5 (not traced: the live proof is enough for the
preset); the objective progress of an artifact in play (the quest engine, stage 1.8); the gear system counts the perks of an artifact
also with no objective achievement (the client shows them as locked); the jewelry stat lines and the attribute bonuses of the build
(stage B4, 4.49: the maximum health of the character is 5,330 from stage B4, the video shows 10,676).

### 4.46 The bow abilities: the shot items of a fragment, the aim of Penetrating Shot, the pierce, the explosion record (live proof of stage B1; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage B1, the second part of the bow build of the user. Build sheets: `~/Games/soulwarden/work/research/bow-build.md` (3.4, 4.2),
`bow-build-2.md` (Q2, Q3, the live checks B1-1 to B1-6), `abilities-ranged.md`. Live checks before the code:
`~/Games/soulwarden/work/evidence/bow-build/b1/implement/live/LIVE-CHECKS.md` (runs b1-l1 to b1-l3: the layer reads of the client,
the input captures, a probe of the explosion slice); the live proof: `RESULT.md` of that folder (runs b1-f0, b1-f1). Code:
`world/abilities.h` (`VolleyPlan`, `resolve_volley`, `volley_shot`, `volley_landed`), `world/projectiles.h` (`pierce_hits`),
`gamedata/projectile_data.h` (format 2), `gamedata/combat_tables.h` (`hitRate`), `world/passives.cpp` (the volley count),
`world/creatures.cpp` (the damage ticks). Document: [Abilities](systems/abilities.md). Test cases BW1 to BW10. The stage uses no new
message and no new state: the shot items of 4.27, the cooldown state of 4.22, the spell record of 4.25, the compact status map of
4.22.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The key of Penetrating Shot (Q) starts only the aim of the client: the layer of the upper body plays the ready fragment, then `Bow_Aim_UpperBody`; the camera goes over the shoulder; no shot item. A click (attack press 555 with `aim_shoot_depressed` 359) or the key again shoots: one shot item in the input step of the click or of the key. | Live L1 (layer reads; inputs 3897, 6711, 8148, 10392 of session 1) | The action list `playeractions_ability_bow` (game data) |
| The shot item of Penetrating Shot: attack `penetratingshot` (CRC `cfd11046`), row `bow_penetrating_attack` (`6a1b00c5`), charge 0, ammunition `defaultarrow` (`c0e3a523`), the origin 0.9 m in front of the character and 1.45 m above its feet, the direction of the camera. The client keeps the aim with no time limit (58 s seen). | Live L1; live proof (the final run: the server shot from the item) | The item of 4.27 |
| The cooldown of the client for Penetrating Shot starts at the shot, not at the key (`SetCooldownTimer` in `Bow_Penetrating_Shoot`). A key of a different ability out of its cooldown in the aim plays that ability (the aim ends); a key in its own cooldown is not played, and the aim stays. | Live L1 (inputs 7510 to 10065) | Game data |
| Explosive Arrow (R): the key casts at once; one shot item 13 input steps (0.433 s) after the key (the cast at 0.4667 s of the fragment): attack `explosionshot` (`e1df9cfc`), row 0, charge 0, ammunition 0 (the form of a spell shot of 4.28). | Live L1 (inputs 10739 to 10752); live proof (the server got the item 0.432 s after the key) | Game data `player_bow.adb` |
| Rapid Shot (F): three shot items 9, 25, and 52 input steps after the key (0.30 s, 0.83 s, 1.73 s; the clips at 0.3333 s, 0.8667 s, 1.7333 s): attack `tripleshot` (`4516adc7`), rows `bow_rapidattack_standard` (`d8aa756a`), the same, `bow_rapidattack_knockback` (`72db63b6`), ammunition `defaultarrow` each, each with its own direction. | Live L1 (session 2, inputs 592 to 644); live proof (0.299 s, 0.816 s, 1.716 s at the server) | Game data |
| A key of a different bow ability in a bow fragment: the key R at 0.67 s of Rapid Shot is not played; at 2.0 s (in the buffered window 1.8681 s to 2.6 s of `CanAttack_*`) it is played at 2.6 s (the instant transition), and its item comes 13 steps after that. | Live L1 (session 2, inputs 8182 to 9005) | Game data (CAGE-TransitionBuffered, CAGE-TransitionInstant) |
| The client shows no explosion of Explosive Arrow by itself. The record of the slice `spell_aoe_explosionshot` (asset `9289CD31-D118-5E6C-827E-5513DD935E4A`, the Spell state at slot 1 with the spell key `Spell_Ability_Bow_ExplosiveShot_Explosion` and the position) shows the fire burst 0.4 s after the record, for about 0.6 s; the small slice of Contained Detonation shows the small burst. | Live L1 (two shots with no record: nothing; the probe `probe1-sheet.png`); live proof (`video-b1-2-explosive-burn.mp4`, `video-b1-2-explosive-contained.mp4`) | The spell record of 4.25 |
| The burn of Flame Arrow on a creature: its status number in the compact status map of the creature (slot 8, type 4236) shows the burn icon at the name plate for each observer, and the tick numbers (123) for the source. | Live proof (`b-18-burn-nameplates.png`: the icon at the plate of the direct target only) | The map of 4.22 |
| The cooldown state (slot 45, type 2932) with the end that Rapid Accuracy made shorter: the HUD shows 3 s at 3.5 s after the key (7 s; with two hits and a miss: 9 s, the 14 s of the data). The countdown of the HUD ends 1 s to 2 s before the end of the server (the client clock, 4.22). | Live L3; live proof (`b-20` to `b-22`) | The state of 4.22 |
| The client keeps the targets that a piercing projectile passed as an ordered list: field 16 `m_piercingHits` of the projectile state (type 16); each entry has an entity, a time (with a form "the same as the last"), and an optional hit volume. The backend sends no projectile state for the shot of a player (4.27), thus not this list. | Static (build sheet Q3) | Reader `0x6951010`; projectile state ctor `0x6715370` |
| For a damage row with NumConsecutiveHits above 0 (row field +0xD0) the hit test of the client needs the target of the hit data (+0x88): the count is a count for one target. | Static (build sheet Q3; the meaning of +0x88 is not proved) | Hit test `0x67A18F0`, row loader `0x43AA0C0` |
| For a hitscan profile the client uses `m_maxRange` as the length of its ray (the reason for the range 50 m of PenetratingShot and not the 100 m of the text). | Static (build sheet Q2) | `0x256D636` |

**The data** (`projectile-data.json` format 2, `combat-tables.json` member `hitRates`, [Game data](development/game-data.md)):

| Fact | Source |
| --- | --- |
| The actions `Bow_Penetrating_Shoot` (fragment `Bow_Shoot_UpperBody`, the variant with Bow and PenetratingAim: the clip PenetratingShot at 0.0333 s), `Bow_SpreadShot` (`Ability_Bow_ExplosiveArrow`: the casts at 0.4667 s of `Spell_Ability_Bow_Explosive_Base` with the condition `Bow_ExplosiveArrow_NoUpgrade` and of `_Upgrade` with `Bow_ExplosiveArrow_Upgraded`), `Bow_RapidShot` (`Ability_Bow_RapidShot`: three TripleShot clips; CanTransition 2.6 s); the transitions to the bow abilities (buffered 0.2333/0.5/1.8681 s, instant 0.7/1.1667/2.6 s) | `playeractions_ability_bow.actionlist`, `player_bow.adb` |
| The profile PenetratingShot: maxRange 50, radius 0.275, AccelDelayDist 30, destroy flags 1; ExplosionShot: maxRange 100, radius 0.275, the slice `basic_arrow_spread` | `rangedattackdatabase.radb` |
| The rows: Bow_Penetrating_Attack 1.5, Bow_Penetrating_AttackA1 1.5 with HitRateDmgModifier 0.1 and MaxHitRateDmgMod 0.5, Bow_RapidAttack_Standard 1.0, Bow_RapidAttack_KnockBack 1.25, Bow_ExplosionShot_Projectile 0.5, Bow_ExplosionShot_Explosion 1.35 (2.5 m), Bow_ExplosionShot_Explosion_Dmg 1.7 (1.25 m) | `DamageTable`, the slices `spell_aoe_explosionshot` and `_small` |
| The cooldowns 18 s, 22 s, 14 s; no mana cost; the burn `Status_ExplosiveShot_Burn` (10 s, TickRate 1, HealthModifierDamageBased −0.08, Fire) | The ability tables |

**The rules** (the source of each rule is in [Abilities](systems/abilities.md)): the volley of a cast and its shot items, the aim and
its confirm by the shot item, the windows of a bow fragment, the pierce in the order of the line and its hit rate, the explosion on
the bodies in its sphere, the burn only from the direct row, Rapid Accuracy for the arrows of one cast on one target (decision D11).

**Not known, not done** (stage B1): the charged shots A1 and A2 of Penetrating Shot (`IfAbilityActive PenetratingCharge`: no ability
row has that name); the arrow of the shot of a player for an observer (4.27); the arrow of the client prediction at 11 frames each
second (live check B1-6: the flight of a charged arrow with and without Aim True was the same on the server; the predicted arrow is in
no frame); a push of 0.01 m to 1 m of a walking creature for a live check of finding B0-R1 (no row of the build has one).

### 4.47 The stacks of a status on a creature and the rules of the Boltcaster (live proof of stage B2; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage B2, the third part of the bow build of the user. Build sheets: `~/Games/soulwarden/work/research/bow-build.md` (4.3),
`bow-build-2.md` (Q4, K1 to K4, the live checks B2-1 to B2-4). The live proof: `RESULT.md` of
`~/Games/soulwarden/work/evidence/bow-build/b2/implement/` (live checks L1 and L2 before the code; runs b2-live1 to b2-live4). Code:
`world/creatures.h` (`give_status`, `application_ready`, `stacks_of`, `remove_statuses`), `world/statuses.h` (`renewed_end`),
`world/passives.h` (`target_status_pass`, `own_status_pass`, `status_rules`), `server/rep/states/status_state.cpp` (the packed byte).
Documents: [Creatures](systems/creatures.md), [Status effects](systems/status-effects.md), [Passive rules](systems/passive-rules.md).
Test cases BC1 to BC11. The stage uses no new message and no new state: the compact status map of 4.22, the damage number of 4.24.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The packed byte of an entry of the compact status map of a creature (state type 4236, group 3 field 1, `statusIndex:u16 packed:u8`): bits 0 to 6 are the stacks (1 to 127), bit 7 is a potency of 1. The name plate of each observer shows the icon with the count ("x2", "x3", "x5"); a byte with stacks 1 shows the icon with no count. A new list with a larger sequence replaces the count. | Live L1 (the charges of the Boltcaster 1, 2, 3; a command entry with 5 stacks: `L1-sheet-stack-count.png`) | The compact reader `0x4044030`; one entry for each status number `0x4038E80` |
| An entry of a status with no time and with FxScriptOn (the bolt `Status_Perk_Artifact_Set1_Bow_StaticDischarge`, index 4047) in the compact map plays the visual effect of the row at the creature (the lightning strike), for each observer. The entry for 0.1 s (DurationMax) is sufficient; with no entry the client shows no strike. The damage number of the bolt is the number of 4.24 (type Lightning). | Live L2 (`L2-discharge-raw.mp4`); live proof B2-1 (`B2-1-sheet-bolt.png`, the number 1842) | The map of 4.22 |
| The hit test of a damage row of the client with TargetStatusEffect, TargetStatusEffectStackSize, and TargetStatusEffectComparison counts the stacks of the status of each source on the target; only with CheckStatusEffectsOnTargetOwned (bit 1 of +0xC4) the stacks of the attacker. An empty comparison kind is LessThanOrEqual. | Static (build sheet Q4; decision D10 of the orchestrator keeps this rule) | Hit test `0x67A18F0`; comparison parser `0x43B6390` |
| The self test of a row with StatusEffect (+0x1DC), StatusEffectStackSize (+0x1E0), and StatusEffectComparison (+0x1E4) counts the statuses of the attacker; CheckOwnedStatusEffects (bit 0) counts only the statuses of its own source. | Static (build sheet Q4) | Self test `0x67A0130` |
| StackMax below 1 is 1. The client has no reader of RefreshDuration (bit 28) and of RemoveUnappliedStacks (bit 26): the rule of a new stack and of its end is a rule of the server. | Static (build sheet Q4, K3) | Status loader `0x44B4430`; StackMax `0x44B65F5` |

**The data** (`abilities.json`, `statuses.json`, `damage-table.json`; no new file form; `projectile-data.json` format 3 for the
corrections of the code review of stage U3, [Game data](development/game-data.md)):

| Fact | Source |
| --- | --- |
| Electrified `GlobalPerk_Artifact_Set1_Bow` (OnHit, Light,Heavy, DamageTypes Lightning, BaseDamage 0.2); Bolt `GlobalPerk_Artifact_Set1_BowPerk` (OnHit, RequireReaction, Light,Heavy, the charge); the bolt `GlobalPerk_Artifact_Set1_BowPerk2` (21 bow rows, DamageTypes Lightning, the charge with StackSize 3 Equal, ActivationCooldown 1, RemoveTargetStatusEffectsList the charge) | `abilitytable` of the artifacts |
| The charge `Status_Perk_Artifact_Set1_Bow_StaticCharge` (index 791, 3 s, StackMax 3, RefreshDuration, RemoveUnappliedStacks); the removal `_StaticCharge_Removal` (0.2 s, RemoveStatusEffects the charge); the bolt `_StaticDischarge` (index 4047, no duration, DurationMax 0.1, HealthModifierDamageBased -0.8 Lightning, ApplicationCooldown 3, ShowInNameplates) | `statuseffects` of the artifacts |
| Shirking Shock II: the tracker `Status_ItemPerk_Weapon_DodgeSuccess_LightningDMG_Tracker` (10 s, EquipAbility `Ability_Perk_Weapon_DodgeSuccess_LightningDMG`, StopOnHitCount 1), the shock -0.236 Lightning; the potency 1.274 at gear score 800. Surprise Attack `Passive_Bow_Hunter_DmgOnFirst` (0.2) and its helper `_Tracker` (RequiredEquippedAbilityId) | The perk tables, `BowAbilityTable` |
| The switch marks and the dodge marks of the bow actions (Rapid Shot: the switch buffer 1.868143 s, the switch 2.566667 s; the dodge buffer 0.2 s, 0.867 s, 1.733 s and the dodge cancel 0.567 s, 1.1 s, 2.333 s) | `player_bow.adb` (CAGE-TransitionBuffered, CAGE-TransitionInstant, CanDodgeBuffer, CanDodgeCancel) |

**The rules** (the source of each rule is in [Passive rules](systems/passive-rules.md) and [Creatures](systems/creatures.md)): the
stacks of a creature effect and their end, the ApplicationCooldown of a row for each target, the removal window, the one-time damage of
a status with no duration, the target status test of each source (D10), the snapshot of the target statuses before the hit (the 4th
hit is the bolt), the trackers of the dodge perks, Surprise Attack with 0.2 of the data.

**Not known, not done** (stage B2): the view of a second client (only one client can run: the case BC6 tests two sessions); the
meaning of DisableScalePerTargetSEStack; the source of the Momentum status of the rapier (no plan of a rapier ability).

### 4.48 The haste chain of the bow build (live proof of stage B3; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage B3, the fourth part of the bow build of the user. Build sheets: `~/Games/soulwarden/work/research/bow-build.md` (rows 24, 26 to
28, 36, 44, 45), `bow-build-2.md` (Q1, K1, K2, K11, the live checks B3-1 to B3-4). The live proof: `RESULT.md` of
`~/Games/soulwarden/work/evidence/bow-build/b3/implement/` (live check L1 before the code; run L2 with the backend of the stage). Code:
`world/passives.h` (`weapon_changed`, `dodge_started`, `dodge_ended`, `duration_factor`), `world/statuses.h` (the time factor,
`on_stack`), `world/stamina.h` (the dodge action), `gamedata/ability_data.h` (format 7). Documents: [Passive rules](systems/passive-rules.md),
[Status effects](systems/status-effects.md), [Stamina and dodge](systems/stamina-and-dodge.md). Test cases HS1 to HS10. The stage uses no
new message and no new state: the status map of 4.24 (the full map entry of the owner gives the HUD icon), the speed table of 4.24.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| A full map entry of the owner (state type 4236, group 1 field 0) of a haste with ShowInUITray gives the HUD icon and its countdown: `Status_Passive_Bow_Skirmish_DodgeHaste` (index 2500) "4s" for 3.24 s (the HUD rounds up), `Status_Passive_Bow_Skirmish_HasteOnSwap` (index 1120) "9s" for 8.1 s, its timer `_HasteOnSwap_Timer` (index 3361, the category Timer, UseLightweightReplication) its own icon "10s" in the full map; the empower of Evasive Tactics `Status_Ultimate_Bow_Skirmish_DodgeDmg` (index 3326) "5s". The icon goes away at the end and at a removal (the bow leaves the hand). | Live L1 (a command: `L1-sheet-icon.png`, `L1-sheet-swap-timer.png`); live B3-1, B3-3, B3-5 (`B3-1-sheet-icon.png`, `B3-3-sheet-icons.png`, `B3-5-sheet-evasive.png`) | The map of 4.24 |
| The speed table of the owner (slot 43, type 1525) with 1.07 makes the run 1.065 to 1.069 times longer in the same time (L1: 22.0 m and 23.4 m in 4 s; B3-4: 19.05 m and 20.37 m in 3.5 s). The position check accepts each report (no refusal in the run). | Live L1, B3-4 (`B3-4-run-compare.mp4`) | 4.24 |
| The client predicts no status of the dodge status action `Passive_Bow_Skirmish_HasteOnDodge` that the HUD shows: a dodge of the client with the node, the bow, and full stamina showed no icon with the backend of stage B2 (no server entry). The icon of the haste comes from the server entry only: 0.18 s after the server applies it (live B3-1: the server at 0.50 s after the start of the dodge, the first frame of the icon at 0.69 s). | Live L1 (`L1-sheet-dodge-predict.png`), B3-1 | The action list runs in the client (`playeractions_ability_bow`); the predicted entry rule `0x4039600` (research `abilities-shared.md` Q1) did not show here |
| The client sums the time factors of a status: `AbilityComponent::GetStatusEffectDurationMultipliers(statusId, reduction, overall)` adds `potency x StatusEffectDurationReduction` of each ability of the character that names the status (StatusEffect) or a category of it (StatusEffectCategories) and whose self conditions pass; it does not read StatusEffectDurationCats. The client has no consumer of the sum: its use is server code. | Static (research `bow-build-2.md` Q1) | `0x68308E0` (sum helper `0x6830710`, lambdas `0x6987A20`, `0x6987A30`), self conditions `0x67A16A0`, `0x67A0130` |
| The loader reads OnWeaponSwap into bit 29 of the flags at +0xB8 of an ability row; no client code tests the bit: the event is server code. A hard-coded perk script removes the haste of Dodge and Weave when the weapon in the hand is not a bow. | Static (research Q1) | Loader `0x43AB5E4`; script `0x1E140A0` (from the perk tick `0x238B500`) |

**The data** (`ability-data.json` format 7, [Game data](development/game-data.md)):

| Fact | Source |
| --- | --- |
| The action `Passive_Bow_Skirmish_HasteOnDodge`: IfAbilityActive `Passive_Bow_Skirmish_DodgeHaste`, IfMannequinTag Bow, IfActionStatus Dodge Installed, IfStaminaValue 0.99 Percentage GreaterThanOrEqual, Not IfHasStatusEffect the haste (OnlyCheckOwned false); the exit: IfThenElse IfMannequinTag Bow, EffectsToApplyOnExit the haste. The action `Bow_Remove_StatusEffects_Sheathe`: IfMannequinTag Bow, EffectsToRemoveOnExit the haste | `playeractions_ability_bow.actionlist` |
| The haste of Dodge and Weave (2 s, DurationMax 4, Buff+Haste, MoveSpeedMod and SprintSpeedMod 0.07, AlwaysReplicate); the haste of Archer's Speed (5 s, DurationMax 10, OnStackStatusEffect its timer at AddOnStackSize 1 Equal); the timer (10 s, the category Timer); the empower of Evasive Tactics (5 s, Buff+Empower, DMG 0.15) | The status tables of the weapons |
| Archer's Speed `Passive_Bow_Skirmish_HasteOnSwap` (OnWeaponSwap, DontHaveStatusEffect the timer); Closing In `Passive_Bow_Skirmish_CDRVsLow` (OnHit, TargetHealthPercent 50, Duration -1, CooldownTimer -0.05 ActiveWeapon); Evasive Tactics `Ultimate_Bow_Skirmish` (AfterAction Dodge, AfterSequence the three dodge fragments, Duration 2); Speedy `GlobalPerk_Artifact_Set1_MediumFeet` (StatusEffectDurationReduction 0.5, StatusEffectCategories Haste) and `_2` (OnHit, StatusEffectCategories Haste, BaseDamage 0.1); Alacrity II `Ability_Perk_Amulet_DurHaste` (0.094, the outgoing part 0.094) | `BowAbilityTable`, the perk tables |

**The rules** (the source of each rule is in [Passive rules](systems/passive-rules.md), [Status effects](systems/status-effects.md), and
[Stamina and dodge](systems/stamina-and-dodge.md)): the dodge action of the backend (a buffered next dodge continues it; its end at the mark
CanTransition of its last dodge), the start tests and the exit tags of a dodge status action, the tag removal at a change of the weapon in
the hand, the swap event of the key of a weapon (an equipment change is no swap), the time factor `time x (1 + sum)` with the new status
counted as present, OnStackStatusEffect, the condition StatusEffectCategories, Duration as a mark.

**Not known, not done** (stage B3): the length of the animation of a roll (the end of the dodge action of the client; the backend uses the
mark CanTransition, research K11); the outgoing time factor (TargetStatusEffectDurationMult: Alacrity II on a haste for a different player,
Battle Precision: done in stage B4, 4.49); the use of StatusEffectOverallDurationReduction (no row of the data has it); the view of a second client
(the case HS7 tests two sessions).

### 4.49 The other rules of the bow build: the jewelry lines, the cooldown reduction key, the attack type of a creature hit (live proof of stage B4; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage B4, the fifth part of the bow build of the user. Build sheets: `~/Games/soulwarden/work/research/bow-build.md` (section 4),
`bow-build-2.md` (Q5 to Q8, K9, K13). The live proof: `RESULT.md` of `~/Games/soulwarden/work/evidence/bow-build/b4/implement/` (live check
L1 before the code with the backend of stage B3; run L2 with the backend of the stage). Code: `world/passives.h` (`attribute_rules`,
`merged`, `outgoing_factor`, `taken_reduction`, `cooldown_factor`, `consumable_cooldown_factor`, `consumable_used`, the perk scripts in
`tick`), `world/gear.h` (the jewelry lines, the heal parts of a perk row), `world/stamina.h` (`attack_started`),
`server/rep/states/stat_multiplier_state.cpp` (the key 0x4F), `gamedata/item_catalog.h` (`TrinketStats`), `gamedata/creature_data.h`
(`CreatureContact::attackType`). Documents: [Passive rules](systems/passive-rules.md), [Gear and perks](systems/gear-and-perks.md),
[Consumables](systems/consumables-and-heart-rune.md), [Stamina and dodge](systems/stamina-and-dodge.md). Test cases BR1 to BR14, BY1 to
BY3. The stage adds one key to the stat multiplier state of 4.24; no new message.

**The wire contracts**

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| The key 0x4F (CooldownReduction) of the stat multiplier state of the owner (slot 43, type 1525, the entry of 4.24: key u8, value i32 big-endian x 10000, syncVitals u8). The client takes value - 1 as a part of the cooldown reduction of an ability, in one sum with the fixed cooldown rules: the tooltip of Explosive Arrow (22 s in the data) shows 20.8 s with two sources of Refreshing (2 x 2.72 percent), and 19.7 s with the value 10,500 too (22 x (1 - 0.0544 - 0.05) = 19.70). The backend sends the key only when the line is not 0. The HUD countdown of a cast with the backend of the stage (the server cooldown 19.70 s) ends 19.7 to 19.84 s after the press. | Live L1 (`L1-03-explosive-tooltip.png`: 20.8 with the backend of stage B3; `L1-04-cdr-10500.png`: 19.7 with a table of a command); run L2 (`B4-cooldown-explosive.mp4`, `B4-cooldown-explosive-hud.png`, `L2-02-explosive-tooltip.png`) | The name registry `0x3FF0810` (the enum: MaxHealth 1, HealMod 3, ConsumableHealMod 4, StaminaRate 0x0B, MoveSpeedMod 0x34, SprintSpeedMod 0x35, BaseDamage 0x4E, CooldownReduction 0x4F) |
| The tooltip lines of the jewelry ("+4.53% Max Health" of Gorgon's Amulet at 725, "+9.06% Base Damage" of the ring at 725, "+5.00% Cooldown Reduction" of the earring at 800): gear score x Scale...PerGS x TrinketBonus x 0.01 of the row of the stats table ArmorItemDefinitions. The client computes them from the item; the server sends no line. The max health of the character sheet comes from the vitals of the server (5,109 with the backend of stage B3, 5,330 with the backend of stage B4). | Live L1 (`L1-08-tip-j1.png` to `L1-10-tip-j3.png`, `L1-05-attr-multipliers.png`); live check B4-4 in run L2 (`L2-01-attributes.png`) | The consumer of the lines `0x3636A50` |
| The weapon damage of the character sheet: GetCoreDamage with the item callback; with a gem affix with UseWeaponAttributeScaling (+0x13d; +0x13c IsAdditiveDamage, +0x13e PreferHigherScaling) the number is core x (1 + p x (1 - p)), p the DamagePercentage (+0x4c): the rapier 1,824 (p 0.5), the Boltcaster 2,505 (p 0.99). Without the flag: (1 - p) x core + p x the core with no attribute = 1,012 (the number of the video). A number of the client only. | Live L1 (`L1-05-attr-multipliers.png`, `L1-12-rapier-tooltip.png`); static | `0x14368D200` (GetCoreDamage), `0x1436A57A0` (the callback), the affix loader `0x1443B3A40` |
| The perk tooltips take the potency of the gear score 100 of the potency curve: Refreshing 1.36 ("2.72%"), Enchanted Ward 1.7515 ("1.8%"), Enchanted Ward II 1.27375 x 0.016 ("2.0%", stacks 2 of 5), Alchemist Reprieve 4.0 ("8%"). | Live L1; live check B4-2 in run L2 (`L2-08-tip-head.png`, `L2-09-tip-feet.png`, `L2-10-tip-chest.png`, `L2-tooltips-armor-sheet.png`) | The potency `0x67E9A70` (IsStackableMax +0x944, IsStackableDiminishingModifier +0x948) |
| The statuses of Vital Synergy come from the server with no row of the perk in the data (the rows are empty): the icon "Anointed" (`Status_Perk_Chest_StamLowHealthRegen`) while the stamina is below half, the stamina status while the health is below half; each icon goes away when its test does not hold. | Live L1 (`L1-13-vital-synergy-low-health.png`: no icon with the backend of stage B3); live B4-3 (`B4-3-vital-synergy.mp4`, `B4-3-vital-synergy-icons.png`: in the backend log the low stamina status from 15:27:05.5 to 08.0, the low health status from 10.0 to 16.1) | The perk script `0x238DEF0` from the perk tick `0x238B500` (the tests at `0x238E2E8` to `0x238E4A3`) |
| A status of a food on the character with the time factor of an EquipAbility (Desert Sunrise): the full map entry has the shorter time, and the HUD shows it ("15s" for a poison of 20 s). | Live run L2 (`L2-07-desert-sunrise-poison.png`) | The map of 4.24 |

**The data** (the game data files of stage B4, [Game data](development/game-data.md)):

| Fact | Source |
| --- | --- |
| The jewelry lines: the columns TrinketBonus, ScaleHealthPerGS, ScaleDamagePerGS, ScaleCDRPerGS of ArmorItemDefinitions (2,505 rows with a line; Gorgon's Amulet 0.01 / 0.625 / 0 / 0, the earring 0.01 / 0 / 0 / 0.625) | The item tables (`item-catalog.json` format 4) |
| The attack type of a creature attack: the column AttackType of the damage row of the attack (3,290 contacts: Light 1,801, Heavy 1,347, Magic 45, blank 97) | The damage tables of the creatures (`creatures.json`, a contact of 10 values) |
| The mark CanAttackCancel of the dodge fragments (0.266667 s) and rule 96 of `player.grid` (Dodge to Attack, Shoot, Offhand at that mark) | `player.adb`, `player.grid` (`stamina-data.json` format 2) |
| Long Range `Passive_Bow_Hunter_DmgVsDist` (OnHit, DistFromDefender 10, GreaterThanOrEqual, BaseDamage 0.1); Finishing Shot `Passive_Bow_Hunter_DmgVsLow` (TargetHealthPercent 50, BaseDamage 0.15, no event); Battle Precision `Passive_Bow_Skirmish_DebuffDur`; Enchanted Ward `Global_Armor_DefBasic` (0.01) and `Ability_Perk_Armor_DefBasic` (0.016, IsStackableMax 5); Refreshing `Global_Common_CDR` (-0.02, IsStackableMax 3); the potion perks (`Global_Amulet_PotionHeal`, `GlobalPerk_Artifact_Set1_Earring1` and `_2` to `_5`, `Ability_Perk_Earring_OnPotionDmg`, `_OnPotionFort`); the food row `Global_Consumable_FoodDoT_DurRed_T5` | `BowAbilityTable`, the perk tables, the attribute tables (`ability-data.json` format 7, `progression-tables.json`) |

**The rules** (the source of each rule is in [Passive rules](systems/passive-rules.md), [Gear and perks](systems/gear-and-perks.md),
[Consumables](systems/consumables-and-heart-rune.md), and [Stamina and dodge](systems/stamina-and-dodge.md)): the threshold abilities as
rules of the character, the cap of the sources of a row, the distance of Long Range from the place of the attacker at the hit, a target
condition with no event as a rule of each hit, the outgoing time factor, the reduction of a creature hit by its attack type, the fixed
cooldown rules and the cooldown line in one sum, the perk script of Vital Synergy, the heal parts of the perk rows in the received pool
(decision D9: Divine counts for a potion), the consumable cooldown factor with a positive value and a cooldown id, the consumable rules
after a use, the end of the dodge action at an attack after CanAttackCancel.

**Not known, not done** (stage B4): the maximum health of the video (10,676; this client and its data give 5,330: the client of the video
has different data, research of stage B4); the origin of the position +0x230 of the hit data of the client (Long Range, research K9: the
backend uses the place of the attacker at the hit); the period of the perk tick of the client (the backend tests Vital Synergy each
500 ms); the use of the outgoing factor by the server of the game (the client has no reader); the view of a second client (the case BR12
tests two sessions); the rules of a hit do not merge the sources of a row (only the new paths of the stage do).

### 4.50 The fixed crafts: the craft request, the batch, the progress, the empty storage search, the station variants (live proof of stage 3.7a; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Stage 3.7a, the first part of the crafting. Research: `~/Games/soulwarden/work/research/crafting-stage.md` (sections 1 to 4, 8 to 12),
`crafting.md` (4.1 to 4.3, 6, 10). The live checks and the live proof: `~/Games/soulwarden/work/evidence/crafting/37a/implement/`
(`RESULT.md`, the pictures and videos; checks L1, L2, L3, L10, L10b with the backend of stage B4 and `tools/world fragment`, `tools/world
message`, `tools/world give --action 3` before the code; the stage run with the backend of the stage). Code: `world/crafting.h`,
`server/rep/crafting_messages.h`, `world/inventory.h` (`plan_craft`), `gamedata/crafting_data.h`, `tools/gamedata/crafting.py`,
`tools/gamedata/interactions.py` (`station_overrides`). Document: [Crafting](systems/crafting.md). Test cases CRAFT1 to CRAFT-TOOL.

**The wire contracts** (big-endian numbers; `size` is the prefix code of 3.3; each body starts with the facet target and G of 4.2):

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| Request 2997 (CraftingComponentServerFacet_SetCraftingDetails), the facet of the component `Crafting/f7c1e949-1ae0-410f-b36e-6226843fd706` of the player slice: recipeCrc:u32 (CRC-32 of the lower-case recipe id) quantity:u32 coinFee:u32 gearScore:u16 perkCount:size perkItemCrc:u32* upgrade:u8 ingredientCount:size {type:u8 quantity:u32 itemCrc:u32}*. The flint knife: `A546F7C5 00000001 00000001 0064 00 00 02 01 00000001 0C9280C8 01 00000001 8A914427` (51 bytes with the prefix). Each entry is type 1 (Item), quantity 1, the CRC of the selected item, in the order of the recipe: the server takes the recipe quantities. The count 3 of the default UI is one request (`00000003`) with the same entries. | Live L3, L10b (captures `s0001/11511`, `27799`); live stage run (the log line of each request). Test case CRAFT1 (the live body with a made-up target). | Writer `0x2DF48A0`, `0x2E173D0`; readers `0x2DF4C10`, `0x2E2A040`, `0x2E29E90`; limits `0x2D93E30` (count 10,000), `0x2E6ABE0` (fee), `0x2DE6CD0` (gear score), `0x2E6A970` (upgrade 5), `0x2E6ACB0` (entry 50,000), type check `0x2E6A8A0`; the entry builder `0x5BA9FB0` |
| The fee of the request: the client sends round(max(1, trunc(count x CraftingFee x territory factor) x (1 - taxes))) for a recipe with UseCraftingTax at a station whose Assembly charges a fee: 1 (0.01 coin, "Everfall Fee: 0.01") for the counts 1 and 3 of the knife (CraftingFee 0). The server computes the same with the factor 1 and no tax. | Live L3, L10b (the requests); static | `0x2E513A0` (the count x fee x factor), `0x2E57580` (the tax path: `0x2E579F1` to `0x2E57A2E`, the minimum 1.0 at `0x147EFE900`) |
| Stop 2999 (CraftingComponentServerFacet_CancelCrafting): no field (16 bytes). The client sends it 11 ms after each progress whose completed count reaches its total (also the stop form): the handler of 2993 stops the local crafting through `0x2E4B360`, which calls the send stub `0x2E215F0`. | Live L3, L10b (12214, 17838, 28535 after each final progress); stage run (each "craft stop (type 2999): no craft batch"). Test case CRAFT2. | Reader `0x259E1E0`; `0x2E4B360` |
| Progress 2993 (CraftingComponentClientFacet_OnCraftingProgressUpdate): recipeId:string completed:u32 total:u32 (42 bytes for the knife, 1 of 1). completed >= total ends the crafting of the client (it sends 2999); an empty recipe with 0 of 0 (the stop form, 25 bytes) ends it too. In a batch the Lua handler OnCraftingCountUpdate sets the expected count of item updates only when completed reaches total. The progress carries no item. | Live L3 (trace `0x2E602A0` #0 r8=1 r9=1, #1 r8=0 r9=0), L10b; stage run. Test case CRAFT1 (the exact bytes after the bundle of the item). | Writer `0x2949A30`, reader `0x2949B10`, handler `0x2E602A0` |
| The result: a bag slot update with the cause ItemCreated (3) in the crafting roll state shows the result screen ("FLINT SKINNING KNIFE", the gear score counting to 100, "COMMON", "Continue to Craft", "Equip Tool", "Salvage", "Exit Station") 2 s after the last item; a batch shows "3 ITEMS CRAFTED" with each item and "Salvage All". The crafting screen counts each slot update with the cause 3 or 32 (ItemAddedWithoutLimit); each update restarts a 2 s timer. | Live L3 (`give --action 3` before the code), L10 (`I3-03-craft.mp4`), L10b (`L10b-02-batch-3.mp4`, `L10b-03-batch-summary.png`). Test case CRAFT1. | `craftingscreenv4.luac` OnSlotUpdate, OnPaperdollSlotUpdate, OnCraftingCountUpdate |
| No result: with no item the client shows its craft roll for 10 s, then returns to the recipe screen with "Crafting attempt failed." (the stop form does not shorten it). | Live L3 (`I5-02-attempt-failed.png`, `I5-01-late-refusal.mp4` in the stage run). Test case CRAFT2. | `craftingscreenv4.luac` OnCraftClicked (Delay 10), OnCraftingFailed |
| The search of the crafting screen: 7044 (GlobalStorageComponentServerFacet_RequestSearchStorage; the facet of `GlobalStorage/127579b6-b5be-4282-8985-bf1fce3c3f51`): searchUuid:16 and 15 bytes of parameters, at each open of a station (not at a camp). 7046 (GlobalStorageComponentClientFacet_ReceiveStorageSearchResults): searchUuid:16 count:size entries*; the matching UUID and count 0 (33 bytes) reach the result handler; the recipe screen stays loaded. 7039 (RequestReplicateStorageSearchResults): no field; the server sends the same empty result. | Live L2 (trace `0x2D13950` #0 after the 7046 of `tools/world message`); stage run. Test case CRAFT1. | Constructor `0x2CD5300`, request reader `0x2CE5530`; writer `0x2CD5D50`, reader `0x2CD5DF0`, `0x2D13950` |
| The state 205 (Crafting, slot 63): the empty form `01 03 00 01 01 00 00 01 01 00` (the cooldown map and the gear score bonus map, full, sequence 1, no entry) is applied (trace `0x2E819C0`) and changes nothing that shows. The backend does not send it in stage 3.7a (the recipe screen opens with no such state, live check L4 of stage 2.5b); from stage 3.7b it sends the cooldown map (4.51). | Live L1. | Apply `0x2E819C0`, readers `0x2E897C0`, `0x2E89910` |
| The station variants of decision D1: a server child with the `_t2` slice of a family (key ending in 3) shows the prompt and the screen of that tier: the carpentry station of Oxboro is "WOODSHOP TIER 3" with Carpentry3 (before: the master slice with Carpentry2, "WOODSHOP TIER 5" and the check "Woodshop Tier 2"). | Stage run (`D1-01-carpentry.png`, `D1-02-carpentry-screen.png`; the log "station of the interaction: key Carpentry3"). Test case CRAFT-D1. | Lookup `0x2E804E0` (no alias of a missing key) |

**The game data** (static; `crafting.py`, the research K1 to K4):

| Fact | Source |
| --- | --- |
| 8,294 recipe ids in 15 tables of the type CraftingRecipeData (no id twice); the ingredient types "Item", "Category_Only", "Currency", and blank; 120 rows with no output and no station key; 6 recipes with no cause of stage 3.7a (1hHatchetT1, 1hPickT1, 1hSickleT1, 1hSkinningKnifeT1, 2hFishingPoleT1 with engineering2 and camp1; 1hSwordT1 with blacksmith2 and camp1). | The CraftingRecipeData tables |
| 63 station rows (Name, CraftingTypes, MannequinTag, AzothDiscountPercent); the keys Smelting2, Carpentry2, Masonry2, Tanning2, Weaving2 of the master station slices have no row; their `_t2` variants have the keys ending in 3. | `craftingstations.craftstationdb`, `slices/structures/settlements/stations/` |
| The members of a category ingredient: the IngredientCategories of the master items (617 categories). | The MasterItemDefinitions tables |

### 4.51 Refining and the reward progress: the bonus items, the trade skill experience, the first craft, the cooldown of a recipe (live proof of stage 3.7b; static proof for the client code and the game data; the extra units are decision D2 of the orchestrator)

Stage 3.7b, the second part of the crafting. Research: `~/Games/soulwarden/work/research/crafting-stage.md` (sections 4, 5, 9 to 12). The live
checks and the live proof: `~/Games/soulwarden/work/evidence/crafting/37b/implement/` (`RESULT.md`; checks L4 and L6 with the backend of
stage U4 and `tools/world fragment`, `tools/world skillxp`, `tools/world give --action N` before the code; the stage run with the backend of
the stage). Code: `world/crafting.h` (`CraftYield`, `CraftExperience`, `cooldown_left`), `server/rep/states/crafting_state.cpp`,
`gamedata/crafting_data.h` (format 2), `tools/gamedata/crafting.py`. Document: [Crafting](systems/crafting.md). Test cases CRAFTB1 to CRAFTB-CMD.

**The wire contracts** (big-endian numbers):

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| State 205 (MB::CraftingComponentReplicatedState, slot 63), group 0, field 0, the cooldown map in the full form: `00` (no change entries), `01` sequence:size64, count:size (100 at most), `{recipeCrc:u32 used:u8 end:u64}*`. `end` is the end of the window on the synced wall clock of the client in Unix nanoseconds (the clock of the end of a status effect, 4.24). The client shows "Crafting Cooldown: TIME per N items" with N = CooldownQuantity - used and TIME the time to `end`, and it ends the window itself at `end` (N becomes CooldownQuantity). used = CooldownQuantity disables CRAFT ("per 0 items"). The IngotT51 entry: `3BD6B9F4 01 <end>`. | Live L6 (fragments of `tools/world fragment`: the end in one hour: "59m 57s per 9 items"; the end in 20 s: "18s", "9s", then "per 10 items"; used 10: "1h 29m per 0 items" and CRAFT off); stage run (the state of the backend: "23h 59m per 9 items"; after a restart "23h 58m per 9 items"). Test case CRAFTB3 (the exact bytes). | Apply `0x2E819C0`, reader `0x2E897C0` (count < 0x65); craftingstatspanel.luac OnTick, UpdateCooldownCount (GetCooldownTimeRemainingInSeconds, GetRemainingCooldownCount) |
| State 205, field 1, the gear score bonus map: `00`, `01` sequence:size64, count:size (21 at most), `{skillCode:u8 bonus:u16}*`. skillCode is the TradeskillType of the client with Weaponsmithing 0, Armoring 1, Jewelcrafting 2, Arcana 3, Cooking 4, Furnishing 5, Engineering 6, Smelting 7, Woodworking 8, Leatherworking 9, Weaving 10, Stonecutting 11, Skinning 12, Mining 13, Logging 14, Harvesting 15, WildernessSurvival 16, Fishing 17, AzothStaff 18, Musician 19, Riding 20 (the order of the enum strings TradeskillType_None, ..._Riding at image RVA 0x83B6F08; None is 0xFF: the test of 0x2E57A70). The bonus adds to the gear score of a craft of that skill. The backend of stage 3.7b sends it empty; from stage 3.7c it has the craft bonus of the statuses (4.52). | Live L6: the 21 codes 1 to 21 with the bonus = the code moved the Iron Skinning Knife of the Workshop from 205 to 211; code 6 with 50 gave 255 (reopen of the station). | Reader `0x2E89910` (count <= 0x15) |
| The first-craft fact: the bit of the FirstCraftAchievementId of the recipe (AchievementIndex of the achievement tables: FC_IngotT2 5330, FC_IngotT51 5335) in the achievement state (66, 3210; 4.29). Without the bit the screen shows "First Craft Bonus Available" and adds (multiplier - 1) x the base of one job to the experience of a batch; with the bit it shows only the base. | Live L4 (the full achievement state with the 7 bits of the character and 5330: "+44 Smelting" in place of "+132"); stage run (the backend writes the row in the transaction of the first job: "+220" then "+44"). Test case CRAFTB2. | craftingscreenv4.luac OnCraftClicked (`GetRecipeFirstCraftAchievementId`, `IsAchievementUnlocked`), craftingstatspanel.luac UpdateSkillAmount; the multiplier `javelin.crafting-first-craft-bonus-exp-multiplier` (3: `0x6171316`, `0x617133F`) |
| The trade skill experience: the categorical state 911 (4.13) with the new points. The screen shows the gain of the 911 change ("+220 Smelting" in its summary; OnCategoricalProgressionPointsChanged adds a3 - a2 and the points of each rank up). Its tooltip computes reward x GetIngredientCount() x count + floor(reward x units x (multiplier - 1)) ("Base 132 XP, First Craft 88 XP (x2), Total 220" for 3 iron ingots). | Live L4 (`tools/world skillxp ... 220` during the craft: "+220 Smelting"; the tooltip), stage run (the backend: 220, then 44; `tools/world progress`: Smelting 1, 112 of 114, then 2, 42 of 120). Test case CRAFTB2. | craftingscreenv4.luac OnCategoricalProgressionPointsChanged, OnCategoricalProgressionRankChanged; the unit count `0x5C47AC0` |
| The bonus item chance of the screen ("Chance to craft additional items", "%.0f%%"): max(0, (m + r x 0.0001) x 100). m (0x5C52A40): recipe BonusItemChance (recipe +0xEC); the item of the first recipe ingredient when it is an Item: its tier is the base tier and its ExtraBonusItemChance (item +0x50, an integer) adds; a first category ingredient: the tier of its selected member; each later category ingredient: its tier difference picks BonusItemChanceIncrease (+0x2B0) or BonusItemChanceDecrease (+0x2D0) (index min(difference, size) - 1), plus the ExtraBonusItemChance of the member. r (0x2E57A70, only for an output item with the loaded mark +0x17): for each selected entry int(r + QualityContribution (+0x38)) + IngredientBonusPrimary (+0x40) for the first entry or IngredientBonusSecondary (+0x44) for the others; with a trade skill (recipe +0xE, 0xFF none) and no DisallowBonusesToGS (recipe +0xD) the RollBonus of the rank row and the ROL map of the component (`0x2E4FD80`, +0x458). | Live L4 and the stage run: 0% at Smelting 0, 5% at 1 (510), 8% for the steel ingot at 50 (-0.02 + 1000 points), 25% for the iron ingot at 200 (2500), 0% for Asmodeum at 200 (-0.25 + 2500 + 5 of the Cinnabar). Test case CRAFTB1. | `0x2E4FF50`, `0x5C52A40`, `0x2E57A70`, `0x2E4FD80`; item loader `0x442DD20` (the stores at rbp+0x618 to 0x630); recipe loader `0x4481730` (CraftAll +0x3, IsProcedural +0x5, UseCraftingTax +0xC, DisallowBonusesToGS +0xD) |
| The result of a craft-all recipe: in its roll state the screen counts the output in the bag at each slot update with the cause ItemCreated (3) or ItemAddedWithoutLimit (32); at the first such update whose count - the count before the click reaches the jobs x OutputQty it shows TOTAL, BASE AMOUNT (jobs x OutputQty), and BONUS (the rest). A later update with the cause 3 restarts the bar: the total is right, but the row of the base and the bonus goes away. An update with the cause ItemAdded (0) does not start the screen (each such update is in the count of the next update with the cause 3). The backend: the cause 0 for each job before the last one, 3 for the last job. | Live L4: test A (`give 2 --action 0`, then `give 1 --action 3` for 2 jobs: "TOTAL 3, BASE AMOUNT 2, BONUS 1"), test B (two updates with the cause 3: "TOTAL 3" with no row of the base); the stage run: the seeded batch "TOTAL 14, BASE AMOUNT 10, BONUS 4". Test case CRAFTB1. | craftingscreenv4.luac OnSlotUpdate (SetQuantityCrafted, StartRefiningBarAnimation), OnCraftClicked (SetOriginalInventoryAmount) |
| The fee of a refining recipe: count x CraftingFee (no UseCraftingTax): IngotT2 (CraftingFee 1) sent 3 for the count 3; the server took 3. | Live L4 (the 2997 of the count 3), the stage run ("fee of the batch 3 (claim 3)"). | 4.50 |

**The game data** (static; `crafting.py` format 2):

| Fact | Source |
| --- | --- |
| The RollBonus of the ranks: 20 tables of the type TradeskillRankData (the table name is the trade skill; AzothStaff and Riding have no RollBonus column: 0). Smelting, Woodworking, Leatherworking, Weaving, Stonecutting, Jewelcrafting: 510 at rank 1, 1000 at 50, 2500 at 200; Arcana, Cooking, the gathering skills 10, 500, 2000. | `javelindata_tradeskill*.datasheet` |
| The first-craft achievements: 5,110 bits of the FirstCraftAchievementId of a recipe (AchievementIndex). | `javelindata_achievements.datasheet` |
| The bonus values of the items: 26 items with IngredientBonusPrimary 10 and IngredientBonusSecondary 5 (CinnabarT1, the T1 special materials); ExtraBonusItemChance is blank in each master table. The ROL statuses: SE_FoodLostBlackLicoriceT5 200 for each refining skill, SmelterMastery1 to 3 200, 350, 500. | The MasterItemDefinitions tables, the status effect tables |
| 948 recipes with no cause: 610 refining, 699 craft-all, 942 with an event (487 of them with territory standing), 870 with a first-craft achievement, 45 with a cooldown, 39 with OutputQty above 1. | `crafting-data.json` |

### 4.52 The procedural items: the craft gear score, the perks of a craft, the perk items and the Azoth, the craft bonus of the statuses, the currency ingredients (live proof of stage 3.7c; static proof for the client code and the game data; the mapping of the request to the perk generator and the one-gem rule are decisions of an agent)

Stage 3.7c, the third part of the crafting. Research: `~/Games/soulwarden/work/research/crafting-stage.md` (section 4: K4, K8, K9). The live
checks and the live proof: `~/Games/soulwarden/work/evidence/crafting/37c/implement/` (`RESULT.md`; checks L5, L5b, L5c with the backend of
stage 3.7b and `tools/world fragment` before the code; the stage run with the backend of the stage). Code: `world/crafting.h`
(`CraftGearScore`, `CraftPerkPlan`, `refresh_bonus`, `azoth_of`), `world/perk_roll.h` (`CraftPerkInputs`, `roll_craft_perks`),
`server/rep/states/crafting_state.cpp`, `world/skills.h` (the Azoth balance), `gamedata/crafting_data.h` (format 3),
`tools/gamedata/crafting.py`. Document: [Crafting](systems/crafting.md). Test cases CRAFTC1 to CRAFTC-CMD.

**The wire contracts** (big-endian numbers; the bodies after the facet target and G of 4.2):

| Contract | Evidence | Client code (image RVA) |
| --- | --- | --- |
| Request 2997 with perk items and an upgrade (4.50): `perkCount:size perkItemCrc:u32*` is the CRC-32 of the lower-case resource item id of each filled craft mod control, in the order of the controls 1 to 4; `upgrade:u8` is the Azoth level (G, 0 to 5), apart from the perk items. The gear score claim is the craft gear score of the screen. The knife with SkinnerCharmT1: `FEDD0895 00000001 00000019 00D2 01 4F660ED8 00 03 01 00000001 9BD77F6D 01 00000001 AB801777 01 00000001 086CAAB6`; the knife with G 1 and no charm: `... 00CD 00 01 03 ...`. The bytes do not depend on PerkItemsBucketPush: the great axe (push true) with GemSocketPinsT1 `9F832BDA 00000001 000001C1 0258 01 B3BC2B61 00 03 01 00000001 D5D8D8D8 01 00000001 F5424D95 01 00000001 0099624F`; the bag Procedural_BagAT5 (push false) with AdderStoneT1 `5C7D4F8B 00000001 000001C1 0235 01 D19BA570 00 04 ...`. | Live L5 (captures `s0001/18984`, `26848`), L5b (`s0002/06431`, `09878`, `18825`); stage run (the accepted requests of the knife with the charm and with G 1, the axe with the charm and G 4). Test case CRAFTC3 (the five live bodies with a made-up target). | Writer `0x2DF48A0`; limits `0x2E6A970` (upgrade 5), 4 perk items (setting javelin.crafting-max-num-perk-items, 4 at `0x6171372`); craftingscreenv4.luac OnCraftClicked (the nonzero controls 1 to 4 in order, the level apart) |
| The entry of a currency ingredient in 2997: type 1 (Item), quantity 1, item 0. The Prismatic Setting (IngotT53 and 250 Azoth_Currency): `98B1E892 00000001 000005DC 0000 00 00 02 01 00000001 D5D8D8D8 01 00000001 00000000`. | Live L5c (`s0001/03566`); stage run (the Tempered Cast accepted, 250 Azoth taken). Test case CRAFTC5. | The entry builder `0x5BA9FB0` |
| The gear score claim of a procedural recipe: the server facet of the client computes the craft gear score for a recipe with a BaseGearScore. It is BaseGearScore + MaxGearScoreBonus of the rank for the output tier (T2 to T5 columns; not with DisallowBonusesToGS +0xD or IsRefining +0x4) + IngredientGearScoreMaxBonus of the selected members (IsProcedural +0x5 only) + min(field 1 of state 205 for the skill, 50); GearScoreOverride replaces it; the cap is MaxCraftGS of the item, else of the expansion; at the end min = max. | Live L5: the Iron Skinning Knife 205 (Engineering 0), 255 (Engineering 50), 210 (Fae Iron), 210 (Fae Iron and a timber of tier 3); L5b: the Orichalcum Great Axe 600 (Weaponsmithing 200, the Prismatic materials, the food), the Iron Greatsword 290; stage run: the same values on the result screens and the tooltips. Test case CRAFTC1. | `0x2E41E10`, `0x2E52870`, `0x2E57A70` (the stores `0x2E58218` to `0x2E58222`), the rank map `0x3CB2AA0`, the cap setting `0x61712C1`, `0x61712EA` |
| State 205 field 1 (4.51) with values: the client adds the entry of the trade skill of the recipe to the craft gear score, to 50; it does not add the statuses of the character itself. The backend sends the sum of the MaxGS<skill> effects of the active statuses. | Live L5: the food status FoodEngineerT1 on the HUD with no field 1: 205 (no change); field 1 `{06, 0005}`: 210; `{06, 003C}` (60): 255 (the cap 50); the stage run: the axe 600 (the food 15), 617 (and the trophy of 3 stacks 15 and the gear perk status 2), 602 (the trophy status ended), each after a new open of the station. Test case CRAFTC4. | Reader `0x2E89910`; `0x2E57A70` |
| The Azoth of the upgrade levels: the screen shows the cost of each level as trunc(PerkCost x G) x (1 - AzothDiscountPercent of the station), with floor: 0, 8, 16, 24, 33, 41 for PerkCost 15 at a tier-5 station (0.45). The levels above maximumPerks - fixed perks - perk items are red (41 with a charm on the axe). The server takes the same per job. | Live L5 (the workshop), L5b (the forge); stage run (692 to 659 for G 4 on the axe; 500 to 492 for G 1 on the knife). Test case CRAFTC3. | `0x2E56070` (the conversion `0x2E5614D`), `0x2E40EE0`; the setting javelin.enable-azoth-station-discounts (true at `0x616EBD4`); craftingrecipepanel.luac (the level limit) |
| The Azoth balance: an entry of the categorical state 911 (4.13) for the progression Azoth_Currency (CRC `37504247`), rank 0, the points = the balance. The client shows it in the top bar of the crafting screen, in the Azoth slot ("x33"), in the bag ("659 / 2,500"), and a notice at a gain ("+200 Azoth"). | Live L5 (`tools/world fragment` with 500: the top bar 500, the levels), L5b (the bag "500 / 2,500"); stage run (the backend balance; the tool window "Set": "+200 Azoth"). Test cases CRAFTC3, CRAFTC5, CRAFTC-CMD. | The apply of 911 `0x3CB7170` |
| The result of a procedural craft: the item instance (4.23) with the gear score and the perks of the server. The client derives the rarity from the perk count (5 perks: LEGENDARY) and shows the perks in its own order (the attribute, the gem slot, the ability perk, then the others), not in the order of the instance. The swapped attribute (Acutely Empowered) is the perk PerkID_StatSwap_BaseDamage of the instance. | Stage run: the axe 600, LEGENDARY, Acutely Empowered, Empty Gem Slot, Enfeebling Maelstrom, Shirking Flames, Rogue (the result screen, the tooltip, the tooltip after a restart: `I2-03`, `I2-04`, `I2-05`). Test case CRAFTC6. | `0x44407E0`, `0x5CF4A40`, `0x5CF5750`, `0x5C528D0` (stage 1.3b, 4.23) |
| The bonus item row of the screen ("Chance to craft additional items") is only for a craft-all or a refining recipe. A procedural recipe has no row. | Stage run (the Tempered Cast, craft-all and refining: "0%"; the axe and the knife: no row). Test case CRAFTC1. | craftingstatspanel.luac (RefiningInfo, GetChanceForBonusItems for IsRecipeCraftAll or IsRecipeRefining) |

**The perk generator with the inputs of a craft** (static; research K8; the mapping of the request is a decision of an agent): the
parameter object (`0x5BAAEB0`, `0x5C616F0`) has G at +0x00, the resource CRCs at +0x08, the roll-perks flag +0x28, the crafting flag
+0x2A, the push flag +0x2B, and a count override +0x30. The backend gives G = the upgrade level, the resources = the perk items in the
order of the request, the crafting flag, and push = PerkItemsBucketPush of the recipe. GeneratePerks (`0x5C345F0`) rolls each resource
bucket with the chance bonus 0 first; then the count roll from the last ordinary bucket backward; the first G eligible buckets get the type
modifier (1.0); a slot (`0x5CD1A20`) needs CanAddPerk (`0x5C03E20`) and the type gate (`0x5C03DA0`: one Inherent). Live L5b supports the
push mapping: with the gem pins on the axe (push true) the screen keeps the row "Gem Charm Slot" of the ordinary gem bucket (two gem rows:
"Empty" and "High Chance"); with the Adderstone on the bag (push false) the charm takes the place of an ordinary row (4 rows before and
after; the largest count at 565 is 4, thus this picture does not separate the two rules). The backend allows one gem perk for each item (the one-gem rule: a decision of an agent; the screen shows the second gem row).

**The game data** (static; `crafting.py` format 3):

| Fact | Source |
| --- | --- |
| Azoth_Currency: CategoricalProgressionData, CRC `37504247`, rank 0; the cap 2,500 (MaximumInfluence of the AzothCurrency table). 20 recipes with no cause have a currency ingredient (Azoth_Currency 250 or 500). | `javelindata_categoricalprogression.datasheet`, `javelindata_azothcurrency.datasheet` |
| The perk generation data of crafting: the second value list of each tier of the player base attributes. Tier 5: 3 perks to 469, 4 at 470 to 599 and 626 to 674, 5 at 600 to 625 and 675 to 1,000; the tiers 0 and 1 have no list. The Perk, Attribute, and Gem Slot Chance Modifier are 1.0. | `sharedassets/genericassets/playerbaseattributes.pbadb` |
| MaxCraftGS: 51 recipes with no cause have an output item with one; the expansion rows: None 625, Expansion2023 700. | The MasterItemDefinitions tables; the expansion rows of `progression-tables.json` (stage 1.1b) |
| The rank gear score: MaxGearScoreBonus and its columns T2 to T5 of each TradeskillRankData table (Weaponsmithing 200: T5 25; Engineering 0: T2 5, 50: T2 55). | `javelindata_tradeskill*.datasheet` |
| The material gear score: IngredientGearScoreBaseBonus and IngredientGearScoreMaxBonus (the Prismatic Ingot, Planks, Leather 20; FaeIronT1 5; IngotT52 10). One cell has a text (MountConsum_GruelT0: "Expansion2023"): the tool takes 0. | The MasterItemDefinitions tables |
| The craft bonus of the statuses: the columns MaxGS<skill> (FoodWeaponsmithT5 15, 2,400 s; FoodEngineerT1 5; Status_Perk_Armor_Weaponsmith 2; HousingMinGearBonus_AllCraftingT3 5 for Arcana, Weaponsmithing, Jewelcrafting, Engineering, Armoring, StackMax 3). | The status effect tables |
| 694 resource items (ResourceID to PerkBucket, GemPerk, SocketType), 20 of them with a bucket that the perk data does not have. 1,917 recipes with no cause: 837 procedural (9 with PerkItemsBucketPush false), 894 with a BaseGearScore. | `javelindata_itemdefinitions_resources.datasheet`, `crafting-data.json` |

### 4.53 The quests: the Objectives state, the readiness of the pin, the auto-pin and the pin messages, the task order, the achievement actions (live proof of stage 1.8a; static proof for the client code and the game data; the grant rules are project rules with the values of the game data)

Research: `~/Games/soulwarden/work/research/quest-engine.md` (Q2), `quest-engine-3.md`, `quests.md`, and the static read
`quest-display-18a.md` of stage 1.8a. The system: `world/quests.h`, [Quests](systems/quests.md). Live checks L1, L2, L4 of stage 1.8a:
`~/Games/soulwarden/work/evidence/quests/18a/implement/live/` (`LIVE-CHECKS.md`) and `RESULT.md`. Client build 1.400.6031.6004151; all
addresses are image-relative.

**The Objectives state** (owner only; slot 22 of the player slice, type 3857 `MB::ObjectivesComponentReplicatedState`, constructor
0x3A1DF90, apply 0x3A43530; the remote player slice has no slot 22) [static, research Q2; live L1]. Two groups; the backend writes group 1
(mask `02`), fields 0, 1, 3, 4 (field mask `1B`). A full container is `00 01 sequence:size64 count:size element*` (reader 0x279DC70); the
sequence is the world revision of the last change of the state:

```text
field 0 taskStartTimes    map: instance:u64 index:u8 -> start:u64       (reader 0x3A68CB0; the synced wall clock, Unix nanoseconds)
field 1 trackedObjectives instance:u64 each, 8 at most                  (reader 0x3A68340)
field 3 activeObjectives  56-byte elements                              (reader 0x3A688F0, element 0x5B52F60)
    objectiveId:u32 instance:u64 missionId:u32 (0) destinationOverride:16 bytes (0) acceptTime:u64 (0) originTerritory:u16
    available:u8 (1) isCommunityGoal:u8 isPoiObjective:u8 isDynamicPoiObjective:u8 parentObjectiveId:u64 isResetObjective:u8
    selectedIngredients:size (0)
field 4 taskStates        18-byte elements: instance:u64 index:u8 taskCRC:u32 progress:u32 state:u8   (reader 0x3ADDB50)
```

`objectiveId` and `taskCRC` are the CRC-32 of the lower-case ObjectiveID and TaskID (`06_getoffbeach` = `E4D66020`, its task 9 =
`1B73198B`) [static; live L1]. States: 0 New, 1 Active, 2 Complete, 3 CompleteActive, 4 Inactive, 5 Failed (table 0x3AA46F0).
`acceptTime` is 0 [live L1 with 0; decision of an agent: the native TaskTimer can take its start from the creation time, research
quest-display-18a.md Q4]. `originTerritory` is the territory of the grant (6 for the start quest) [live: the start quest shows with 6].
Fields 5 and 6 are not written. Field 2 (completed: a notice list) is written from stage 1.8b (4.56).

**The order and the values of the task elements** [live L1; static 0x3A43530, 0x3ADDE50, 0x3AD6C60, 0x3AD6D20, 0x3AC0990]: the elements of a
quest are in index order (a child before its container). The client applies the elements in the received order and runs its container
callbacks between them; with the parent-first order of the research fixture the objective shows with no task line (live L1, picture
`L1-5-hud.png`); with the index order the task line "Talk to Aelstrom on the beach", the world pin, and the compass marker show
(`L1-hud.png`, `L1-journal.png`). A task is satisfied when its progress reaches its target; a container counts its satisfied children (a
simple or consecutive container has the number of its children as target, a partial container 1); a completed leaf has its full target as
progress (a TargetQty of 2: progress 2). A satisfied task with an effective StayActive (a container inherits it from any child) is 3. The
setter skips state 0 and state 4: they do not reset or deactivate a task of the client (static 0x3ADDE50). The start states of
`06_getoffbeach` (live, the new character of stage 1.8a):

| Index | State / progress |
| --- | --- |
| 0, 2, 3, 5, 6 (unlocks) | 2 / 1 |
| 1 (the lock of CaveCorruption, StayActive) | 3 / 1 |
| 4 (simple container) | 3 / 4 |
| 7, 8 (TaskAchievement), 9 (trigger), 14 (consecutive), 15 (partial) | 1 / 0 |
| 10 to 13 | 0 / 0 |
| 16 (root) | 1 / 3 |

**The readiness of the pin** (owner only; slot 73, type 5606 `SeasonsRewardsTrackedStatComponentReplicatedState`; state `01 01 01`:
group 0 field 0 `initialized` 1) [static 0x3DDABF0 -> 0x3A57CC0; live L1]. With slot 22 alone the client shows the banner of the quest and no
pin; when slot 73 comes, the client pins the tracked objective on the HUD and sends 1458 (`L1-1-slot22-only.png`, `L1-3-slot73.png`).

**The gate of 2709** (`ObjectivesComponentServerFacet_SetObjectiveAutoPinning`, sender 0x3A632C0, reader 0x39EF030: `target:u64 entity:u64
count:size` (36 at most) and one flag byte (0 or 1) for each objective type of EObjectiveType) [static; live: 53 bytes, flags
`111111011111111111111111111111111111`, CommunityGoal 0]. The client sends it in OnEnable of its objectives facet (0x3A52510), 0.7 s to 1 s
after the first record, and more than one time. The backend sends slot 73 and slot 22 only after the first one [live L1; the facet copies
the tracked list only when it is local]. The flags are the auto-pin options: the backend pins a new objective when the flag of its type is
1; with no flag yet (the start grant comes before the first record), the default of the client options.

**The pins of the client** (1458 `ObjectivesComponentServerFacet_UpdateTrackedObjectives`, sender 0x3A05DA0, reader 0x39F1590:
`target:u64 entity:u64 count:size` (8 at most) `instance:u64` each) [static; live]. The client sends it after each change of its pinned
list: at the entry first two empty lists, then the list with the pin (live, the new character of stage 1.8a). The backend stores a list
with only active instances of the character and sends no state back.

**The achievement actions and slot 66** [live L2]. The quest tasks change bits of the achievement state (4.29). In one record the client
applies slot 22 before slot 66 (it sorts by slot); in one tick the backend can also send slot 66 in a bundle before slot 22 (a large
achievement state). Both orders work: the task change shows, and the bit has its effect (live L2 variant 1: the change, then the bit
`Start_IGC_Aelstrom_WW`; the implementation: the bit `Watched_IGC_Aelstrom_MB`, then the change that completes the partial container: the
quest turns to "Talk to Aelstrom", `L2-impl-questbeach-after.png`). A lock of the last bit sends an empty list (`00 01 sequence 00`).

**6703** (`PlayerCutsceneComponentServerFacet_OnClientRequestStartCutscene`, sender 0x2EDA740: `target:u64 entity:u64 cutscene:u32`)
[live L2]: 2 s after the bit `Start_IGC_Aelstrom_WW` the client sends `4817DD08` (the CRC-32 of `06_getoffbeach_msq_revivalshoreknight_ww_igc`)
and moves its camera while it waits for the answer. Stage 1.8a writes it to the log; the answer (6707) is in 4.56.

**The timer** [static; live]: the start of a TaskTimer is field 0 (Unix nanoseconds of the synced wall clock, the clock of 4.24). The server
ends the timer at the start plus Duration with its fraction (live: `06_skinningknife` task 14, Duration 25.4, ended after 25.416 s, and its
next task unlocked `SwapActive_01_AelstromWW_03`). The client shows a time row `00:00:00` (red) under this quest from the first second; the
source of that row (the `TimeLimit` data of a task row, 0x3AD7BE0) is open (U-L4).

**A removed objective** [live]: a full active list that drops an objective with no completed entry shows the banner "Objective Failed"
(apply 0x3A43530, 0x3A559B0).

### 4.54 The quest events: the places of the client, the screens, the abandon and the reset, the quest objects, the kill counter (live proof of stage 1.8c; static proof for the client code and the game data; the rules are project rules with the values of the game data)

Research: `~/Games/soulwarden/work/research/quest-engine.md` (Q1, Q3), `quest-progress.md`, `map.md` 2.1. The system: `world/quests.h`,
[Quests](systems/quests.md). Live checks of stage 1.8c: `~/Games/soulwarden/work/evidence/quests/18c/implement/live/` and `RESULT.md`.
Client build 1.400.6031.6004151; all addresses are image-relative. No new message or state goes from the server to the client: the
changes of a quest are the task elements of the Objectives state (4.53).

**2297** (`TerritoryDetectorComponentServerFacet_AttemptTerritoryUnlock`, writer 0x29C0E70, reader 0x29C0F20; component TerritoryDetector
of the player slice, `RootPlayer/efc5d83c-a790-6a1f-f6f2-150e05d4d093`) [static; live POI1]:

```text
target:u64 entity:u64 reference(actor:16 raw, group:u64, local:u64) territory:u16 achievement:u32
```

`achievement` is the CRC-32 of the lower-case achievement name (0 for none). The client sends it when its player goes into the sphere of
the DiscoveryRadius of a point of interest (the "discovered" achievement) and into the shape of the point of interest (the "charted"
achievement) [live POI1: 40142 sends `d66d9b33` at 150 m from its placement and `39eec8c7` at the shape, 1.5 m inside the border of the
shape of the data]. While the discovery achievement is not set, the client sends 2297 again at each entry (the map of stage 2.7 sets no
bit).

**6416** (`TerritoryDetectorComponentServerFacet_AttemptTerritoryExit`, writer 0x29C2440) [static; live POI1]: the same body with no
achievement. The client sends it when its player leaves the sphere and when it leaves the shape, with no difference in the body (live
POI1: two 6416 for 40140). The backend uses 2297 and 6416 only as a cue and a log line: the membership of a place task is the shape of the
data and the accepted position of the server (a client report is no proof of a place).

**The shape of a point of interest** [data; live POI1]: `objects/worldprisms/pois/REGION/poi_ID_NAME.vshapec`: `u32 0, u32 count`,
`count` points of 3 `f32` (x, y, z; the z is 0), then 20 bytes. The points are local to the POI placement (`POI_CapitalPlot` of the
capitals of the region, `pois.capitals.json`); the height of the prism is not in the file (the tool uses 180 m). A placement is the origin
of a shape when the entries of `region.metadata` with the territory id are in the shape and the entries without it are not (6 m tolerance
at the border: the metadata has entries 0 to 5.4 m on the wrong side). 476 of 761 shape files of the build get an origin. The tool:
`tools/gamedata/pois.py`, the file `poi-data.json`.

**2617** (`ObjectivesComponentServerFacet_NotifyOpenUiScreen`, sender 0x3A055A0, caller 0x3A50F50; the Objectives facet of the player,
`RootPlayer/a42a47db-4f14-41c6-81d1-19a95ba99f84`) [static; live L1, L2]: `target:u64 entity:u64 screen:u32`. `screen` is the CRC-32 of the
lower-case name of the UI state: `a1126b78` is "hud" (the UIScreenTag `HUD` of the quest data). The client sends it at the world entry
(`a1126b78`, then `8d922292`), when the journal opens (`002f27e8`, then `6cb0696c`), and again with `a1126b78` when it closes (live L2;
the names of the other three values are open). TaskOpenUiScreen counts the CRC of its UIScreenTag.

**3836** (`ObjectivesComponentServerFacet_AbandonSingleObjective`, sender 0x3A04130, reader 0x39E8A10) [static; live L2]:
`target:u64 entity:u64 instance:u64`. The journal shows the button "Abandon" for an objective with CanBeAbandoned 1 (a faction mission in
live L2) and asks "Are you sure you want to abandon your current mission?"; "Yes" sends 3836 with the instance. After the removal of the
instance the journal shows the section with 0 objectives and the banner "PvP Missions Failed" (live L2, `AB-3-after.png`).

**5600** (`ObjectivesComponentServerFacet_ResetObjective`, handler 0x3A62210, reader 0x39E8A10) [static; live L2]: `target:u64 entity:u64
instance:u64`. The journal shows the button "Reset" for the main story objectives (CanBeAbandoned 0) and asks "Are you sure that you want
to reset your current mission? Resetting will result in the loss of all progress."; "Yes" sends 5600. The backend ends the instance and
adds a new instance of the objective; the client shows the banner of a new quest ("MAIN STORY QUEST / SOUL SURVIVOR", live L2,
`RS-2-after.png`).

**A quest object** [static 0x8261FC0; data `javelindata_variations_gatherables_quest`; live G1 of stage 1.8c]: a placement of
`slices/gatherables/master_quest_instanced.dynamicslice` shows its prompt, its beam, and its world pin only while the character has an
Active task with the InteractTag of its variation (`IO_MSQ_Treasure_A` of `06_pillageship` task 5). The client sends the request of the
gathering (4.44) for it. With the new backend the gather completes after 1 s and gives the quest drop (`0601_0104_Treasure_A`, the loot
notice "+1 Champlain's Astrolabe"); the HUD marks the task line, and the object goes out of the view of that character only (live L2,
`G1-L2-c-after.png`). After the third object the root of `06_pillageship` is complete, and the HUD shows "Talk to Grace O'Malley" (the
hand-in of stage 1.8b; `G1-L2-f-all.png`).

**The kill counter** [live K1]: the kill of a `Corrupted_Sailor_1H_Club` ("Corrupted Mariner") by a character with the credit changes the
task element of `06_crewmates` task 8 (progress 1 of 5); the HUD shows "1 / 5 - Defeat Corrupted Mariners" (`K1-1of5.png`), then
"2 / 5" (`K1-2of5.png`). A kill while the creatures ignore the character (`tools/world ignore on`) gives no credit and so no count. When
the character leaves the place of the StayActive task 6 (TaskGoToPOITag), task 6 returns to Active, task 8 becomes Inactive with its
progress, and the HUD shows the text of task 6; in the place again, task 8 is Active with progress 1 (`K1-back-1of5.png`).

### 4.55 The persons: the person children that the client makes, the presence, the conversation state, the topics, the services (live proof of stage 2.5c; static proof for the client code and the game data; the selection rules are the rules of the client, the faction values of a character are project rules)

Research: `~/Games/soulwarden/work/research/npc-conversations.md` (Q1 to Q3), `quest-engine-3.md` (Q2), `shops-stage.md` (Q5); the
static read of stage 2.5c: `~/Games/soulwarden/work/research/scratch/persons-25c-static/ANSWERS.md`. The system: `world/persons.h`,
[Persons](systems/persons.md). Live checks of stage 2.5c: `~/Games/soulwarden/work/evidence/persons/25c/implement/live/` and `RESULT.md`.
Client build 1.400.6031.6004151; all addresses are image-relative. No new message or state goes from the server to the client: a use of a
person gets the reply 3743 and the object records of 4.35.

**The person child of a PrefabSpawner** [static 0x17898F0, 0x69EE550, 0x6160B60; live L1]: the client makes the child of a PrefabSpawner
of a level placement with m_isClientAuthoratative false and m_spawnOnInitialize true at the load of the level (a person of a mission swap
slice, of the walkaway slice, of `npc_variantmaster`), with a client id of tag 7:

```text
G = ((((round(z*4) & 0xFFF) | ((bucket | (asset << 4)) << 12)) << 18 | (round(y*8) & 0x3FFFF)) << 18 | (round(x*8) & 0x3FFFF)) << 3 | 7
actor = G (u64 LE) + 8 zero bytes;  K = root XOR G;  instance = entity XOR K;  target = instance + component id
```

x, y, z: the translation of the composed transform of the child spawner (the placement matrix times its m_worldTM; 0x69EEEAE to
0x69EEEE1). bucket: (int)(the yaw in degrees, 0 to 360) / 23, 4 bits. The yaw is the Euler Z of the composed matrix in float (0x63344A0
with atan2f, cosf, sinf, fmodf; the degrees factor 57.29582595825195 at 0x85725F0; the wrap to 0 to 2 pi with tau at 0x7F400F0). asset:
the first 8 bytes of the asset UUID of the child slice, little-endian; only the low 9 bits stay. round: roundf (import 0x7ECAC28; a half
goes away from 0). Live L1: 19 of 19 ids of a trace of the client have this value (a yaw of -107.43 degrees counts as 252: bucket 10).
The request of the client names this actor and the targets of the XOR rule, as for a level object (4.35). A person child that the client
does not make (m_spawnOnInitialize false: 2 in the build) has no known source. The tool of the interaction data computes x*8, y*8, z*4
in float, as the client (the product of a float and 8 is exact), and near a border of the rounding or of a yaw bucket (0.001) it writes
each id of the candidates of each axis, the id of the float rule first (stage 1.8b, finding PR5C-R6 of the code review of stage 2.5c: the
old border rule could omit the id of the client). The data of the build keeps each of the 1309 ids of stage 2.5c (the 19 of the trace of
gate R too) and has 2 more.

**The NPC of a person child** [data; research npc-conversations.md Q1]: the column NPCId of the row of the variant (m_sliceVariant of the
spawner) in the variation table of the child slice (m_variationTableUniqueName), not the defaults of the generic slice.

**The presence for one player** [static 0x3369430; data; live A2]: the GatherableController of a swap slice shows its child by an
achievement of the local player (m_enableUseAchievementForClientState, m_achievementName; the variation action "Change Gatherable
Achievement" sets it from the column SwapAchievementId). `masternpc_swap_defaultshown`: the person goes while the achievement is true;
`masternpc_swap_defaulthidden`: it comes while it is true; `masternpc_walkaway`: it comes while AE.3_RequiredAchievementConditional is
true (its AchievementEvents script). Live A2: a bit of the achievement state (4.29) that the server sets makes the person go, and the bit
that the server clears makes it come back, with no new load (`A2-02-bit-off-return.mp4`).

**The conversation state** [static 0x37AC710, 0x37AB440, 0x37AB7A0; data; live L2, L3]: the client scans the states of the NPC row
(ConversationStateId1 to N) in reverse and takes the first valid record whose requirements pass (stage 1.8f, static read
`~/Games/soulwarden/work/evidence/quests/18f/implement/static/STATE-SELECTION.md`, proved by code; live Q1 of stage 1.8f: the offer of
`06A_MSQ_01_Quartermaster` at Marcel Dupont is accepted with the server rule): the state loader 0x379E280 writes byte +0x190 of the record
as IsInitialState XOR hasRequirements (0x379F2F0 to 0x379F377: RequiredLevel +0xD9, RequiredFaction +0xDA, RequiredFactionCooldown +0xDB,
RequiredCompletedObjective hash +0xDC, RequiredAchievement length +0xF0, RequiredProgression id +0x110 with rank +0x114 or points +0x118,
the RequiredActiveObjectiveId list +0x120 with RequiredActiveTaskId +0x150; each a loaded value that is not 0), and the selector skips a
record with byte +0x190 0 before the player check (0x37AC846 to 0x37AC870; a fallback lookup 0x37AC3F0 of the same id in the other groups
of the manager is checked again); the topics of the state whose requirements pass are
its options, and a selected topic offers its ProvidedTopics (0x3779D87). The requirements (static read): RequiredLevel (0x37AB467:
level or more), RequiredAchievement (a conditional), RequiredFaction (converter 0x37AF200: Syndicate 1, Marauder 2, Covenant 3, Any 4, each
other text 0, also a name with `!`; a state: 0 and 4 pass, 0x37AB5B4; a topic: 0 passes, 4 needs a faction, 0x37AB898; else equal to the
faction of the player), RequiredFactionCooldown (a state only: NotInCooldown 0 passes; InCooldownMultipleDays more than 24 hours,
InCooldownSingleDay 1 to 24 hours of the cooldown of a faction change; 0x37AB5F9 to 0x37AB6B5), RequiredActiveObjectiveId with
RequiredActiveTaskId (a state only; a rule only with both; an objective of the list with the task in state 1 or 3: IsTaskActiveById
0x3A49520), RequiredCompletedObjective (a state only: the completion achievement of the objective, IsObjectiveComplete 0x3A48A30),
RequiredProgression ("ID RANK POINTS", parser 0x37AF340: rank or more, and the points or more when above 0; 0x3CB82D0). Live L3: a person
shows its default state, and with the active task of a topic state the state with the topic (`L3-01-default-comparison.png`,
`L3-03-topic-options.png`).

**3158** (`ConversationComponentServerFacet_RequestConversationTopicFromClient`; caller 0x376E530, sender 0x3722AE0, reader 0x26FEC60
through 0x3707E90; the Conversation facet of the player, `RootPlayer/7447fb63-fa77-44eb-8605-6dbab2ba7a00`) [static; live L3]:

```text
target:u64 entity:u64 topic:u32
```

`topic` is the CRC-32 of the lower-case ConversationTopicId (live L3: `e532485a`, the task topic of a person). The client sends it when its
player selects a topic. No answer is known; the client shows the NPCResponse of the topic from its own data (`L3-04-topic-response.png`).
Stage 1.8b records the selection only; the effects of the topic come at 5397 (4.56).

**5397** (`ConversationComponentServerFacet_OnFinishedConversationTopicsFromClient`; close 0x37622D0, sender 0x3722540, writer
0x3701DF0, reader 0x3701E90 through 0x37080A0) [static; live L3]:

```text
target:u64 entity:u64 count:size (10 at most) topic:u32 * count
```

The client sends it at the close of the conversation, after 6705 and 2473 (live L3: 6705, then 2473, then 5397 with `e532485a`). No
answer is known. Stage 1.8b applies the effects of each finished topic here (4.56).

**The services of a state** [static: registrar 0x37B3DA0, builder 0x37ACF10, switch table 0x37AE038; data; live L4]: the names of
ConversationService1 to N have the numbers Inn 1, Faction_Board 2, Community_Board 3, Faction_Selection 4, Join_Outpost_Rush 5, Event_Store
6, Horse_Store 7, Wolf_Store 8, Lion_Store 9, Expansion_Modal 10, Fast_Travel_Guide 11, PvP_Guide 12, Join_3v3_Arena 13, Transmog 14,
Barbershop 15, Open_Shop 16, Open_Mission_Board 17 (Invalid 0: no option); the builder adds an option for each (Inn: option type 10,
Open_Shop 24). Faction_Board needs the faction of the state and its level (0x37A7940), Faction_Selection also passes for Any (0x37AD46C).
Live L4: an innkeeper (`0222a_Innkeep`) shows the Inn option; its selection sends 2473 for the conversation and a new interaction request
of the action 70 OpenInnScreen (option `6cb6b4f9`) to the same object.

**The close** [live L5]: after the close of a person conversation (the key, or 3746 of the server), the session answers 6705 with 6709 as
before (4.20), and the character walks at once (`L5-walk-after-close.mp4`, `L5-walk-after-server-cancel.mp4`). A new world entry shows the
prompt of the person again, and E opens it (live L6).

### 4.56 The offers, the hand-in, the cutscenes, the cinematics, the whispers, the completed notice, the trigger areas (live proof of stage 1.8b; static proof for the client code and the game data; the checks and the reward rules are project rules with the values of the game data)

Research: `~/Games/soulwarden/work/research/quest-engine.md` (Q3 to Q6, K8), `quest-engine-3.md` (the beach chain, G8, G9, G13), and
`conversations.md` (the request table, the cutscene handshake). The system: `world/quests.h`, `world/persons.h`, [Quests](systems/quests.md),
[Persons](systems/persons.md). Live checks of stage 1.8b: `~/Games/soulwarden/work/evidence/quests/18b/implement/live/` and `RESULT.md`.
Client build 1.400.6031.6004151; all addresses are image-relative. Each request below has the prefix `target:u64 entity:u64` of its facet
and one `u32`: the CRC-32 of the lower-case name (reader 0x26FEC60).

**3772** (`ConversationComponentServerFacet_RequestAcceptObjective`; sender 0x3722700, caller 0x376DFF0; the Conversation facet of the
player) [static; live I2]: `objective:u32`. The client sends it when its player accepts an offer of the selected state of the person (option
0x3779AE0, check 0x375A980); the window shows the accept text at once (response type 4, before a state comes). Live I2: `d137c605`
(`06_skinningknife`) after the hand-in of `06_getoffbeach` at Aelstrom. The backend answers with the Objectives state of the new instance
(no other answer is known). A refused accept gets no answer.

**794** (`ConversationComponentServerFacet_RequestCompleteObjective`; sender 0x37228F0, caller 0x376E2C0, the option 0x3749B80; the
Conversation facet) [static; live P1, I1]: `objective:u32`, no instance and no NPC. The client shows the option "Complete the Quest!" for a
quest whose NpcDestinationId is the person and whose root is complete (live P1: `e4d66020`, `06_getoffbeach`, 20 bytes). The backend finds
the active instance of the character; the answer is the Objectives state (the instance in field 2, then not in field 3), the achievement
state, the progression, and the item records. A refused hand-in gets no answer: the client removes the option and waits in the conversation
(live I2 at Urda, the Meat Pouch reward of `06_skinningknife`).

**The completed notice** (field 2 of the Objectives state, `completedObjectives`: `instance:u64` each; reader 0x3A68340, content 0x3A68E90)
[static: apply 0x3A43530 calls 0x3A559B0 with the completion flag only for a removed instance in field 2; live L3]. With the field mask
`1F` the record that removes a handed-in instance names it in field 2; the client then shows the completion and no "Objective Failed" (live
L3: `06_demonstorm` leaves the HUD and the journal, `L3-hand-in-completion.mp4`). The backend keeps the 16 newest instances of the world
entry and writes field 2 only when it has an instance (the bytes of a quest with no hand-in do not change).

**6707** (`PlayerCutsceneComponentClientFacet_OnServerCutsceneRequestGranted`; handler 0x2EFE880; server to client, owner only)
[static; live C1, I1, C2]: `target:u64 entity:u64 cutscene:u32`, the PlayerCutscene component of the player slice as for 6709 (4.20).
The handler compares the CRC with its pending cutscene and plays it (0x2EF13F0); a mismatch can send 6705 with no play. Live I1: the
character of a new creation sends 6703 `4817dd08` at its first accepted position in Aelstrom_IGC_Trigger, the backend grants it, and the
cutscene plays (`I1-cutscene-and-walk.mp4`); live C2: the Grace cutscene `848caecf` of `06_survivors` at its trigger area. A 6703 that no
active TaskCutsceneComplete of the character names gets no answer (the client waits, then gives up).

**6706** (`PlayerCutsceneComponentServerFacet_OnClientCutsceneStarted`; sender 0x2EDA550, caller 0x2EF13F0) [static; live C1, I1]:
`cutscene:u32` (75 ms after the grant in live I1). The backend marks the pending cutscene of the character as started. The 6705 after it (the end, 4.20)
completes the tasks of the cutscene; the session answers 6709 for each 6705, as before. A 6705 with no started pending cutscene changes no
quest.

**6606 and 6542** (`PlayerCutsceneComponentServerFacet_NotifyCinematicStarted`, sender 0x2EDA180, caller 0x2EF9A00;
`..._NotifyCinematicEnded`, sender 0x2ED9F90, caller 0x2EF81F0) [static; no live check: the cinematic `TempestRising` of `06_raisealarm` is
after the refused hand-in of the chain]: `cinematic:u32`. The backend counts 6606 for TaskCinematicStart and keeps it as the pending
cinematic; 6542 counts for TaskCinematicComplete only for the pending cinematic. No answer is known.

**5070** (`WhisperPlayerComponentClientFacet_RequestStartWhisper`; reader 0x4304980; server to client, owner only) [static; live W1, W2]:
`target:u64 entity:u64 whisper:u32`, the CRC-32 of the lower-case WhisperId. The target is the Whisper component (`7D5CA09D7B3CC6FA`) of
the entity `Whisper` (`000000A02DBCEA72`) of the player slice with the XOR rule of a slice child: `(entity XOR root XOR G) + component`.
Live W1: `69128a14` (`01_Whisper_Beach_05`, a development message): the client plays it and shows its subtitles (`W1-subtitles.png`); live
W2: `52f36e05` (`01_IsabellasFirstWhisper_Isabella_01`) from TaskTriggerGameEvent of `06_survivors_soldier` (`W2-whisper-trigger.mp4`).

**The trigger areas** [live T1]: the client sends no message for an ObjectiveDetectionVolumeComponent of a quest: no 560 at the entry of
Aelstrom_IGC_Trigger and no message at a spawn in it. The server tests the accepted position of the character (4.7) against the volumes of the trigger data.

**The finish of a topic** [live T2]: the client sends 3158 at the selection of a topic and 5397 with the topic at the close (4.55). The
backend applies the AchievementUnlock of the topic and counts TaskConversationTopic at 5397. Live T2: `06_demonstorm` at Grace: the bit
`01_Grace_Goodbye_WW` at the close, the next task locks `SwapActive_01_GraceWW_C`, and Grace walks away after the conversation
(`T2-topic-finish-walkaway.mp4`).

### 4.57 The open of a loot container, the territory standing, the repeat of a quest, the countdown row of a timer (live proof of stage 1.8d; static proof for the client code and the game data; the reward join, the repeat cycles, and the standing territory are project rules with the values of the game data)

Research: `~/Games/soulwarden/work/research/scratch/quests-18d/rules/RESULT.md` (Q1 to Q9) and `timer/RESULT.md`. The systems:
[Items and equipment](systems/items-and-equipment.md), [Quests](systems/quests.md), [Progression and skills](systems/progression-and-skills.md).
Live checks of stage 1.8d: `~/Games/soulwarden/work/evidence/quests/18d/implement/live/`, `live-final/`, and `RESULT.md`. Client build
1.400.6031.6004151; all addresses are image-relative.

**1733** (`CraftingComponentServerFacet_InitiateSalvage`; native call 0x2E69D20, check 0x2E4AB20, sender 0x2E220D0 at 0x2E69E89, writer
0x2DF3000; the Crafting facet of the player) [static; live D1 and F]: 48 bytes:

```text
+0x00 target:u64
+0x08 entity:u64
+0x10 containerType:u32       1: the bag
+0x14 slot:u32
+0x18 instance:16 bytes
+0x28 locationQuantity:u32    0 in the live checks (0x2E69D20 sets it to zero at 0x2E69E21)
+0x2c salvageQuantity:u32     1 for Open
```

The bag action "Open" of an item of the class LootContainer (`dynamictooltip_commands:OnOpen`, `BoxOpenHelper`) and a double click
(`newinventory:ItemDoubleClickAction`) send it with the quantity 1; no screen has a quantity selector [UI scripts]. Live D1 and F: the
Meat Pouch (`QuestReward_MB_Meat_Pouch`, "Essential Supplies Cache") in bag slot 1: `salvage request (type 1733, bag slot 1, quantity 1,
location quantity 0)`. The backend answers only with the bag state.

**The bag state with the cause OpenRewardChest** (slot 61, type 1755, `ActionCausingSync` 26) [static: the dispatcher 0x48E2CC0 tests the
action 26 at 0x48E2E95 and selects the reward chest branch; live D1 and F]: one record removes one unit of the container (the cause 2,
ItemRemoved) and adds the items of the roll with the cause 26. The client then runs `rewardcelebrationpopup:OnRewardChestOpened` and shows
its reward popup with the items (live F: the Weak Health Potion and 10 Travel Rations, `F-37-pouch-open.mp4`). The cause 33 (SalvageReward)
selects the salvage branch at 0x48E2EAA; it is not used. The loot table of a container is the RepairRecipe `[LTID]NAME` of its item row
(0x2E57210 reads it, 0x3687F10 removes the prefix) [static; data].

**The territory standing** (slot 65, type 911, the categorical progression) [data; live I0]: the standing of a territory is an entry
with the id CRC-32 of the territory number (Windsward "6": `1DB87A14`), the rank (u16), and the points in the rank (u64). Live I0: the
entry of rank 0 with 120 points makes the map of the client show "120 / 150" for Windsward (`I0-17-standing-120.png`). The first try had
one byte too many in the u64 of the points: the client wrote "Failure during read of ReplicatedStateBundle" and dropped the record. Stage
QG1 (4.59): a rank-up, its token (ProgressionPoint, slot 67, type 3681), and the card choice (4040).

**The offer of a quest that does not pass** [static: eligibility 0x3A4BFE0, the person check 0x37AE910; live I2 of stage 1.8d]: the client
hides an offer whose checks fail (no locked display). Live F: Urda offers no quest before the hand-in of `06_skinningknife`, and the offer
of `06_boarfeast_soldier` ("Campfire Feast") at once after it; 3772 `41bb821e` accepts it (`F-31-offer-unlocked.png`).

**The repeat of a quest** [static: the reset defaults 0x616C670, instructions 0x6181D4B to 0x6181F87; the person answer OnCooldown 8 at
0x37AE910; no live check]: the daily cycle starts at 05:00 UTC, the weekly cycle on Tuesday at 05:00 UTC. The completion achievement of a
repeatable objective stays set to the start of the next cycle; the server then removes it in the achievement state (slot 66), and the person
offers the objective again.

**The countdown row of a timer** [static: the publisher 0x3B2FD90, activation 0x3B2D250, `objectives/hud/pinnedobjective.luac`
`SetObjectiveId`, `OnTick`; live I3 and F]: the HUD row below an objective reads `Hud.LocalPlayer.MissionTimers.<instance>.TargetTime`.
The client publishes it at the bind of the TaskTimer (0x3B2FD90, the value of `task + 0x510`) before the activation (0x3B2D250) sets the
deadline, and no later code publishes it again. With the elements of the Objectives state (field 0, the start; state 1, Active) the row
shows a red 00:00:00 (live F: "Basic Needs" and "Amidst the Wreckage"; `T1-05-hud-timer-row.png`). The timer of the server runs and ends
with its fraction (live T1: 25.415 s, then Aelstrom 03 shows). No server field that sets the published value is known: open (research
quests-18d timer U5).

### 4.58 The destructible quest objects, the cinematic TempestRising, the countdown row of a timer (live proof of stage 1.8e; static proof for the client code and the game data; the strike rule and the work of an object are project rules with the values of the game data)

Research: `~/Games/soulwarden/work/research/scratch/quests-18e/` (`destructible/ANSWERS.md`, `cinematic/ANSWERS.md`,
`timer/ANSWERS.md`). The systems: [Quests](systems/quests.md), [Chests](systems/chests.md). Live checks of stage 1.8e:
`~/Games/soulwarden/work/evidence/quests/18e/implement/live/`, `live-final/`, and `RESULT.md`. Client build 1.400.6031.6004151; all
addresses are image-relative.

**A destructible quest object** (the corruption nodules of `06_survivors`, the monolith of `06_demonstorm2`: the variation table
`Gatherable_Quest_Damageable`) [data; static]: the client makes the object from its level data (a managed actor of the placement:
G = the XOR of the big-endian halves of the placement UUID, the low 3 bits 3; the actor id is G little-endian and 8 zero bytes). Its slice
has a DamageReceiver (slot 1), the VariationData (slot 2), the PositionInTheWorld (slot 3), the GatherableController (slot 4, with
`m_isDamageableGatherable`), Interact (slots 5 and 6), and a Marker (slot 7); it has no Vitals and no HitVolume. The melee contact collector
of the client (0x485FBA0, the append at 0x4861DFA) takes only an entity with a HitVolume: the input report of a swing names no contact on
the object, and the key E has no option ("DESTROY CORRUPTION NODULE" is the marker text). The append (0x4861B7E to 0x4861DFA) needs
the HitVolume and the DamageReceiver (0x3FC4660) of the target [client]; the branch that each swing of live I0 took was not traced. The
ranged reports (list 1, list 2) do not use this gate (their query filters accept a target with no HitVolume) [client 0x256C980,
0x48633C0, 0x4863760; no live check]. The health bar of the marker is a fallback of
`marker.luac` (HealthPercent 1). The object shows only while an active task has its InteractTag (InteractOnlyOnQuest, 0x8261FC0).

**The destruction** [live I0 and F]: the object is an instanced object (InstancedLootType 4 of the data). The GatherableController state
(type 12) has no effect for it: its apply (0x3371590) returns when the instanced reference is valid (live I0: Depleted in slot 4 changed
nothing). The LootTracker state of the player (slot 33, type 982, owner only, 4.41) with an entry of the managed actor of the object, the
state Looted (2), the kind 0, and the position of the placement removes the object, its marker, and its corruption; a map with no entry
brings it back (live I0: `I0-07-looted-claim-destroys-nodule.mp4`, `I0-08-restore-crops.png`). The server finds the hit itself (each strike
window of the attack graph of the server, 5.0 m and 90 degrees of the look) and counts one unit for each strike; at the last unit it sends
the entry (live F: four single strikes destroy each nodule, `F-97-nodule-destroyed.mp4`; the fourth nodule starts the outro cutscene
`2b76de11`, `F-98-last-nodule-and-outro.mp4`; one strike destroys the monolith, `F-146-monolith-destroyed.mp4`). The game event of the row
(`MSQ_CorruptedNodule_1_interact`) counts for TaskGameEvent in the server; the client gets the new task state in the Objectives state.

**6606 and 6542 of TempestRising** [static: the cinematic facet trigger 0x2CC7A90, 0x2CC8260, the start 0x2CC7E70; the Bink play
0x6506CB0; the senders 0x2EDA180 (6606) and 0x2ED9F90 (6542) through 0x2EF9A00 and 0x2EF81F0; live C1 and F]: the timeline of the statue
lead-in cutscene (`06_RaiseAlarm_MSQ_AncientStatueLeadin_WW_IGC`, layer `Trigger_Bink_Video` at 9.7 s) starts the cinematic; no server
message starts it. The client plays `LyShineUI/Videos/TempestRising.bk2` (Bink 2, 106.7 s) and sends 6606 at the start event of the player
and 6542 at its finished or stopped event (a hold of Escape of 2.25 s stops it: skipped). Each body is `target:u64 entity:u64
cinematic:u32` (TempestRising: `40214776`). The play function returns before it opens the file when `sys_audio_disable` is above 0: a
development run (no audio) shows a white screen with the subtitles, never ends the video, and sends neither message (live C1 and F:
`F-150-statue-cutscene-and-cinematic.mp4`). No server answer to 6606 or 6542 is known; 6709 does not close the video screen. The play with
audio has no live check (a development run must not make sound).

**The countdown row of a timer** [static, research `timer/ANSWERS.md`; no live trace]: a new TaskTimer has the deadline 0; the objective
initialization (0x3A551C0) binds the timers of the tree (0x3B2FD90) before the activation of the root, and the bind publishes the zero
deadline to `MissionTimers.<instance>.TargetTime` (the key at 0x3B2FDCA, one node for each objective); the activation and the tick do not
publish again; the task states, the parent transitions, a resend of the active list, and 5684 do not bind again. The UI data build
0x3AD7BE0 reads the TimeLimit of the task (+0x1F4). No server field or message that makes the row count is known: open.

### 4.59 The territory standing: the rank-up, the tokens of the territory pools, the card choice 4040, the card values (live proof of stage QG1; static proof for the client code and the game data; the territory of a hand-in outside each territory and the GainGatherRate rule are project rules)

Research: `~/Games/soulwarden/work/research/territories.md` section 5 (verified study), the static read of stage QG1
(`~/Games/soulwarden/work/evidence/quests/qg1/implement/static/ANSWERS.md`), the live runs of stage QG1
(`~/Games/soulwarden/work/evidence/quests/qg1/implement/RESULT.md`). The addresses are image-relative (client build 1.400.6031.6004151).

**The rank-up** [static: 0x3CAACF0, 0x3CAAF15, 0x3CAAFDF, 0x3CAB113, 0x3CB7500; data: Territory_Standing, CategoricalProgressionData]:
the points of a gain go into the rank; equality with MaximumInfluence enters the rank path; the threshold goes, the rank goes up, and the
rest goes on (one gain can pass more than one rank). Each new rank gives `1 + AdditionalProgressionPoints` points to the pool of the row
(0x3CB772A; the standing rows have no additional points: one token). At MaxLevel 300 (MaximumInfluence 0, AllowOverflow false) the rest
is dropped (0x3CAAD94). The experience of a new rank: XpReward of the new rank times TerritoryStandingXpModifier of the territory (+0x6C,
0x31FD6AC; a value of 0 is no multiplication), as the game event CategoricalProgressionRankUp (UniversalExpAmount 1, 0x31FD6F3), then the
character experience rules (the GainXP card, below). Live (`live/20-saatvik-page2.png`): the hand-in of `06A_MSQ_01_Quartermaster` with
120 points of Windsward gives "+100 Windsward Standing" and "+1325 XP" (1,200 + 100 x 1.25); the map shows "Newcomer in Windsward", rank
1, "70 / 375" (`live/22-map-standing-after.png`).

**The points state with the territory pools** (slot 67, type 3681; 4.13): a territory pool `TerritoryRankBonus_<territory>` (Windsward:
CRC-32 `CCDDF19B`) has the category 2 (Territory) in `poolCategories`, its free tokens in `poolCounts` (u16), and its cards in
`pointEntries` (`poolId:u32 pointId:u32 level:u16`; TB_GainStanding `68C1F9AA`). The backend sends each weapon pool (category 1) and
then each territory pool of the standing data in one fragment with each entry: the client removes a pool that a fragment does not have, and
the entry list of a fragment replaces its list (live stage 1.7). The free tokens are the standing rank minus the spent levels; a card with
DoNotSpendPoint spends none [static 0x3CE0370, 0x3CF0600]. The bytes of the fixtures of the research (`standing-pool.hex`, 28 bytes;
`standing-card-selected.hex`, 38 bytes) are the bytes of the encoder (case QG1B). Live: the map shows the token badge "1" on the Windsward
row, and the screen "Claim your reward" shows "Windsward Standing Points 1" (`live/23-card-screen.png`).

**The card choice** [static: the request 0x3CFD4A0, CanSpend 0x3CE7490, the sender 0x3CD8E80, the writer 0x3274450; the server handler
0x3CFD510 through the dispatch 0x3CCC340]:

| Type | Name | Body | Answer |
| ---: | --- | --- | --- |
| 4040 | `ProgressionPointComponentServerFacet_RequestSpendTerritoryStandingPoint` | `target:u64 entity:u64 progressionId:u32 pointId:u32` (24 bytes) | the points state (slot 67) |

`progressionId` is the CRC-32 of the territory id text (`1DB87A14` for "6"), not the pool. The checks in the order of CanSpend: the
standing row and its pool; the card is in the rank row whose index is the count of the spent levels (`AvailableProgressionPoint1` to 4,
0x3CE7501); a free token, also for a card with DoNotSpendPoint (0x3CE7564); RequiredCategoricalProgressionLevel against the standing rank
(0x3CF3770); MaxLevel (0x3CE75AC). A card with DoNotSpendPoint (TB_PurchaseHouse) gets its level and keeps the token (0x3CE6083). The
client changes no state of its own at the request (no prediction in 0x3CFD4A0); the Lua callback reports success at once
(`RedeemTerritoryReward`). The backend sends the points state again after a refusal. While TB_PurchaseHouse is available the client shows
it alone (`ProgressionPointsToRewards`), and the popup closes after a choice with the last token. Live (`live/23` to `live/33`): the house
alone, then GainStanding +5.0%, GainXP +3.0%, GainGatherRate +5.0% (row 0); after one spent token Station Fee -5.0%, Trading Fee -5.0%,
Storage +25.0 (row 1); after two, "Standing Gain (+5.0% -> +9.3%)" (row 2: the next level of a card of level 1); a restart keeps the
standing, the tokens, and the cards ("Storage (+25.0 -> +50.0)").

**The card values** [static 0x5C51100; data: Territory Bonus Data of `sharedassets/genericassets/playerbaseattributes.pbadb`]: the bonus
of level n is the sum for i = 1 to n of `max(MinValue, f32(f64(InitialBonus) * pow(f64(f32(1 - ReductionModifier)), i - 1)))` with an f32
sum. The kind of a card is its Territory Bonus Id (1 GainStanding to 10 HousePurchase, the enum at 0x3CFAE8E). The client shows the same
values (live: +5.0%, +9.3%, +25.0).

**The cards that a system applies** [static, ANSWERS.md Q2 and Q3]: GainStanding: the standing of a grant is
`trunc(f32(f32(1 + bonus) * standing) * multiplier)` with the card of the territory of the grant (0x31F7D19 to 0x31F7D37; the multiplier
of a game event is 1, 0x31FD1CE). GainXP: the character experience of a game event is `trunc(f32(experience * (1 + bonus + other
terms)))` with the card of the detected territory of the player (0x3CEC0CA, 0x3CF04A9, 0x3CF0AE0); a debug grant has no multiplier.
GainGatherRate: no client formula was found; the backend adds the bonus to the speed sum of a gather (a project rule). The other cards
(GainStorage, the taxes, GainFactionRep, GainHousePoints, HousePurchase) are stored; no system of the backend reads them yet.

**The territory of a standing** [static: the grant 0x31F5400 reads the territory of its context (`GameEventParams +0xA`, 0x31F6D2B) and,
with 0, the detected territory of the player (`GetDetectedTerritoryId`, 0x2832410, 0x29F77A0); the writer of the context of an objective
completion was not found]: the backend gives the standing of a hand-in to the territory at the position of the character (the bounds of
`objects/worldprisms/bounds/territory` of the data), and to the origin territory of the instance outside each territory or in a territory
with no standing row (a project rule). The reward preview of an offer and the journal show the standing as a number with no territory
(`objectivedatahelper GetRewardDataFromGameEventData`); the reward notice of the hand-in names the territory ("+100 Windsward Standing").

### 4.60 The small quest rules: the category rule of a kill, TaskHaveLevel, the salvage event of a container, the Azoth amount, no arc gate, the push (live proof of stage QG2a; static proof for the client code and the game data; the payment of the Azoth amount, the schedule of the push, and the open paying only coin are project rules)

Research: `~/Games/soulwarden/work/research/quest-gaps.md` (QG0), `quest-gaps-qg2b.md` (QG2b), the static read of stage QG2a
(`~/Games/soulwarden/work/evidence/quests/qg2a/implement/static/category/CATEGORY.md`), the live runs of stage QG2a
(`~/Games/soulwarden/work/evidence/quests/qg2a/implement/RESULT.md`). No new message and no new state: the rules use the Objectives state
(slot 22, 4.53), the achievements (slot 66), the currency state (slot 60), the categorical state (slot 65), and 1733 (4.57). The rules
are in [Quests](systems/quests.md), section of stage QG2a.

**The category of a kill** [static: the event callback of TaskKillContribution 0x3B171D0 (function pointer 0x8277688) calls the predicate
0x3B19F40 with the base vitals id (event +0x6C) and the separate category (event +0x70) of the victim]: a task category Q matches the base
vitals id (0x3B1A057), the separate category (0x3B1A078), the parent of the separate category (one lookup of the category row 0x3A375A0,
row +4 against Q: 0x3B1A081 to 0x3B1A098), each category of the base row (the vector of row +0xF0: 0x3B1A0D0), and the parent of each of
them (0x3B1A0DA to 0x3B1A0F3). No lookup of a parent of a parent. The loader of VitalsCategoryData 0x5BBF960 keeps GroupVitalsCategoryId at
row +4 (0x5BBFD1C); a failed lookup gives a row with parent 0. The quest drop helper of ItemDropVC 0x5C75830 has the same parent rule with
no base vitals id test. The replicated vitals state has vitalsId (state +0xF48) and vitalsCategoryId (+0xF70; constructor 0x671E040): the
separate category of a creature is a value of its state, the base-row list is local game data. Live: the kill of a Bronzeroot Soldier
(`Dryad_Soldier_31`, separate category `Dryad_Soldier_02BW04`, parent `Bronzeroot`) changes the journal line "0 / 3 - Destroy Bronzeroot in
Postrekhin" to "1 / 3" (`shots/15-journal-kill-before.png`, `shots/18-journal-kill-after.png`).

**TaskHaveLevel** [static: factory 0x4461150 case 48, constructor 0x3B01C80 (base target 1; TargetQty +0x550, ShouldCheckPoints +0x554,
the progression id +0x558), activation 0x3B04F40, the level callback 0x3B073A0, the points and rank callbacks 0x3B07100, 0x3B07140]: with
no progression id the task is satisfied at the level of the screen TargetQty or more; the Objectives state has its progress 1 and its
state. No message.

**The open of a container with a salvage event** [data: SalvageGameEventID of MasterItemDefinitions; live]: the client sends 1733 for the
double click of a "Bag of Coin" (`QuestReward_WW_Coin_Purse2`, no RepairRecipe) like for a container with a table (4.57); the server pays
CurrencyReward (2500: 25.00 coin) and sends the container state (the stack 2 to 1) and the currency state (986.02 to 1,011.02)
(`shots/07-bag-before-purse.png`, `shots/09-bag-after-purse.png`). The rule of the original server for the other rewards of a salvage
event is not known (the open pays only coin: a project rule).

**The Azoth amount** [static: the integer getter 0x5AC0ED0, cvttss2si at 0x5AC0F7B; the event loader 0x31E8025, ax at row +0xC
(0x31E802A); the chance at +0x34 (0x31E806F) has no reader in the previews: objectivedatahelper.luac, research quest-gaps-qg2b.md section
2]: the offer, the journal, and the hand-in screen show the truncated 16-bit amount (37 for 37.5). Live: "Dentistry Artistry" shows 37 in
the journal and at the hand-in (`shots/25-journal-azoth-preview.png`, `shots/28-hamza-talk-3.png`), and the categorical state of Azoth
goes from 393 to 430 (`shots/26-azoth-before.png`, `shots/29-azoth-after.png`).

**The eligibility of an objective** [static: 0x3A4BFE0; loader 0x445CA30, the objective constructor 0x3A1B560 at `[rbp+0x110]`; research
quest-gaps-qg2b.md section 1]: row +0x20 is QuestArcTag, +0x24 ScheduleId, +0x28 AchievementId, +0x30 RequiredEntitlementId, +0x34
RequiredGameModeId. The check at 0x3A4C0A3 to 0x3A4C0E6 is RequiredGameModeId (the query 0x2CE2150 is IsInGameMode of the
GameModeParticipant component, 0x32BA3B0: a match of an active mode of slot 24, type 3312); the eligibility does not read QuestArcTag.
The study QG0 and an earlier study named the check of +0x34 the arc gate: wrong (QG2b). The arc tag groups the QuestArc meta-achievements after a completion (0x38ACDC0
calls 0x38A6E20: row +0x20 against meta row +0xDC, type 0x13).

**PushObjectiveOnReqMet** [static: the loader keeps ReqMet at row +0x368 (0x445E55C) and Retrigger at +0x369 (0x445E5A4); the ReqMet rows
go into a list of the data manager (+0xC90, 0x445EB7F to 0x445EBCA); the eligibility reads ReqMet at 0x3A4C20F and then ExclusiveTerritory
(+0x328, 0x43D7D00); no client request starts a push]: the server grants a push row through the Objectives state (a new active objective
of the source push, with its automatic pin) and its first task actions through the achievement state, in one record of the commit. Live:
the entry of the development character shows "Acquire a Mount", "Faction Introductions", "Orbs of Power" in the journal
(`shots/03-journal-mount-push.png`, `shots/04-journal-side-push.png`); the bit `06A_MSQ_03` adds "Learn to Craft" (Side Stories 3 to 4,
`shots/05-journal-push-change.png`, `shots/06-journal-push-change-new.png`). The schedule and the Retrigger rule of the original server
are not known (project rules: the entry, a change of the bits or of the level; Retrigger is a grant of one time).

### 4.60 The quest chest drops, the proximity quest objects, the glyph locks, the variation of a bound (live proof of stage QG5a; static proof for the client code and the game data; the place of a chest drop, the instant find of a proximity object, and the per-character respawn are project rules with the values of the game data)

Research: the static read of stage QG5a (`~/Games/soulwarden/work/evidence/quests/qg5a/implement/static/ANSWERS.md`), the live run of
stage QG5a (`~/Games/soulwarden/work/evidence/quests/qg5a/implement/RESULT.md`). The addresses are image-relative (client build
1.400.6031.6004151). No new message or state: the backend uses the Objectives state (4.53), the bag and its loot notice, and the LootTracker
state (4.41).

**ChestDropProbability** [static: the item task field +0x6A4 (reflection 0x3B0AAF0, loader 0x445EDE0 at 0x445F71B); the description
0x825F500 "0-100, probability to drop items for loot chests/POI loot"; the roll of the task 0x3B0B290: `floor(random(0, 100)) <= chance` at
0x3B0B2F3 to 0x3B0B336, one item (0x3B0B359); PauseAndClearLootOnTaskComplete +0x68D at 0x3B0B2D6; the item tasks of the enum values 25,
26, 29, 30 (factory 0x4461150 at 0x446235D); the data check 0x3A6D4D0 sums the chest counts of the territory record of the task (0x3A6E2E3
to 0x3A6E30A, message 0x8259070)]. The chest open, its receiver, and its place filter of the original server are not in the image. The
backend gives the quest item in the result of the open (one grant with the chest loot, the cause ItemGathered: the loot notice). Live:
`02A_Side02_02_EarthProvide` at the Ancient Chest of Postrekhin (`ancient_ruins_40214`): the notice "+1 Alkahest" with the chest loot
(`shots/I1-04-chest-opened.png`), the journal "0 / 2" before (`shots/I1-03-journal-scroll.png`) and "1 / 2 - Collect Alkahest from chests in
Postrekhin" after (`shots/I1-05-journal-after.png`).

**The proximity quest objects** [static: the server facet field m_isProximityGatherable of the GatherableController (string 0x81C4F38,
reader 0x3378C1D); the messages 5078 OnEnteredProximityGatherable (u32, the key of a GatherableData row; decoder 0x26FEC60, handler
0x4900030) and 5118 OnExitedProximityGatherable (handler 0x49002B0) go from the server to the client: the handler writes the tags of the row
(+0x70) to the data path `Hud.LocalPlayer.Gathering.GatherableTags` and sends nothing; the sender of the server is not in the image; data:
the slice master_quest_proximity_instanced has a DetectionVolumeComponent with a sphere (the column DetectionVolumeSphereRadius, the action
"Change Sphere Shape Radius") and an Interact component with no option]. The client sends nothing at a proximity object: the server finds
the character in the sphere. The backend does not send 5078 or 5118 (the HUD value has no use that a live check showed). Live P1: the walk
over the trail `04A_Side01_01_Proximity_Trail1` (0.71 m of 0.75 m) gives no request of the client; the server completes the task, the client
plays the effect of the depletion and the progress line of the HUD, and the beam of the trail goes (`videos/I2-trail-walk.mp4`,
`shots/I2-05-find-effect.png`).

**The per-character state of a quest object** [data: InstancedLootType 4 (Quest, 0x36FDA37 to 0x36FDA47) of the quest slices; the rule of
4.58]: a Looted entry of the LootTracker state with the managed actor of the object hides an instanced quest object for its character
(live: the trapped boar of `09A_S1_SaveAnimals` and its marker go after the gather, `shots/I4-02-boar-after.png`). A glyph tablet (no
instanced slice) keeps its prompt after the gather (`shots/I3-02-glyph-after.png`); the server refuses a second gather until the respawn.

**The glyph locks** [static: the paperdoll slot 38 of the Azoth Staff (AZOTHSTAFF_SLOT, 0x5CAB873 to 0x5CAB883; the tool query 0x48FF240);
the rank gate of the row (Tradeskill +0x2C, RequiredTradeskillLevel +0x2E: loader 0x440D879 to 0x440D8E6, condition 0x33732F0); the
column RequiredAchievement of a variation row goes to the required achievement of the Interact component ("Set interaction req
achievement": 0x42D4BF0, 0x35D5FA0), which the validation 0x35C2E60 and the prompt 0x35D7970 evaluate as an achievement conditional
(0x2ED8BB0)]. Live: with Darkness_AzothStaff_T1 in slot 38 and the rank 5 of AzothStaff the client shows "E UNSEAL ANCIENT GLYPH", the gather
takes 3 s, and `16_Side_01_Glyphs` goes to "Read the Ancient Glyph" (`shots/I3-00-glyph-arrival.png`, `shots/I3-02-glyph-after.png`).

**The variation of a bound** [static: "Set Interact Tag" 0x42D5240 calls the setter 0x35D60F0 (the tag and its CRC: 0x35D6117 to 0x35D6155);
"Set InteractOnlyOnQuest" 0x42D4B70 and 0x35D5EF0; the quest gate reads the effective tag and flag at 0x35C36CC to 0x35C36E1]: the selected
variant (the placement, else m_selectedVariant of the slice) sets the tag, InteractOnlyOnQuest, and the required achievement of the Interact
component of the entity of each action. A blank cell of the row: not proved (no such case in this build).

**RequireControllingFaction** [static: the gatherable loader 0x440D2A0 reads it with the flag converter 0x5BF6BC0 at 0x440DC07, as
IsLootContainer (0x440E41C) and ResetGatherTimeOnEndInteraction (0x440E3D4)]: a flag; the cell "0" of 557 rows is no rule.

### 4.61 The ObjectiveFlow of a non-persisted achievement, the restriction ItemRequired, the categorical progression reward of a success event, the quest gimmicks (live proof of stage QG5b; static proof for the client code and the game data; no removal of the item of a gather, the trade skills that a reward pays, and the form of a quest gimmick are project rules)

Research: the static reads of stage QG5b (`~/Games/soulwarden/work/evidence/quests/qg5b/implement/static/A/ANSWERS.md` and
`static/B/ANSWERS.md`), the live run of stage QG5b (`~/Games/soulwarden/work/evidence/quests/qg5b/implement/RESULT.md`). The addresses are
image-relative (client build 1.400.6031.6004151). No new message or state: the backend uses the Objectives state (4.53), the bag, the
categorical progression state (type 911, slot 65), and the LootTracker state (4.41).

**ObjectiveFlow** [static: the NPA loader 0x43B5510 reads the column at 0x43B57F3 and calls the parser 0x43BD930 at 0x43B5827; the split at
"," 0x43BDBBF; the prefix 0x43BDF9F, only `u` (0x833DEB4) and `l` (0x833DEB8) (0x43BDFCC to 0x43BE049), another prefix leaves the clause out
(0x43BE66E); "/" and ":" split the objective, the task, and the modifier; the ids are CRC-32 of the lower case text (0x12F4730); a record of
12 bytes: +0 unlock (u 1, l 0), +1 modifier, +4 the objective hash, +8 the task hash or 0 (0x43BDF75, 0x43BE5FC); the objective modifiers
a 0, c 1, r 2 (0x43BD9DD), the task modifiers a 0, c 1 (0x43BDB6C); the evaluator 0x318A4F0 with the clause matcher 0x3189DA0 through
ObjectivesComponentRequestBus (table 0x8256158): each clause first calls IsObjectiveComplete (slot +0x20, 0x3A48A30: the completion
achievement of the row +0x28); else OBJECTIVE and OBJECTIVE:a HasObjective (0x3A46DB0), OBJECTIVE:c false, OBJECTIVE:r
IsObjectiveReadyForCompletion (0x3A48D50), OBJECTIVE/TASK and :a the task complete (+0x370, 0x3A49640: states 2, 3) or active (+0x368,
0x3A49520: states 1, 3), OBJECTIVE/TASK:c the task complete; the clauses from the last to the first, the first match decides (0x318A5B1 to
0x318A618): a `u` passes, an `l` fails (0x318A6A0); no match fails; no clause passes; the three gates of an NPA with AND: the conditional
(0x318A533 to 0x318A582), the flow, the status (0x318A6EE to 0x318A734); a starred NPA id in any achievement conditional (the token scanner
0x3A8AEB0, the hash 0x3A8AF47, the NPA path 0x318D5E0 by the tag 0x5E1621EB); the presence of a person child: the swap controller callback
0x33737C0 calls 0x3369430, and the refresh-all path 0x316EBE0 to 0x31882D0 broadcasts a changed NPA; the event that starts it at a quest
change is not found]. Live I1: at the cage of `NPC_18007` (Nighthaven) the cage is empty with no Quest_18018 (`shots/I1-01-before-cage.png`)
and the caged Cecilia shows after the grant of Quest_18018 (`shots/I1-04-after-grant-side.png`): the client applies the clause
`u:Quest_18018` of `*SwapActive_18007_Cecilia` from its own Objectives state; the server rule gives the same presence ("not there", then
"there").

**ItemRequired** [static: the gatherable loader 0x440D2A0 reads ItemRestrictionQuantity at 0x440D9D2 (the integer converter 0x5BF6D50; a
blank cell is 0) and ItemRestrictionId at 0x440D9F4 (CRC 0x12F4730; the descriptor +0x10 item, +0x14 quantity: 0x440DA25 to 0x440DA34);
the restriction enum NoToolNecessary 1, ToolRequired 2, ItemRequired 4 (0x337A333, 0x337A348, 0x337A372); the gather validation 0x35C26E0:
the early branch for 1 and 3 (0x35C2A18), the tool branch for 2 (0x35C2BDD), the item branch for 4 (0x35C2C8E): the ContainerComponent
(UUID {91B8850B-3BDF-1D1F-16BB-4B7CB8DDC4E1}) method +0x68 (0x48D63E0) with the quantity, then 0x48D6200: the sum of the stacks of the
slots of the container at least the quantity (no paperdoll, no storage); 0 accepts (0x48D626D); the item and the count of the prompt:
`gatheringtooladditionalinfo.luac` (RefreshHasRequiredItem, IsRestrictionMet); a removal path of the item: not found]. Live I2: the client
sends no gather with no Blossoming Flower in the bag (the prompt "1/1 Blossoming Flower" stays); after the flower comes back and a new
focus of the object (a walk out and back), the gather of the Altar of the Departed runs 3 s and completes `08a04_08a04_d_03_TaskGameEvent_shrine`
(`shots/I2-01-altar-no-flower.png`, `shots/I2-05d-altar-again.png`, `shots/I2-06-altar-after.png`); the flower stays in the bag.

**The categorical progression reward of a success event** [static: the event loader 0x31E70B0 reads CategoricalProgressionId at
0x31E8126 (its hash at 0x31E8154, row +0x40) and CategoricalProgressionReward at 0x31E8182 (the integer converter 0x5BF6D50, row +0x3C);
the receiver of the state 911 0x3CB7170 (ids u32, ranks u16, points u64), the progression map 0x3CC0D50, GetRank 0x3CB6330; the id of
AzothStaff is the CRC-32 of the lower case text 0xC92581E4; a rank change dispatches the notification 0x3CC12E2 (OnCategoricalProgression
RankChanged 0x53C5E27); AzothStaff has ShowAsObjectiveReward false (+0xAA, 0x3CA5DB9); the grant path of the original server and the value
of the global progression modifier (0x3CB6BC0) are not found]. The backend adds the reward as experience of the trade skill (the rank table
AzothStaff: 1 for each rank) in the plan of the hand-in, and the state 911 of the character goes to its owner. Live I3: the hand-in of
`99G_MSQ_SM_PrepareWarfare` at Father Russo: the reward notice (+12,000 XP, coin, +100 Azoth, the Azoth Staff, the legwear; no rank line,
`shots/I3-08-russo-sheet.png`), the log "1 AzothStaff experience (rank 5)"; before it the glyph of rank 5 shows "E UNSEAL ANCIENT GLYPH" and
the server refuses the gather (rank 4 of 5, `shots/I3-05-glyph-rank4.png`); after it the gather runs (`shots/I3-10-glyph-unseal.png`,
`shots/I3-11-glyph-after.png`).

**A quest gimmick** [static: the cannon slice has its GatherOption (action 14) on its Healthy state and a server timeline of the shot
(`timelines/gimmicks/cannon_destruction/gimmick_breakingwallscannon_01.timeline`: the fuse, the muzzle flash, the projectile, the debris);
static read B5]: the gather of Gimmick_BreakingWallsCannon_Vignette (2 s) completes `12A_MSQ_08_DestroyRune_UseCannon` (live I4,
`videos/I4-cannon-fire.mp4`, `videos/I4-cannon-fire-sheet.png`: the ring of the work, then the task; the shot does not play: the backend runs
no timeline).

**Action 82** (InteractTeleporter, InteractWereravenLift; not done) [static: the client handler 0x4062020 of the
InteractTeleportComponentClientFacet (vtable 0x82FBDA0) compares the action 0x52 at 0x4062046, writes the loading screen category
(0x40620F9 to 0x4062175), and calls a bus method (0x40621C8 to 0x40621F7); the option rows give action 82, not committed; the input block
is the ordinary interaction block (writer 0x35E67E0); the teleport answer of the server and a gather of the row at action 82: not found].
The shrine of `99D_MSQ_07_PortalFight` and the lift of `Quest_16008` stay out (a teleport handler of a later stage).

### 4.62 The encounter states: the root record, the manager state, the stage state and 1278, the link of a nearby event, the participant message, the kill counter, the task tree and the timer of a row, the VariationData state of a start object (live proof of stages QG5c1 to QG5c5; static proof for the client code; the backend runner of stages QG5c2 to QG5c5: docs/systems/encounters.md)

Research: `~/Games/soulwarden/work/research/quest-encounters.md` section 2; the static read A of stage QG5c1
(`~/Games/soulwarden/work/evidence/quests/qg5c1/implement/static/A/ANSWERS.md`); the live probes of the same folder (`shots/steps/`,
`shots/I1-*`, `shots/I2-*`; the bytes: `scripts/probe_hex.py`, `scripts/root_objectives.py`; `tools/world probe`, `tools/world fragment`,
`tools/world message`). Client build 1.400.6031.6004151; all addresses are image-relative.

**The case** [data]: `slices/worldevents/msq2_everfall/enc_quest_04a_side02_obeliskdefenders_01.dynamicslice` (catalog GUID
`7B52B1B2-898D-5D26-84BD-2DCED94F3321`, sub 2; root entity 137792453816943; the placement of the area spawner at 8732.26 4251.94 181.18
in the Perseus ruin). Slots: 1 Spawner (4005), 2 EncounterManager (6786), 3 PositionInTheWorld (13), 4 Objectives (3857), 5 `Wave_03 -
Cleanup`, 6 `Wave_00 - Setup`, 7 `Wave_02 - Objective: Kill AI`, 8 `Wave_01: Interact with IO` (each 2133).

**The root record** [live]: a record with the metadata state (the asset, sub 2, an actor of the allocator), the PositionInTheWorld state at
slot 3, and the manager state at slot 2 makes the entity group of the root in the client: its ObjectivesComponent (slot 4) takes a state,
and the link below shows its objective; the stop of the entity takes the objective off the HUD. The Spawner state (4005) is not needed for
these states [live: not sent; static: the status handlers do not read it]. The root shows no body of its own.

**The manager state 6786** [static: constructor 0x26E6700; one group, field 0 `status:u32`; the apply (vtable 0x80B1730 +0xB0, 0x26F0180,
0x2A9970) only stores the value at facet +0x28]:

```text
6786 := 01 01 status:u32
```

The labels [static: Lua globals of the client, 0x54DF7B4 to 0x54DF8C5; the link to this field is an inference]: 0
`eEncounterStatus_Uninitialized`, 1 `NotStarted`, 2 `InProgress`, 3 `Succeeded`, 4 `Failed`. [live: the statuses 1, 2, and 3 change
nothing that the client shows.]

**The stage state 2133 and the message 1278** [static: constructor 0x26E58D0, reader 0x26F7550; the apply 0x26F00C0 takes an entry only
when signed(sequence) is above the last sequence of the facet (+0x2C), then calls the handler 0x26F64A0 and stores the sequence; 1278
(`EncounterComponentClientFacet_SyncStateChange`, reader 0x26D52A0) calls the same handler]:

```text
2133 := 01 01 count:size (sequence:u32 status:u32)*count        count <= 10; the wrap of a sequence: not found
1278 := target:u64 G:u64 status:u32                              target: the EncounterComponent of the stage (4.2: the stage entity id
                                                                 XOR K, plus its component id; Kill: 137998612247151, 5006536641863069946)
```

A new status that is equal to the status of the facet returns before each action (0x26F64AC to 0x26F64AE) [static]. The handler compares
the statuses as signed 32-bit values (0x26F64B7 to 0x26F64CD) [static; code review of stage QG5c1, finding QG5C1R-R5]. For a different
status:

| New status | Status before was 0 | Status before was not 0 |
| --- | --- | --- |
| 0 | no action | store only |
| 1 | store only | the reset (0x26F56E0: the end actions, then the start actions, backwards) |
| 2 | the start actions (0x26F72A0) | the start actions |
| 3, 4 | the start actions, then the end actions (0x26F7170) | the end actions |
| 5 to 0x7FFFFFFF | the start actions, then the end actions | store only |
| 0x80000000 to 0xFFFFFFFF (negative) | store only | store only |

The handler does not read `m_objectives`, `m_timeLimit`, or `m_userWave`, and it does not change the root Objectives or the manager
[static]. [live: the statuses 2 and 3 of the interaction stage (2133, sequences 1 and 2), the status 3 of the cleanup stage, and 1278 with
the status 3 for the kill stage: the session stays, the HUD line does not change, and no change shows at the root (the end actions activate
and deactivate the child group 137818223620719 with `Object_04A_DevicePerseus_Visual_Healthy`, the glow, the glyphs, and the lights; no
change of it was seen from two places at the root).]

**The link of a nearby event** [live: proved; static: the apply 0x3A44520 to 0x3A4487D compares the ids with its set, the request +0x210
through 0x39FE8A0 addresses the Objectives bus by the u64, the add path 0x3A44990 to 0x3A44A76 and 0x3A68FA0]: the player Objectives state
(slot 22, type 3857) group 1 field 5 is a full container of u64 entity ids (G of the root):

```text
02 20 00 01 sequence:size64 count:size (G:u64)*count     field 5 only; field 6: 02 40 00 01 sequence count (index:u16)*count
```

With G of the root the client shows the banner "Objective Started" with the title and the level of the objective of the root, and the
objective at the top of the tracked list of the HUD with a teal diamond icon (`shots/I2-2-link.png`); an empty list takes it off, and a new
list brings it back (`shots/steps/g123.png`).

**The objective of the root** [live]: the root Objectives state at slot 4 (type 3857, group 1 fields 3 and 4 with the element forms of
4.53) with one instance gives the HUD lines of the encounter: the title "Perseus Stellar Research" (the key
`@04A_Side02_02_ObeliskDefenders_Title` of the stages), the line "Stellar Research", and the task line of the stage of the active task index:
index 0 the interaction stage ("Examine the Ancient Device", `@Enc_04A_Side02_02_Interact`), index 1 the kill stage with a counter ("0 / 2 -
Defeat the enforcers", `@Enc_04A_Side02_02_KillAI`; 2 is the m_requiredCount of the stage) (`shots/steps/b12.png`, `d123.png`). The
indices are the stages with m_userWave in the stage order [static: 0x26F1FD0]. The objectiveId of the element does not select the text (the
quest id and an unrelated id gave the same title); a new instance id gives a new banner.

**The partial progress** (the N of "N / 2") [static read B of stage QG5c2, `~/Games/soulwarden/work/evidence/quests/qg5c2/implement/static/B/ANSWERS.md`;
live: check L2 of stage QG5c2, "1 / 2 - Defeat the enforcers", `shots/I5-*` of that folder]: N is the progress of a generated
`ObjectiveTask` (+0x36C, the getter of the vtable 0x8265710). The client makes a task tree for the encounter objective (0x44650F0, from the
POI branch of the apply 0x3A43B9A when the element has `isPoiObjective` 1): its counter starts at 1 (0x4465159), each leaf takes one index
(0x272E52C), and each stage wrapper one more. For n stages with `m_userWave` in the stage order (k from 0): the leaf of stage k has the index
2k+1, its wrapper 2k+2, the outer container 2n+1. This is a different index space than the user-wave indices (0x26F1FD0: 0, 1), and the
earlier probes of QG5c1 set the interaction leaf (index 1), not the kill leaf (index 3). The task element of field 4 of the root
Objectives (reader 0x3ADDB50, 18 bytes, big-endian):

```text
task := instance:u64 index:u8 taskCRC:u32 progress:u32 state:u8
```

The apply 0x3A43EF8 to 0x3A43FC6 finds the task by the instance and the index and sets the progress and the state; taskCRC is used only
after that for a task-data lookup (0x3A43FD7), and its generated value is not found: the backend sends 0 [static; live: the counter
shows]. A separate destroyed-count of the SliceDestroyedObjective (+0x1D4, 0x272FCE0) is not the HUD number [static]. The message 5407
(`ObjectivesComponentClientFacet_OnMilestoneCorrection`) is not needed [static; not tested].

**The backend contract** (stage QG5c2, `server/rep/states/encounter_states.cpp`, `objectives_state.cpp`; live checks L2 and L4 of stage
QG5c2): for each instance the backend sends one root record to each session near it: the metadata (the asset and the sub id of the recipe
slice, an actor of the allocator), PositionInTheWorld at the place slot, 6786 with the manager status (1 at the make, 2 at the start of the
first stage, 3 at the success, 4 at a failure), the root Objectives at the objectives slot, and one 2133 for each stage with a status (the
history of the stage, sequences 1, 2, ... ; 10 entries at most). The root Objectives:

```text
field 3 := 00 01 sequence:size64 01 objectiveCRC:u32 instance:u64 0:u32 0*16 0:u64 0:u16 01 00 01 00 0:u64 00 00
           (one active element: the instance id is G of the root, available 1, isPoiObjective 1)
field 4 := 00 01 sequence:size64 count:size task*count   (the generated indices, a child before its container; the progress of a kill
           leaf: the destroyed members of its stage; the state 1 Active, 2 Complete)
```

The backend does not send 1278 (2133 carries the same status) or 2363 (its meaning is not found). The stop of the root (at the end of the
instance) takes the objective off the HUD [live: L4 of stage QG5c2]. The player Objectives field 5 of each participant has G of each root
that links it; the end of the instance or the end of the participation sends the list without it.

**The VariationData state of a start object** (stage QG5c2) [static read C of stage QG5c2, `.../qg5c2/implement/static/C/ANSWERS.md`; live:
checks L3 and L4 of stage QG5c2]: MB::VariationComponentReplicatedState is type **14** (constructor 0x42C5E80, readers 0x279E210 and
0x2A42E10), at the slot of the VariationData component of the slice (1 for the start objects of the Obelisk and of the Blighted Ranger).
One group: field 0 `index:u8`, field 1 `index:u16`. The apply 0x42CF180 uses field 0 when its version is not -1, else field 1; 0x42D0720
maps the index to the row name of the variation table of the slice (0x42CEEB0; the index is the zero-based file row order of the table:
0x42DCCD0, 0x42C6650), refuses an index out of range or an empty name, skips an unchanged row, stores it, and applies its actions. The
activation 0x42D0300 clears `m_selectedVariant` and applies only an entity-specific entry: a server child gets its variant only from this
state (or from message 3631 with the same u16).

```text
14 := 01 02 index:u16          (field 1 only; the Obelisk device 04A_Side02_02_EncounterStart_01 of Gatherable_Quest: 02 dd, 733;
                                the blight offering 1434_0104_EncStart_BlightRanger: 00 cd, 205)
```

[live: without it the client shows the mesh of the start object and no prompt (L1 and L2 of stage QG5c2); with it the prompt shows ("E OFFER
BLIGHT", "E ANCIENT DEVICE"), E sends the interaction request of GatherOption (action 14), and after the gather the device shows a red
rune (L3 with a probe fragment, L4 with the state of the backend). The source of the rune is not proved: the variant actions or the end
actions of the interaction stage.] The client sometimes ends that gather at once (2473 about 110
ms after the accepted request, then a second request that it ends too), and a later press completes it [live: L4 and L5 of stage QG5c2,
with the quest task complete and with it active; the cause is not known, the tag query near 0x35D77C0 is not read].

**The participant message 2363** (`ParticipantTrackerComponentClientFacet_SyncParticipantData`) [static: reader 0x27245B0, collection
reader 0x27265C0; the invoke 0x2724420 copies the first list to facet +0x28 and the second to +0xB8, with no test of the local player and no
notification]:

```text
2363 := target:u64 G:u64 first:size (actor:16 G:u64)*first second:size (actor:16 G:u64)*second
        target: the ParticipantTracker component of the root (component id 11980792288100801750)
```

[live: both lists with the actor of the character: the session stays, and no change shows.] The meaning of the two lists: not found.
Message 915 (`EncounterRewardsComponentClientFacet_OnSyncEncounterRewards`, `target:u64 G:u64 rewards:u32`, handler 0x26FEB70: it stores the
value and notifies) [static only]: the meaning of the value is not found.

**The encounters of stage QG5c3** (the summoned opponents, the ambushes, the kill waves with child stages;
`docs/systems/encounters.md`) [live: checks L1 to L3 of stage QG5c3, `~/Games/soulwarden/work/evidence/quests/qg5c3/implement/`]: no
new message and no new state. The backend sends the records and the states of this section.

- A proximity start object (an ambush, for example `io_99b_msq_05_yonasretreatprox.dynamicslice`) is a server child with the same
  record as a gather start object. The backend finds the character in its sphere at its scan (each 500 ms). It does not wait for a
  client message [agent; live: L2, the walk into the 30 m sphere found the object, and the stage ended in the same scan].
- An area trigger member of a stage (a member with no slice body) and a timed member have no record [agent].
- The HUD counter of a parent stage with child stages shows the destroyed spawns of its children: "0 / 2" then "1 / 2 - Defeat
  Helvan's Lackeys" [live: L3]. The backend counts each spawn of a child for its parent too.
- The HUD shows a red timer "00:00:00" under the counter of a stage [live: L3], and the HUD row of an encounter with timed user-wave stages
  (the intermissions of Thorpe) stays at the first intermission through the later stages [live: L4]. Both are corrected in stage QG5c4
  (the task tree and the timer of a row, below).
- The client runs the actions of a stage from the stage status of 2133. For example, the sky of the siphon encounter of
  09A_S18 goes dark [live: L1; the backend sends no weather and no world time change].

**The task tree and the timer of a row (stage QG5c4)** [static read D of stage QG5c4,
`~/Games/soulwarden/work/evidence/quests/qg5c4/implement/static/D/ANSWERS.md`; live: checks L1 to L3 of stage QG5c4,
`~/Games/soulwarden/work/evidence/quests/qg5c4/implement/`]. This corrects the stage rule 2k+1 above (static read B), which holds
only when each user-wave stage has one objective that makes a task.

- The tree [static]: the collector 0x26F1FD0 walks the stages of the manager and their child stages in their order and takes each
  objective of a user-wave stage (`m_userWave` 1) in the order of `m_objectives`. 0x44650F0 calls the factory of each objective
  (virtual +0x80): InvasionWaveObjective 0x272D660 (a leaf with the target 1), SliceDestroyedObjective and SliceSpawnedObjective 0x272E0E0
  (the target `m_requiredCount`), EntityActiveStateChangedObjective 0x2714D40 (a leaf with the target 1 only when its description is not
  empty; else no task), WaitForParentObjective 0x7C7DF0 (no task). Each task gets a leaf and its own consecutive wrapper of one child; the
  counter starts at 1 (0x4465159): for the task m (from 0) the leaf is 2m+1 and the wrapper 2m+2, and the outer consecutive container is
  2T+1 for T tasks (target T: 0x3AD0C95). A stage with N objectives that make tasks takes 2N indices (the kill stage of the Yonas
  retreat: the leaves 3, 5, 7, 9; the outer container 11).
- The satisfaction [static]: a task is satisfied at its target (progress = target; update 0x3ADDEC0); state 2 with progress 0 does not
  satisfy it. A consecutive container moves to its next child only when the current one is satisfied (0x3AC0A22, 0x3AC2C70), and its
  count is the satisfied children (0x3AD6C60). So a finished leaf is `target / 2`, its wrapper `1 / 2`, the outer container
  `satisfied / 1` (state 2 at T). A timed leaf has no TaskTimer and no automatic completion: the server sends `1 / 2` at the end of the
  time [static: 0x272D660; the gate +0x4F4 of 0x3AD76C0 is off].
- The timer [static: `pinnedobjectivetask.luac` lines 298 to 306 and 499 to 510; the handler 0x3A41440 of `GetTaskStartTime`]: the row
  of an active leaf with `GetUIData("TimeLimit")` above 0 (task +0x1F4: `m_timeLimit` of the objective, 0x2F2D40) shows `start +
  TimeLimit - ServerNow`. The start is field 0 (`taskStartTimes`, reader 0x3A68CB0) of an Objectives state: the request of the player
  looks at its own map, then at the Objectives of each root of its field 5 (0x3A4163B to 0x3A416C5), so the root slot is enough. With no
  entry the start is 0, and the row shows a red 00:00:00. The Lua caches the deadline at the activation of the leaf: the entry must come
  first (or in the same state).

```text
field 0 of the root Objectives := 02 01 00 01 sequence:size64 count:size (G:u64 leaf:u8 start:u64)*count
                                  start: the synced wall clock in Unix nanoseconds (the clock of 4.24), the start of the stage
```

- `m_countDownProgress` (task +0x2FA) hides the "N / M" of the row (`GetHideProgress` 0x3AD3FB0) [static].
- The backend [live: L1 to L3 of stage QG5c4]: field 4 has the leaf and the wrapper of each objective of a stage that started (a stage
  that ended, and an objective that met its count: satisfied), and the outer container with the count of the satisfied wrappers; field 0
  has an entry for each leaf with a time limit, added at the start of its stage in the same state as the completion of the leaf before
  it. Thorpe: after the first wave the row "Intermission 00:00:07" counts down, and after 10 s "Stage 2/5" and "0 / 5 - Defeat Thorpe's
  Loyalists" follow (in stage QG5c3 it stayed at the intermission); the shipwreck of C10A_Q4: "Curse is lifting 00:00:06"; the hacker
  defense of 09A_S9: "Defend Kievan Sidorov from the Anubian Guardians 00:00:57" (the countdown flag: no "N / M").
- The entity-state stage of a tarot tower needs no message: the runner finds the character in the trigger of the data at its scan
  [agent; live: L3]. The cleanup status of the tower makes the client remove the corruption of the room (its stage actions) [live: L3].
- Stage QG5c5 (the Darkness events, the boat reinforcements; no new message and no new state type) [live: L1 to L3 of stage QG5c5,
  `~/Games/soulwarden/work/evidence/quests/qg5c5/implement/`]: the same root record, stage states, and root Objectives show the rows of a
  monolith ("Corrupted Monolith": "Destroy the Pylons to break through the barrier. 0 / 3 - Corruption Pylons Destroyed", "Expose the
  Corrupted Monolith's Core. Carapace Destroyed", "Destroy the Corrupted Monolith"), of a portal ("Corrupted Portal": "Disrupt the ritual.
  0 / 2 - Acolytes Defeated", "Fight back the enemy invasion! Defeat all Enemy Waves", "Destroy the Corrupted Portal"), and of a boat ("The
  Captain's Arrival": "Scour the Seas", "Boat Arrival 00:00:30" with the timer of field 0, "0 / 2 - Defeat Esteban's Crew", "Defeat Captain
  Esteban", "Fire the nearby cannon to sink Captain Esteban's Ship"). The child stages of a portal and of a monolith have no user wave:
  no row. The client plays the stage actions of a status itself: the flashes of the portal at the start of each sub-wave, the boat of the
  timeline of "Spawn Boat, Start Timeline" (the ship comes from the horizon in 61 s and docks at the pier: no record of the backend for
  the ship), and the end of the corruption and of the red sky at the cleanup. A part of a group, a pylon or a core (a start object of an
  ordinary row with its VariationData state), and a core shell (the record of a stationary target, 4.14) need no new state.

### 4.63 The Faction state, the join and the change 3161, the empty Social state (live proof of stage QG7; static proof for the client code and the game data; the join with the time 0, the same-faction refusal, and the completion of TaskChooseFaction are project rules)

Research: the static read of stage QG7 (`~/Games/soulwarden/work/evidence/quests/qg7/implement/static/FACTION-STATIC.md`), the live runs
of stage QG7 (`~/Games/soulwarden/work/evidence/quests/qg7/implement/RESULT.md`). The addresses are image-relative (client build
1.400.6031.6004151). The rules of the backend: [Factions](systems/factions.md).

**The Faction state** (slot 49 of the player slice and of the remote player slice, type 3152,
`MB::FactionComponentReplicatedState`) [static: the constructor 0x30A7910, the initializer 0x30B6BA0, the apply 0x30B6210, the setter
0x30C0A40; live: the join of stage QG7]. Four groups; group 0 has no field.

| Group | Field | Name | Value | Reader |
| ---: | ---: | --- | --- | --- |
| 1 | 3 | lastFactionChangeTimepoint | u64 big-endian, the wall clock in Unix nanoseconds | 0x279E480 |
| 1 | 4 | factionChangeCount | u16 big-endian | 0x2A42E10 |
| 2 | 0 | faction | u8: 0 None, 1 Syndicate, 2 Marauder, 3 Covenant | 0x17B3C60 |

- Group 1 has the owner as its only target; group 2 has no target list (each observer gets it); group 3 has the null target
  [static 0x30B6BA0]. The backend sends no PvP field.
- The bytes of the owner (time 0, count 0, Syndicate): `06 18 0000000000000000 0000 01 01`. The bytes of an observer: `04 01 01`
  [test case QG7A].
- The apply republishes the faction on the HUD at each received faction field, also with the same value [static 0x30B6210 to
  0x30C2E50]. The client publishes the faction of its own request before the answer (0x30C00B0). So the answer to a refused request is
  the Faction state again [static; test case QG7B; no live refusal of 3161: in the wait the client refuses each change itself].
- The wait of a change: `lastFactionChangeTimepoint + Faction Change Cooldown Seconds × 10^9` against the CRT wall clock of the client
  (`_Xtime_get_ticks` × 100, 0x172DCF0) [static 0x30B1DE0, 0x30AD478]. The value is Unix nanoseconds [live: with the time of the
  server now, the button "Change Faction" of the Bio tab says "Cannot change factions while on cooldown. 30d", also after a restart;
  the static read did not prove the epoch]. With the time 0 the button is active. Live too: the recruiter Astrid selects the state
  `9826_RepAstrid_CooldownMultiple_b` ("We cannot recruit you at this time.").
- The name plate of an observer reads the faction through `marker.luac` (callback 0.6.18, `FactionType`) [static; the live check with a
  second client is not done: the bytes are in test case QG7G].

**The request** [static: the sender 0x30C00B0, the enum validator 0x2E6A8A0; live: the join of stage QG7]:

| Type | Name | Body | Answer |
| ---: | --- | --- | --- |
| 3161 | `FactionComponentServerFacet_RequestSetFaction` | `target:u64 entity:u64 faction:u8` (17 bytes) | the Faction state (also after a refusal) |

- The target is the component `Social/2bbb6c0d-28a5-4c6c-9e02-1b28ebd8170c` (the component Faction of the entity Social of the
  character). The client validator accepts the faction 0 to 4; the backend refuses 0, 4, and a larger value.
- The client sends it from the faction poster of a recruiter ("Join this faction?" of `changefaction/changefactionposter.luac`) with the
  achievement check off, and from the change button of the Bio tab with the check on [static: CanSetFactionWithResults 0x30AD140].
- Live: a join from the poster of Astrid (`shots/15`, `shots/16`): the backend log has the request 3161 and the Syndicate (time 0,
  count 0); the HUD shows the crest of the Syndicate, Astrid shows the marker of the Syndicate (`shots/22`). A change from the Bio
  panel (`shots/52` to `shots/54`): the panel greys out a faction with no recruitment achievement; after "Yes" the client sends 3161
  (faction 2), and the Bio shows the Marauders with their own rank and tokens. In the wait the panel greys out each other faction and
  the client sends nothing (`shots/56`).

**The empty Social state** (slot 55 of the player slice, type 4176, `MB::SocialReplicatedState`) [static: the constructor 0x3ED7110,
the apply 0x3EF4450 then 0x3EF3E40; live: stage QG7]:

```text
group 0 field 0  the title          u32 (0: no title)
group 0 field 1  the pronoun        u8
group 0 field 2  the chatting state u32
group 1 fields 0, 4, 5, 6  the war, friend, invite, and block maps: each an empty full snapshot 00 01 01 00
bytes with the slot and the type (31): 37 90 41 03 07 00000000 00 00000000 71 00010100 00010100 00010100 00010100
```

- Without this state the client crashes when it opens the faction poster or the options of a conversation: an access at 0xD08 in the
  vector size getter 0x2F2D50, from `JavSocialComponentBus.GetFriends` of the poster (`Show`, `RefreshStats`:
  `GetFriendCountInFaction`) [live: two crashes of stage QG7 with the faction state alone]. The backend sends it to the owner one time,
  after the first 2709 of the session (the rule of the late owner states). An observer gets no fragment.

**What is not proved**: the message 467 (the client validation after a faction change) has no send site [static]; the PvP fields of
the Faction state (group 1 other fields, group 3); the request 3817 (`RequestPurchaseRankIncrease`, the faction shops).

### 4.64 The town mission board: the TerritoryInterface state, the open, the provider state, the accept 3819, the mission fields of the Objectives state, the hand-in 2461 (live proof of stage QG6a; static proof for the client code and the game data; the expiration, the projects of a town, the destination argument, and the projects of a seed are project rules)

Research: the research of the boards (`~/Games/soulwarden/work/research/mission-boards.md`), the static read of the offers of stage QG6a
(`~/Games/soulwarden/work/evidence/quests/qg6a/implement/static/offers/OFFERS.md`), the live runs of stage QG6a
(`~/Games/soulwarden/work/evidence/quests/qg6a/implement/RESULT.md`). The addresses are image-relative (client build
1.400.6031.6004151). The rules of the backend: [Mission boards](systems/mission-boards.md).

**The board** is a child that the client makes itself (the spawner `Town_Project_Board` of the settlement, m_isClientAuthoratative
false, m_spawnOnInitialize true): the client id of its place (client 0x6160B60, the rule of a person child) and the actor G and 8 zero
bytes [data; live: the open 1589 of the board of Windsward names the object 95f0af0ac1092587 and the target 8750122fcacd363c, the values
of this rule; a second board of the backend with an id of the allocator was not the board that the client opened]. The backend sends the
board a record with that actor (the metadata state, the Interact state, the TerritoryInterface state). Its option `OpenMissionBoardOption`
is the interaction action 51 (committed); the answer of the open is 3743 with result 1 [live].

**The TerritoryInterface state** (slot 3 of `town_project_board`, type 1746, `MB::TerritoryInterfaceReplicatedState`) [static: the
constructor 0x29E3F00, the apply 0x29FBEC0, the reader 0x2A0BB00 -> 0x29D5CC0; OFFERS.md section 5]. Three groups: group 0 the bools
territoryFunctionalityDown and territoryRefsSet, group 1 field 0 territoryProgressionData,
group 2 field 0 territoryGovernanceData. The backend writes group 1 field 0 only:

```text
activeCount:size { key:u8 projectId:u32 timeProjectEnds:u64 currentProgress:u32 }*
completedCount:size  defaultProject: projectId:u32 end:u64 progress:u32  statusCount:size
one always available project (key 6, CommunityProjectId, CRC-32 of the lower-case id):
01 06 PPPPPPPP 0000000000000000 00000000 00 00000000 0000000000000000 00000000 00   (38 bytes)
```

- The town getter GetCurrentCommunityGoals 0x4955A90 asks the TerritoryInterface of the provider for this data (the ready predicate
  +0x2AA, set by the apply when group 1 field 0 arrives) and runs the generator for each nonzero project of the active map. With no
  state the board has no offer [static].
- The town screen (`townprojects.luac`) lists a project only when `LandClaimRequestBus.GetTerritoryProgressionData(ClaimKey)` has it
  in `activeProjects` (not cancelled), and it reads the details from the TerritoryInterface of the entity of the data node
  `Hud.TerritoryGovernance.EntityId`: the board that the player opened (the writer 0x49592C0) [static: the scripts, LANDCLAIM.md]. With
  the board states alone the screen opens and lists nothing [live].

**The LandClaimManager state** (slot 2 of the world entity `slices/globalgde.dynamicslice`, asset F1855F94-8894-55D7-A6B0-D003EC278126
sub id 2, type 3086, `MB::LandClaimManagerComponentReplicatedState`) [static: the constructor 0x2F75790, the readers of
`static/landclaim/LANDCLAIM.md` and `static/landclaim-owner/OWNER.md` of the evidence of stage QG6a; live: the town screen lists the
offers, and a travel into a territory with a claim has no fault]. One group of 18 fields. The fields of the claims are parallel full
vectors (`00 hasVersion [version:size64] count:size` and one element for each claim). The backend writes these fields:

| Field | Name | Element |
| ---: | --- | --- |
| 2 | replicatedClaimKeys | u16, the territory id |
| 3 | replicatedConflictFaction | u8 (0) |
| 4, 5, 7, 13 | the conflict lottery end, the conflict start, the darkness cycle end, the influence race start | u64 time point (0) |
| 6 | replicatedDarknessThreshold | u32 (0) |
| 9, 10, 11 | the influence of the three factions | u8 (0) |
| 12 | replicatedGovernance | four f32 fees, isValid u8, failedToPayUpkeep u8 (18 zero bytes) |
| 15 | replicatedProgression | `upgradeCount:size {category:u32 level:u8}* projectCount:size {projectId:u32 progressionType:u8 activeState:u32}* territoryLevel:u8` |
| 16 | replicatedOwnerData | company id (16 raw bytes; 0: no company), company name (text), crest background u16 and color (4 f32), foreground u16 and color (4 f32), player name (text), faction u8, last claimed time u64 (63 zero bytes) |
| 17 | replicatedWarDecThresholdMetFaction | u8 (0) |

- The values of the enums: progressionType 6 AlwaysAvailable (1 Fortress, 2 Settlement, 3 Lifestyle, 4 WarPrep, 5 WeakenInvasion, 7
  Event); activeState 0 Active (-1 None, 1 Completed, 2 Expired, 3 Cancelled, 4 Blocking) [static: LANDCLAIM.md section 1].
- The getters find a claim by its key in field 2 and read the element of that index of another field with no length check (the owner
  0x2F81310, the copy 0x2831BCF). A territory entry reads the owner at once (0x2A09BA0 -> 0x29FF700) [static: OWNER.md; live: with
  fields 2 and 15 alone the client stopped at a territory entry, an access at 0x10 + index × 0xD0].
- The backend sends no field 14 (the positions): a position makes the manager "available" (`Hud.LocalPlayer.IsLandClaimManagerAvailable`)
  in the apply before the other fields are copied, and the map and HUD code then read more [static: 0x2F848A0; live: a fault].

**The provider state** (slot 10 of the player slice, type 3829, `MB::ObjectiveInteractorReplicatedState`) [static: the constructor
0x3A1B9E0]. One group, four fields:

| Field | Name | Value | Reader |
| ---: | --- | --- | --- |
| 0 | objectiveProviderId | 16 raw bytes: the provider identity of the board, the actor id of the Claim landmark of its territory (Windsward: `4b657e1a68e06c9f0000000000000000`; not the actor of the board) | 0x279E260 |
| 1 | expirationTime | u64 big-endian, the server clock in nanoseconds (the clock of 335): the start of the next seed [agent] | 0x279E480 |
| 2 | missionParams | a full vector of ObjectiveCreationParams: `00 01 sequence:size64 count:size`; a town board: count 0 | 0x3A68000 |
| 3 | m_communityGoals | `count:size`, then for each territory `territory:u16 missionSeed:u32 unavailable:size u32* previousProjects:size u32*` (no container header) | 0x3A68C60 -> 0x495DDA0 -> 0x494F320 |

- The owner gets the state after its first open of a board in a world entry. An observer gets no fragment (the remote player slice has
  no ObjectiveInteractor component) [data; test case QG6AI].
- The client marks an offer unavailable only when the stored missionSeed of the territory is the current seed and the mission is in the
  list (0x3A3684C) [static; test case QG6AF].

**The town offers** are computed by the client (GetCurrentCommunityGoals 0x4955A90 -> 0x3A34C70 -> 0x3A362A0) [static: OFFERS.md;
live: in five seeds the town screen listed the offers that the backend computed for the same character, territory, and minute]:

- seed = low 32 bits of (FNV-1a 64 of the characterId text + territory + the server clock minute rounded to the interval with the
  half-interval phase, signed i32 + the seed addition 0) [0x3A36220, 0x39D2A70, 0x3A1C52A]. The interval is the CVar default 30.0
  minutes (`javelin.territory-progression-mission-cooldown-min`, 0x61743A9). The characterId is the text of the Player state 3935
  group 2 field 0.
- For each active project: r = ((projectCrc + seed) mod 2^32) xor 0x5DEECE66D; for each bucket with candidates (Kill 4, Gather 1,
  Explore 5) one step r = (r × 0x5DEECE66D + 11) mod 2^48, the offer is candidate (r >> 16) mod count [0x3A36539, 0x3A36809 to
  0x3A3683D]. The order of the candidates is the hash order of the client (`townBuckets` of the mission data, OFFERS.md section 6).
- The countdown is GetSecondsToNextSeed 0x4957F20: 60 × (P − ((m + P/2) mod P)) − s.

**The requests** (the component ObjectiveInteractor `RootPlayer/006554ad-a98d-48e4-a5d6-f8a0ab3e7933`, slot 10) [static: the research of
the boards 2.2]:

| Type | Name | Body | Answer |
| ---: | --- | --- | --- |
| 3819 | `ObjectiveInteractorComponentServerFacet_OnCommunityGoalSelectionRequest` | `target:u64 entity:u64 missionId:u32 projectId:u32` (the name of the writer: destinationOverride) | no message: the Objectives state and the provider state |
| 2461 | `ObjectiveInteractorComponentServerFacet_OnObjectiveCompletionRequest` | `target:u64 entity:u64 instance:u64` | no message: the Objectives state, the rewards, the provider state |

- 3819: the sender 0x494E700, the writer 0x3274450 (the mission at +0x24 first), the caller 0x495C7F0. The second u32 is the project of
  the card: the card of the town screen sends the project id (`townproject_activeproject.luac` OnTaskStart), the detail popup 0
  (`townprojects.luac` OnTaskDetailCallback) [static: the scripts; live: a click of a card sends `07 04 f9 91` (Bison_10) then
  `25 91 c4 12` (CommunityProjectId)]. A refusal of the backend marks the provider state and the Objectives state.
- 2461: the sender 0x494E910, the caller 0x495CCE0, the reader 0x39E8A10. The client sends one for each ready mission at the board
  [live: a click on the card "COMPLETED"]. The client offers it only while the return task is pending and the container has its child
  count minus one (the ready check 0x3A3CC50), and while the destination of the return task is objectiveProviderId (CanCompleteMission
  0x4954180) [static: DESTINATION.md]. The backend completes the return task at the request, then hands in.

**The mission fields of the Objectives state** (slot 22, type 3857, group 1 field 3, the element reader 0x5B52F60, section 4.53): a
mission instance has missionId (the CRC-32 of the lower-case MissionID), destinationOverride (the claim of the board: the town screen
keeps a community goal only when its destination is a claim or an outpost of the map whose settlement is the territory, and
CanCompleteMission 0x4954180 compares the destination of the return task with objectiveProviderId), originTerritory (the territory of
the board), and isCommunityGoal 1 [static: `static/destination/DESTINATION.md`]. The objectiveId is the template objective of the mission
row. The client joins the mission row by missionId and builds the effective tree with its override cells (the factory 0x4461150; case
13 the kill override, case 23 the destination of TaskInteract `ReturnToGiver`) [static].

**What is not proved**: the positions of the claims (field 14) and the map data; the TerritoryInterface groups 0 and 2; the other
project types of a town. The explicit offers of field 2 are section 4.65 (stage QG6b).

### 4.65 The faction mission board: the open at a representative, the offers of field 2, the accept 3816, the hand-in 2461, the GameEvent state (live proof of stage QG6b; static proof for the client code; the selection of the offers and the window are project rules)

The static read is `~/Games/soulwarden/work/evidence/quests/qg6b/implement/static/faction-board/FACTION.md`; the live runs are in the
result of stage QG6b (`RESULT.md` of the same folder).

**The open**: a faction representative is a person with an ObjectiveProvider (m_editorProvidedMissionTypes Marauder, Syndicate, or
Covenant; m_isCommunityGoalBoard 0; m_useTerritoryId 1) and the options OpenConversation and OpenMissionBoardOption of its Interact
component [data: `coatgen/7b53567b/npc_1607_commander_*`]. The conversation offers the service Faction_Board as the option 8
("Open Faction Board", 0x37AD685) when the state passes for the player; its selection closes the conversation and sends the ordinary
interaction request 1589 with action 51 at the same person (0x376E845 -> 0x374EF70) [static; live: `option d183d760`, action 51, the
answer 3743 result 1].

**The provider state** (slot 10, type 3829, section 4.64) of a faction board:

| Field | Value |
| ---: | --- |
| 0 | the actor id of the Claim landmark of the territory of the representative (0x495941E to 0x4959435: the same rule as a town board) [static; live: Everfall `1b939fd32e1c1a3c0000000000000000`] |
| 1 | the end of the offer window (server clock nanoseconds) [agent] |
| 2 | `00 01 sequence:size64 count:size`, then the offers: ObjectiveCreationParams of 52 bytes each (`instance:u64 missionId:u32 destination:16 acceptTime:u64 originTerritory:u16 available:u8 isCommunityGoal:u8 isPoi:u8 isDynamicPoi:u8 parent:u64 isReset:u8 ingredients:size`); instance 0 before the accept and the accepted instance after it, destination the claim, available 0 after the accept (0x495A166) [static: 0x5CE92B0, 0x3A43320; live: three cards] |
| 3 | the town entries of the character (section 4.64) |

The state apply 0x3A43320 splits field 2 into the PvE list and the PvP list by the row of each missionId; the screen
(`owmissionboard.luac`) shows three PvE cards and the countdown of field 1 [static; live: "MARAUDERS MISSIONS", three cards, "Missions
refresh in 20:12"].

**The requests** (the component ObjectiveInteractor, slot 10):

| Type | Name | Body | Answer |
| ---: | --- | --- | --- |
| 3816 | `ObjectiveInteractorComponentServerFacet_OnObjectiveSelectionRequest` (UUID 40B9177C-06DE-4725-9166-236BD7D4A7E8) | `target:u64 entity:u64 index:u8` (0 to 99) | no message: the Objectives state and the provider state |
| 2461 | `OnObjectiveCompletionRequest` (section 4.64) | `target:u64 entity:u64 instance:u64` | no message |

- 3816: RequestMissionSelectionById 0x495CE70 finds the missionId in the PvE list of the provider and sends its zero-based index (the
  sender 0x494EB10, the reader 0x49493C0, the constraint 0x495D880) [static; live: a click on the second card sends the index 1].
- The accepted element of the Objectives state (section 4.53) has isCommunityGoal 0, destinationOverride the claim, and
  originTerritory the territory of the representative; the card shows "COMPLETED" when CanCompleteMission 0x4954180 passes (the same
  claim) [live].

**The GameEvent state** (slot 68 of the player slice, type 497, the constructor 0x31E5850) [static: FACTION.md section 5; live: "Daily
Bonuses Available 3/3", then "2/3" after a hand-in with the bonus]:

| Group | Field | Name | Value |
| ---: | ---: | --- | --- |
| 0 | 1 | dailyBonusesUsed | `00 01 sequence:size64 count:size`, then `{modifierCrc:u32 bonusesUsed:u8}` for each modifier (5 at most; reader 0x3205890, element reader 0x31DFDF0) |

- The backend sends only field 1 (the field mask `02`), and only for a character with a use. GetAvailableDailyBonuses 0x31F1140 is the
  maximum (the CVar default 3 of `javelin.daily-bonus-mission-count`, 0x616E608) minus the uses of the key of FactionMissionBonus
  (0x85D30F59).
- The reward preview (0x31F4630) adds the row FactionMissionBonus of RewardModifierData to the multipliers while a use is left:
  experience × 3, coin × 10, tokens × 5, reputation and standing with no daily part [static; live: the card tooltip "400 Reputation,
  2475 Tokens, 70.50 Coins, 250 Territory Standing, 2520 XP, Daily Bonus", and the backend paid these values].

**What is not proved**: the original selection of the faction offers, the refresh interval, and the duplicate rules (the backend rule
is in [Mission boards](systems/mission-boards.md)); the PvP accept 3818; the field 0 gameEvents of the GameEvent state.

### 4.66 The level change in one connection: the self identification, the level information, the spawn notice, the bundle sequence, the owner record, the acknowledgement 5181 (live proof of the probe of research DG0 and of stage DG1; static proof for the client code; the instance model, the return place, and the account key are project rules)

The live probe is in research `dungeons.md` section 2 (2026-10-22, a probe patch of `5174ff8`); the backend of stage DG1 sends the same
sequence (`RepSession::change_level`, docs/systems/instances.md). Live runs of stage DG1: four level changes of one session (the main
world to Amrine, the return, a second entry, a third entry), a second connection, and a login after a backend stop
(`~/Games/soulwarden/work/evidence/dungeons/dg1/implement/RESULT.md`).

**The sequence of the server** [live], for a client in `InGame`:

1. The self identification 1628 (section 3.5) with the new context instance in its level block (the same actors).
2. The level information 1635 (section 4.1: level text, terrain text, the four floats 0, the grid layer 1 (section 4.69), an empty list, flag 0, the instance, the
   reload flag 1, flag 0, the version) with the level of the new space, a new version, and a new instance (the account rule of section
   3.5). The level of an instance: the CoatlicueName of its map row in lower case with "/" and its last part (`nw_dungeon_windsward_00`,
   `coatlicue/nw_dungeon_windsward_00`) [data; live].
3. The spawn point notice 1617 (no fields).
4. An empty state bundle with the sequence 1 and the new instance: the client resets its expected sequence to 1 (0x6463540 [static]), and
   this bundle starts the load (0x643EEE9 [static]).
5. The next tick: the full owner record (sequence 2, interest number 1, the metadata state in slot 0 first), then the records of the
   entities of the new space with new interest numbers. The client cleaned its context (`ClientContext::Initialize` 0x171B850, cleanup
   0x16835B0 [static]): no stop is needed for the entities of the old context. The entity, its actor id, and its GDE id stay [live: research
   dungeons.md 2.3, U5].

**What the client does** [live]: game log `LevelInfoChanged`, `Reset`, `InGame` to `WaitingForPlayerSpawn`, `m_clientContextInstanceId A
changing to levelInfo.m_clientContextInstanceId B`, `Calling clientContext->Initialize`, then one line `HandleUnexpectedMessage skipping
ReceiveStateBundle for bundle message due to clientContext == nullptr` and, after the map load, `ProcessPendingReliableMsgQueue processing 1
messages from m_pendingUnexpectedMsgs`: the client keeps the bundle that starts the load and applies it after the load (the same two lines
come at the first world entry; stage DG1: six loads, six pairs, no `Failure during read`, no disconnect; the bag, the gear, the coin, the
quick slots, the attributes, the journal, and the tracker the same after each load). Then `Loading level LEVEL` and `player spawn succeeds,
in game`. The loading screen shows a tip of the level (Amrine: "Got the Munchies"). Times of stage DG1: the mine 4 s to 8 s after the level
information, the main world 11 s.

**The acknowledgement 5181** (`PlayerComponentServerFacet_OnAckLevelInfoChanged`; body target:u64 entity:u64 value:u8; reader 0x65917B0,
top-level reader 0x658A6A0 [static]): the client sends it from the old context 11 ms to 26 ms after the level information, with the new
instance as its byte [live]. The backend writes one log line and waits for nothing: the owner record is in the bundles that the client keeps
until its level is ready [live].

**The observers** [project rule, MP-2]: a session in the old space gets the stop of the interest number of the mover at its next interest
scan (at once: the move marks `last_travel`), and it keeps its level, its context instance, and its bundle sequence (case IN8). A session in
the new space gets a record of the mover with a new interest number (live: a remote character that joined the instance came into the
interest table of the client in Amrine and showed with its name tag).

**Not sent at a level change** [project rule]: 3746 and 6709 (the end of the input lock of a travel), 3951 (the loading screen of a far
travel), and 3954 (on player teleport). Not checked: the four floats and the list of 1635 (zero and empty), the U2 and U3 questions of
research expeditions.md (the self identification and the spawn notice are sent; their need is not tested).

**A level change to the same level text** [live, stage QG9a]: the entry of a story instance of `newworld_vitaeeterna` (QuestMedea) sends
the same sequence with the level text of the main world, a new version, and a new context instance. The client loads the level again (the
loading screen) and spawns in the new context; the entities of the main world are not in it. Live runs of stage QG9a: four entries (one
of them into the empty instance again) and one return by the exit portal, no fault.

### 4.67 The entrance of a game mode, the result 3094, the GameModeParticipant state, the leave 3309 and 1980, the cutscene of an encounter stage (live proof of stage QG9a; static proof for the client code; the entry with no 946, the audience of the state, the value X, and the event of Intro_Complete are project rules)

The static read is E of stage QG9a (`~/Games/soulwarden/work/evidence/quests/qg9a/implement/static/E/ANSWERS.md`). The live runs are L1 to
L5 of stage QG9a (`~/Games/soulwarden/work/evidence/quests/qg9a/implement/`: the backend logs, the pictures, and the videos). The backend
side is in docs/systems/instances.md (the story instances).

**The entrance** [static; live]: the bound of the entrance slice (`4_1_trial_entrance_questmedea`) has the options OpenDungeonMenu
(action 71, committed) and EnterQuestCaerDun (action 72, option CRC B17F828E; the text of the option is the name of an other mode, and
the mode is `DungeonEntranceComponent.m_gameMode` = QuestMedea, CRC 95B632E9). The client sends each one in its input 1589 (protocol notes
4.35): first 71 (the client opens `DungeonEnterScreen` itself, handler 0x2FF5720; the reply 3743 accepts the use), then, at the Enter
button, 72 with the base action 71 (0x49A0F5A, 0x4912410: the client searches the current options by the action 72). Live: 1589 with 71,
then 1589 with 72 base 71 about 3 s later. The required item of the Enter option (KeyDynasty) has the count 0 in the data: no item [data;
live: the backend refused the entry while it tested the item].

**3094 DungeonEntranceClientFacet_OnDungeonActivateResult** (server to client; reader 0x26D52A0, handler 0x2FFB4F0 [static]):

```text
target:u64 entity:u64 result:u32
```

- `target`: the DungeonEntrance component of the entrance (the instance id of its entity by the XOR rule plus its component id; slot 3 of
  the Medea entrance); `entity`: G of the entrance. Live: target 53b85d6577faef54, entity d95c21d3f6dcde53.
- `result`: eDungeonActivateResponse (registration 0x54D473F to 0x54D484D [static]): 0 Success, 1 NotEnoughMembers, 2
  NotEnoughMembersInProximity, 3 MissingReqItem, 4 ReqItemDisabled, 5 TooManyMembers, 6 RaidGroupDisallowed, 7 SoloGroupFailure, 8
  PendingActivation, 9 InCooldown, 10 EntranceActivationFailure, 11 GroupAlreadyInDungeon, 12 GroupTimeout, 13 MutationLevelReq, 14
  MutationDifficultyReq, 15 ItemRemovalFailure, 16 ScheduledNotAvailable, 17 EntranceBusy.
- The handler has no state change and no teleport. `DungeonEnterScreen.OnDungeonActivateResult`: 0 closes the window with no notice; a
  refusal closes the window with its notice; 17 tries again after 0.5 s, three times [static]. Live: 0, the window closed, then the level
  change of section 4.66.
- The backend sends 0 and moves the character in the next tick (the reply 3743 of the request goes first). It sends 10 for a level below
  RequiredLevel of the mode and for a mode whose content it does not run, 11 for a character in an instance [project rule].

**946 PlayerArenaComponentServerFacet_TeleportIntoDungeon** (client to server; body `target:u64 entity:u64` only; reader 0x259E1E0, send
stub 0x2760D20 [static]): the queue HUD sends it at the accept of a dungeon invite (WaitingEntry). The local entrance does not send it, and
the client needs no 946 before a level change [static; live: no 946 in the runs, and the entry worked]. The backend writes one log line.
PlayerArena (slot 4, type 2406: field 14 the state u8, 15 the deadline u64 wall ns, 16 the mode index u8) is not sent [project rule: no
reader of a solo entry needs it; live: the HUD of the mode, the compass, and the respawn worked with no PlayerArena fragment].

**The GameModeParticipant state** (slot 24 of the player slice, type 3312, constructor 0x329ACB0 [static]). Group 0 has six fields; the
backend sends two:

```text
field 0 activeGameModes   changes:size=0  stamp:u8=1 R:size64  count:size
                          count x { A:16raw  G:u64  F:u64  C:u32  M:u32  W:u64  X:u64 }
field 1 flags             six raw bytes (bitset, little bit order)
```

- `changes = 0` selects the full replacement (0x32D96E0) [static]. The stamp R is the revision of the last change of the component: a
  newer replacement has a greater stamp (0x32D785E) [static; live: the entry and the leave of six runs each applied].
- A, G, F: the server-facet reference of the game mode entity (writer 0x32BE580, reader 0x328D6D9) [static]. The backend reserves an actor
  for each instance and sends its 16 bytes, its G, and G as F [project rule; live: the dungeon HUD and the compass took it].
- C: CRC-32 of the lower-case GameModeId (IsInGameMode 0x32BA3B0 compares it; TaskGameMode 0x3AD5CC0 hashes its GameModeID) [static]. M:
  the CRC of GameModeMapId (QuestMedea: also 95B632E9) [static]. W: the entry time, Unix wall nanoseconds (0x32ABFD0 takes the greatest W)
  [static]. X: a u64 that no checked reader tests: 0 [project rule].
- flags: bit 22 PlayerCanQuit (getter 0x2F2C60 returns 0x16): `00 00 40 00 00 00`. It shows the extra mode button of the escape menu
  (escapemenu.lua lines 517 to 533) [static]. Stage QG9e: no bit (`00 00 00 00 00 00`); section 4.69.
- The owner gets the entry in the instance and the empty map with no flag after the leave (the client sends its exit notification, apply
  0x32B78C0) [static; live: the compass showed the name of the mode, then the territory of the main world again]. Nothing before the first
  entry of a world entry. An observer gets no fragment [project rule: the audience of the original server is not found; the remote player
  slice has the component].
- Live (L1 to L5): the TaskGameMode task of 06A_MSQ_21 (enter the mode) became done at the entry, from the state alone.

**3309 GameModeParticipantComponentServerFacet_RequestLeaveGameMode** (client to server; reader 0x3273BB0, caller 0x32D2A10 [static]):

```text
target:u64 entity:u64 A:16raw G:u64 F:u64          (48 bytes)
```

The key of the first entry of activeGameModes. The backend leaves the mode when the key is the key of the membership of the character;
an other key changes nothing [project rule]. No UI caller was found, and no 3309 came in the live runs.

**1980 GameModeParticipantComponentServerFacet_OnReceivedClientEvent** (client to server; reader 0x26FEC60 [static]): `target:u64 entity:u64
event:u32`. The leave confirmation of the dungeon HUD and of the escape menu send the event 2612035792 = 9BB080D0 (SendClientEvent) [static].
The backend leaves the mode for that event and writes a line for an other event [project rule]. Not checked live: the leave button of the
escape menu was empty in the runs (no text, nothing sent); the exit portal was the leave. Stage QG9e: the cause and the leave of the menu
(926) are in section 4.69.

**The exit portal** [data; live]: a server child of the mode root (`interactobject_portalstone`, spawner Portal) with the option
ExitDungeon (action 74, not committed; the client needs a hold of about 0.9 s). The backend makes it in the space of the instance at the
start of the instance [project rule: the mode script that spawns it is not in the client]. Live: 1589 with 74, the leave, the level change
to the main world, the return place.

**The cutscene of an encounter stage** [static; live]: a CutsceneFinishedObjective stage with `m_backgroundActivation` 2 starts early, and
its start action EntityEventAction activates the IGC spawner of the slice (`01_IGC_Spawner_Medea_Intro`, `m_triggerOnActivate` 1); the
client then asks for the cutscene with the exchange of section 4.20:

```text
client 6703  target:u64 entity:u64 cutscene:u32      (the PlayerCutscene facet of the player)
server 6707  target:u64 entity:u64 cutscene:u32      (the same CRC)
client 6706  target:u64 entity:u64 cutscene:u32      (the start)
client 6705  target:u64 entity:u64                   (the end, no CRC)
```

The CRCs: the intro `06A_MSQ_21_MedeaShowdownIntro_IGC` A921416F, the outro `06A_MSQ_21_MedeaShowdownOutro_IGC` FBEA1598 (CRC-32 of the
lower-case name) [static; live: both granted, both played, both ended]. The backend grants the CRC of a running cutscene stage of the
encounter of the space of the character, and 6705 ends that stage; the time limit of the stage (22 s and 60 s, succeedOnTimeout) ends it
with no end of the client [project rule]. The IGC is a slice of the client: no record of the server [static: 0x2F0CD10].

**The task tree of the Medea encounter** [static]: CutsceneFinishedObjective and EncounterEventObjective make a task only with a
description (factory 0x2714D40); WaitForParentObjective makes none (0x7C7DF0). The tree of the Medea slice has only the cleanup: leaf 1
(ObjectiveTask, target 1, time limit 1), wrapper 2, outer 3. The kill objective is on the boss stage, whose userWave is 0: no HUD task.
The event `Intro_Complete` of the child stage: no slice or table of the client sends it; the backend meets it at the start of its stage
[project rule].

**The respawn in the mode** [static; live]: the respawn screen reads the mode from GameModeParticipant (`DungeonRespawnsPermitted` 1 of
QuestMedea) and offers the dungeon entrance fallback; the client sends 1938 with `random=01` and an empty id. The backend puts the character
at the start of the map in its space [project rule; live: a death by Medea, the respawn at the start, the fight again].

**Stage QG9b: the order of 3094** [project rule; code review of stage QG9a, finding QG9AR-R6]: the result 0 goes after the commit of the
rows of the entry and before the move of the character; a store that refuses the rows sends 10 (EntranceActivationFailure). A refusal of
the checks sends its result at once. Live: QuestMedusa, QuestMammoth, QuestApophis (3094 result 0, the level change, the instance).

**Stage QG9b: BossPhaseComponent, state 403** [static: static read F of stage QG9b, F1]: constructor 0x25BE020, one group with a chained
field mask; field 0 `m_bossPhase` (4-byte integer, network byte order, reader 0x17B3620), field 1 `m_spawnedInEncounter` (one byte, 0 or
1, reader 0x17B3CB0); framing `01 03 phase:i32 spawned:u8`, field 0 alone `01 01 phase:i32`. The value is the zero-based index of the
full phase list; the client starts at -1, and its apply (0x25D68C0) runs the phase events of the entries up to that index (0x25D7290),
never back. The backend does not send this state [project rule: the backend tracks the phases on the server for BossPhaseObjective; the
phase events of the slices on the client are not checked live]. The rules of the phases (0x25D8F80 the health compare, 0x25DB940 the wave
end) are in docs/systems/encounters.md.

**Stage QG9b: BossPhaseObjective** [static: F2]: the completion 0x26CA1B0 needs the objective state 1 or 2 and an incoming phase value of
`m_trackedPhase` or more (unsigned compare); with no tracked slice and no alias it takes any phase notification of its encounter. The
factory 0x2714D40 makes a task only with a description (`%s: BossPhase %s: tracked phase: %u`, type text `EncounterObjectiveBossPhase`).
The sender of the value is not in the client; the backend meets the objective when the phase number T of the phases of the key
Boss_Phase of its boss ended [project rule].

**Stage QG9b: a stop of the client at the kill of Medusa** [live; static: static read CRASH of stage QG9b]: the client stops with an
access violation at 0x4871DB6 (`mov rax,[r14]`, r14 = 0) in 0x4871B60, a repair of the action history of an `ActionListComponentClientFacet`
(owner `ALCRemoteEntityManagerComponent`, tick 0x2A400F0 to 0x4873040). A flag at the component +0xCC8 starts the repair (the only
writer found: 0x4873284 in 0x48731C0, when the counters of the ReactionTrackingComponent of the context differ); the second history
insertion 0x4873330 returns no entry when the history is full and the time lookup 0x485A4E0 reaches its end, and the caller does not test
it. Live: each kill of Medusa by a hit of the player (four runs, with and without the minions, with and without a cast of Medusa) stops
the client 0 s to 0.4 s after the death; the kill by the development command does not; the kills of Mahantaram and Apophis by hits do
not. The server bundles of the kill by a hit and by the command are the same (385 bytes, two records). The trigger is not known (open
problem): the slice of Medusa has no Reaction, no Reaction_Additive, and no Death_FB action and no reaction tables (reactions
`[61, -1, 99, -1, -1]`), a difference from the other bosses [inference].

**Stage QG9c: the cause of the stop at the kill of Medusa, and the renewal of the body** [live: gdb reads of the client, runs A1 to A7 and
F1 to F4 of stage QG9c; static: static read REACTION of stage QG9c; the renewal is a project rule]. The evidence:
`~/Games/soulwarden/work/evidence/quests/qg9c/implement/`.

- The facet of the fault is the facet of Medusa: its entity id is the server id of Medusa, and its name is `FirstLight_MSQ_Medusa` [live].
  The kill by the development command after one hit of the player stops the client too; the kill by the command with no hit does not
  [live].
- At a hit of the local player the client puts a pending record into the ReactionTrackingComponent of the target (the counters old N and
  new N + 1, the source the attacker) [live: the counters at R+0xC0 and R+0xC4; the writer of the prediction is not proved (static read
  REACTION, Q1)]. Only the activity ClearDamaged closes the record: 0x2AAE7B0 (its name), the activity vtable 0x8108CA0, slot +0x30 =
  0x2AB4E20, then the virtual +0x108 of the component = 0x67A3BA0 (old = new) [static]. The actions of the creature data with that activity
  have the fragments Reaction, Reaction_Additive, Deflection, BrokenGuard, ReactionTalisman, or Pushable [data; inference for the list].
  The apply of the state 1927 (0x683D910) fills only the next record and does not close the current record; its selection 0x693DBD0 needs
  an exact key match [static]. The server has no field that closes the record.
- While old < new, 0x4873284 (in 0x48731C0) sets the flag at ActionListComponent+0xCC8, and the repair 0x4871B60 adds entries to the
  history of the facet (+0x3B0, 15 entries of 0x6B0 bytes, the time at +0x670) in each frame. In the Death fragment of Medusa the history
  fills, and the walk reads an entry with no data (0x4871DB6, `mov rax,[r14]`; 0x4871FDA in a second form) [live: gdb fault watch; static].
- The slice of Medusa has no action with the activity ClearDamaged. 18 slices of the creature data have the same form (96 spawn points of
  the main world, the creature members of 6 slices of the encounters) [data: `no_reaction_slices.py` of the evidence].
- The server change [project rule; live]: at the death of a creature of such a slice after a hit of a character, each session that
  observes it stops its interest and, 250 ms later, makes it again with a new interest number; the movement state of the first record has
  the death action from its start (the time of scope 0 is 0). The new component of the client has no pending record. Live: three kills of
  Medusa by hits (in an attack, in a chain of hits, idle) with no stop and no history insertion; the quest task of the kill is done.
- What did not work [live]: the renewal at once (14 ms, the same interest number): the client stopped. The death action with a start 60 s
  in the past: the client stopped.
- Limit [live]: after the renewal the client shows the despawn effect of the body, not the body on the ground.

**Stage QG9d: the form of the first record of a renewed body** [live: runs P1 of stage QG9d; project rule]: the first record after the
renewal has the death action from its start and the vitals state (type 15) with the flags byte 0 in vitalsData (no dead mark; the health
0); `kDeathMarkLead` (1 s) later the session sends the vitals state with the dead mark (bit 0) for the same interest number. The client
makes an entity that lives, plays its death fragment, and shows the death effect of its slice at the mark; the body of Medusa stays on the
ground, and the client does not stop (no pending reaction on the new entity). With the dead mark in the first record (stage QG9c) the new
entity showed no body [live; the cause is an inference].

**Stage QG9d: the offset of a detection volume** [static: static read AXES of stage QG9d]: DetectionVolumeComponent (reflection 0x17608F0:
`m_center` at +0x140, `m_shape` +0x160, `m_shapeEntityID` +0x170). The transform reader 0x1714A80 takes GetWorldTM of `m_shapeEntityID`, else
of its own entity. The pose helper 0x1710C60 removes the scale (0x1710EC7, when each axis has a length above 0) and computes the matrix
times `m_center` (0x1710F01 to 0x1710F2D): a local offset with the rotation and no scale. With a shape entity it takes the center of the
world AABB of the engine shape of that entity (0x1710D10 to 0x1710D36) and does not read `m_center`. The debug draw 0x177D7E0 adds
`m_center` again in world axes (a debug path only). The server facet is a stub in the client (vtable entries to 0x294860): the query of
the server is not proved. The backend applies the pose helper rule in the tool (docs/systems/encounters.md).

**Stage QG9d: the exit portal of a mode root that the client makes** [data; live]: the spawner `Portal` of the seraph mode root
(m_spawnOnInitialize true, not client-authoritative) gives a client child with the id of 0x6160B60 (the place 4877.52 5206.106 97.5, the
yaw bucket, the slice asset: G 0860c3145624c367, actor 67c3245614c36008); the client sends ExitDungeon (action 74, option D5D62681) to it
also in the space of an instance. The backend takes it for a member of an instance of the mode of the object (the member `mode` of
interaction-data.json) [project rule; live run P2: the leave to the return place].

**Stage QG9d: the cinematic of the Heartforge** [data; live]: the stage `Wave_04 - TriggerCinematic` of
`enc_worldevent_heartforgeguardian` starts the IGC `99A_MSQ_15_YonasTheAltarRitual_IGC` and the DelayedEvent `Delay` (22 s) that triggers
`02_Trigger_IGC_Spawner_Bink` (PlayCinematicVideoComponent, `m_cinematicEntryId` FTUE_Rit, local player only). Live (no sound): 0x2CC7E70
with the entry id `FTUE_Rit` (read of the facet +0xF8), then 0x6506CB0, no 0x2EF9A00, no 6606 and no 6542: the white screen of 4.58. No
task of the quest data waits for this cinematic.

**Stage DG3: the entry at the entrance of an expedition** [static; live; project rules]: the door of Amrine (`dungeonentrance_amrine`,
options OpenDungeonMenu and EnterDungeonAmrine, option CRC 42143B66) opens the expedition screen; its button "Enter With Group" sends 1589
with action 72, base action 71, and the u32 at the record +0x70: the difficulty (0 the normal mode; the reader of the backend took it from
the bytes that it skipped before stage DG3) [static: 0x49A0F5A, 0x4912410; live: 0]. With the remote setting
`javelin.enable-dungeon-group-restriction` false (section 2.3; registered with true at 0x617DFA9) the screen shows "Minimum Players: 3"
with a check and gives the button to one player [live]. The backend sends 3094 with 0 (the entry), 14 for a difficulty of 1 to 3 (a
mutation tier), 3 for a mode with RequiredConsumableItemClasses, 1 or 5 for a group out of the limits (one player: never), and 10 or 11 as
in stage QG9a [project rule; docs/systems/instances.md]. The screen needs no PlayerArena and no GroupData state for one player [live].

### 4.68 The PlayerHome state: the dungeon checkpoints, the respawn 1938 at a checkpoint (live proof of stage DG3; static proof of research death-travel.md 4.1 to 4.5; the activation, the server child, and the id text are project rules)

The research is death-travel.md sections 4.1 to 4.5 (`~/Games/soulwarden/work/research/death-travel.md`) and the static read E of stage
QG9a (E6). The backend side is in docs/systems/instances.md ("The expeditions (stage DG3)").

**MB::PlayerHomeComponentReplicatedState** (type 3652, constructor 0x34E2E40; slot 20 of the player slice). The backend sends group 0 field
0 homePointList in the full form (change reader 0x34FCFD0, full reader 0x34FD550; element reader 0x34FCE70, writer 0x34F13D0):

```text
changes:size = 0  version: has:u8 = 0  count:size
count x { actor:16raw  gde:u64  facet:u64  name:text  x:f32 y:f32 z:f32
          cooldownDuration:u64 (ns)  cooldownEnd:u64 (synced wall clock, Unix ns; 0: none)
          type:u32  overloaded:u8 = 0  failed:u8 = 0  id:text  unique:u32 }
```

- `actor`, `gde`: the actor and G of the server child of the checkpoint; `facet`: the facet target of its HomeComponent by the XOR rule
  (section 4.2: the entity of the component XOR K plus the component id) [project rule: the client compares no reference of a type 10
  point]. `name`: the text key of the name entity of the checkpoint slice (`@Respawn_EntranceName`, `@Respawn_ShrineName`) [data]. The
  place: the entity of the HomeComponent [data]. `type`: 10 Dungeon [data: m_respawnType]. `cooldownDuration`: m_respawnCooldown (30 s)
  [data]. `id`: the name of the spawner of the mode root (`Checkpoint_Entrance`; 31 bytes or less: 0x69128E0 copies it to 32 bytes)
  [project rule]. `unique`: the number of the checkpoint [project rule].
- The owner only (the UI values are of the local player: 0x34F0AB0) [static]. A character of the main world gets no fragment; a leave of
  an instance sends the empty list `00 00 00` [project rule].
- The respawn screen of a mode with DungeonRespawnsPermitted (0x34ECB00) shows each point of the type 10 with its name, its distance, and
  "Ready", and asks "This will put this respawn position in cooldown. Continue?" before the request [live: "Expedition Entrance 8m Ready";
  then "Shrine 6m" and "Expedition Entrance 206m"].
- The request **1938** of the selected point: `target:u64 entity:u64 random:u8 = 00 id:text` (the id of the element) [static; live:
  `random 0, id "Checkpoint_Entrance"`, then `"Checkpoint_Vestibule"`]. The backend puts the character at the place of the point in its
  space and sets the end of the cooldown of the point of that character (`cooldownEnd` of its element); an id that is not an active point of
  the instance of the character, or a point in its cooldown: the start of the map (the random rule) [project rule]. Field 1 homePointId is
  not sent.
- The activation of a point: the sphere of the detection volume of the server facet of the HomeComponent (`CheckpointAreaTrigger`,
  `PlayerOnlyCollision`; 5 m for a crystal, 30 m for the entrance) [data; the activation rule of the original server is not in the
  client: project rule].

### 4.69 The grid layer of the level information, the gate of a new membership, the PlayerArena state, the dungeon leave 926 (live proof of stage QG9e; static proof of the reads LEAVE and GATES of stage QG9e; the value 1, the audience, and the flag rule are project rules)

The static reads are LEAVE and GATES of stage QG9e (`~/Games/soulwarden/work/evidence/quests/qg9e/implement/static/LEAVE/ANSWERS.md`,
`.../static/GATES/ANSWERS.md`). The live runs are L3 and L4 of stage QG9e (`~/Games/soulwarden/work/evidence/quests/qg9e/implement/`:
`logs/l3-backend.log`, `logs/l4-backend.log`, the pictures G1-* and G2-*, the video `videos/qg9e-menu-leave.mp4`). The backend side is in
docs/systems/instances.md (the membership and the leave).

**The grid layer of 1635** [static; live]: the u64 after the four u32 values of the level information (body +0x60; reader 0x1500B7F to
0x1500BA3, big-endian through 0x87AD90). The handler copies it into the pending level information (0x64468C9), and LoadContextAndLevel
installs it as the current level information at Game +0x1C0 (0x6448DB7). `GameRequestsBus.IsInGridLayer` (implementation 0x64458D0) is
true only when the current level information has this value not zero [static]. The apply of the GameModeParticipant map (0x32B78C0) tests
it for a new entry whose map row has a CoatlicueName (0x32B79FD, 0x32B7A0B): false skips the insertion and OnEnteredGameMode; the entry
stays out of the accepted map, and a later revision tries again only with the same test [static]. Each story mode and each expedition has a
CoatlicueName (QuestMedea `coatlicue\NewWorld_VitaeEterna`, QuestAdiana, DungeonAmrine) [data].
- The backend sent 0 before stage QG9e. Live with 0: no dungeon name on the compass in an own level, the game menu with "Recall to
  Watchtower" off and an empty extra button (QuestAdiana, Amrine).
- The backend sends 1 in each level information (`kGridLayerId`) [project rule: the value of the original server is not known; 1 is the
  smallest value that is not zero]. Live with 1: the main world as before (the compass "Belltower of Time"); in QuestDracula the compass
  "Lilith's Dream" and the prompt "Soul Trial Exit"; in Amrine the compass "Amrine Excavation" after a travel from the Belltower of Time; the
  game menu of QuestDracula, Amrine, and QuestMedea with "Leave" [live L3, L4].

**MB PlayerArena state** (type 2406, slot 4 of the player slice) [static: LEAVE E4; live]. The backend sends group 0 field 0 isInArena,
a bool: the field fragment `01 01 01` (true) or `01 01 00` (false).
- ForfeitArena (0x2780110) sends 926 only while IsInArena() is true; fields 14 to 16 do not give it [static]. Live: the development
  fragment `01 01 01` of slot 4, type 2406, then the Yes of the dungeon leave sent 926.
- The owner only, after the first membership of the world entry: true while the membership is in a mode with IsDungeon, false in a mode
  with no IsDungeon and after the leave [project rule: the escape menu is of the local player; the arenas of the original server are not
  in this project].

**The dungeon leave of the escape menu** [static: LEAVE E5; live]: in a mode with IsDungeon (GetCurrentDungeonGameModeId 0x32AA820 scans
the accepted map) the respawn button of the menu shows `@ui_dungeon_leave` ("Leave"). Its popup (`Popup_ExitDungeon`: "Exit Soul Trial?",
"Exit Expedition?") calls `ForfeitArena(root, true)` at Yes.

**926 PlayerArenaComponentServerFacet_ForfeitArena** (client to server; ForfeitArena 0x2780110 [static]; live: 17 bytes):

```text
target:u64 entity:u64 leaveDungeon:u8 = 01
```

- `target`: the facet of the PlayerArena component (`RootPlayer/7158ce3f-e02d-44cb-badd-ba2a43f1eff4`, component 0ADF8519A10C374D; live:
  target 5734d46ca10c374f for the entity 4c554f5300000002).
- The backend leaves the instance for leaveDungeon 1 of a character whose membership is in a mode with IsDungeon (the leave of 3309 and
  1980: the exit rows, the return place, in the next tick); leaveDungeon 0 or a character in no such mode changes nothing [project rule].
  Live: QuestDracula, DungeonAmrine, and QuestMedea, each one 926 and the return to the place before the entry.

**The extra mode button** [static: LEAVE Q3; live]: with PlayerCanQuit the menu shows a second button with QuitButtonTextId of the mode row
("Leave" for QuestDracula; no text for DungeonAmrine). Its click asks the GameMode entity G of the client for a penalty
(`GetGameModeEntityId` 0x32AAE80); the client has no entity at the reserved G, the Lua bridge gets nil, and the click opens nothing and sends
nothing [live]. The backend sends no flag [project rule: no button that does nothing]; a client GameMode entity is an open item.

## 5. Open items

- User test 1 (2026-10-14). The flicker of far creatures ("enemies from a distance are
  flickering up and down, they become normal when closer") was not seen in a development
  run at the place of the user (night and fog). Measured: the backend sends no movement
  state for a creature at rest after its first record (the log has no later record of a
  creature at rest), and a ghost 73 m away goes up and down smoothly in a cycle of 3.4 s
  (its idle animation; no jump between two frames of 30 frames each second). The causes in
  the backend that the stage corrected can look so from far: the light of a spawn action
  with no body (a light that flickers, 4.14), the start of a kept action at each state, and a
  record with a new time in each state (4.14). The client rules of a far creature are not
  known: the interpolation and the extrapolation of a remote creature (0x48709A0, 0x2A29860,
  `g_actionStateExtrapolationLimitMS` 200 ms, research `navigation.md` Q5) work only with the
  states that the backend sends; an animation level of detail was not looked for.
- The projectiles (4.27, stage 3.3b-S3): the observers see no projectile of a player (no projectile entity, type 16, and
  no message 327 to bind the visual of the owner); the use of `m_extraSpeed` and `m_timeForMaxSpeed` of a projectile
  slice is not known (the server flies at `m_baseSpeed`); the server uses `m_accel` of the slice, not the predicted
  acceleration of the profile (the fire ball: 0 and −9.8); the heavy fire ball of a client of the harness does not come
  (the hold bit 191); the grouped spread (2719, 2570), a hit scan, the penetration depth, the bounces, and the fall of the
  damage are not done (the headshots: 4.30, stage 3.3b-S5).
- The speed of a status of a player (4.24, stage 3.3b-S1): the client does not apply the
  MoveSpeedMod or the SprintSpeedMod of a status of the owner map to its run (a haste of 0.2
  and of 0.4, a slow of −0.6: the distance of a walk of 2 s does not change). The server
  accepts the faster positions. Not tried: the stat container of the status component, a
  status of the first record before the world entry of the client.
- A conversation with a person (4.20, 4.55, 4.56): from stage 1.8b the backend accepts an
  offer (3772) and a hand-in (794), grants a cutscene (6707) and sends a whisper (5070). Open:
  the cinematics 6606 and 6542 have no live check with audio (4.58: a development run has no
  audio, and its client plays no video); a faction reward; a repeated quest (stage 1.8e: no
  repeatable quest of the data is supported, research of the support manifest); the person of a
  later topic that no state of the data names (`01_GraceWW_00`).
- Creatures (4.14) fight from stage 3.4b (4.17): a creature sees a character, goes to it,
  attacks it, and goes back to its home. Open for them:
  - From stage 3.4d a creature goes on a path of the navigation data (4.19): around a wall,
    up stairs, on the floor of a building, and through shallow water, and a wall stops its
    vision. Open: one agent size for each creature, no jump link and no drop from an edge, a
    path with more than 60,000 search steps is "no way", the creatures of one fight stand in
    a queue on a narrow way, the objects of spawners that are off at the start (doors, gates
    of a fort) are not in the data, and 1,207 spawn points of kinds that fight are on no free
    floor of the data (their creatures do not leave their place).
  - 814 of the 3,848 kinds have no attack that the tool can use (8,206 spawn points; 859 and
    8,612 before stage 3.4f-A): they do not attack, and a kind of them with roam data patrols
    or wanders (stage 3.4e). No group aggression, no ranged attack other than the basic shot
    of an archer (stage 3.4f-A, 4.37), no magic, no ability, and no flee behavior.
  - The reach of an attack of a creature is a project value, and the sequence and the pause
    of its attacks are project rules (4.19). The backend does not move a creature in an
    attack (the client moves the body in some attacks).
  - A melee attack of a player hurts a creature (stage 3.2a, 4.15). Open for it: the items
    of "Not known, not done" of 4.15 (the kind of each attack, ranged weapons, critical hits,
    the size of a creature).
  - The rewards of a kill and the respawn (stage 3.5a, 4.21). Open: the perks, the gems,
    and the rarity of an item instance (stage 1.3b), the loot bias for the equipment of a
    character, the loot limits (`[LIM]` conditions are not met), the filter of the looted
    items, the tags of a territory and of an event, group credit, rested experience, the
    territory standing and the Azoth of a kill, an item on the ground (ItemDrop, the
    LootTracker), and the persistence of the respawn times (a start of the backend makes
    each spawn point free). The body of a dead creature stays in the world of the backend
    until its respawn time, a command, or the distance rule removes it; the client removes
    the body itself after about 5 minutes.
  - The health of a creature is a rule of this project (the health of the level times two
    modifiers). A creature has its full health again at its home, and no regeneration in a
    fight. The states of the other components of a creature slice (stamina, status effects,
    the paperdoll of a creature with a weapon: its weapon does not show) are not sent.
  - The creature data has the whole world (stage 3.4c: 32,840 spawn points). 50 slices show
    no body or no name plate, and their 1,541 spawn points are not in the creature data
    (4.14, the table of the picture check). The spawn points of an event, of an encounter,
    the entries with a schedule, and the 499 entries with no placement at their position
    are not in the creature data.
- The health and the death of a character (4.18). Open for them:
  - The damage rule of a creature attack is a project rule (the formula of the original
    server is not known). No block, no dodge, no critical hit of a creature, and no
    stamina cost. The health that comes back out of a fight, and the 10 s with no target
    after a travel, are project rules too (4.19): the original values are not known.
  - The respawn place is the nearest travel place: the camps, the inns, and the houses of
    the respawn screen are system 2.6. The death has no cost (durability).
  - What the client of an observer shows for a dead remote player was not tested live.
- Travel (4.16) is a command of the backend. Open for it:
  - The fast travel of the game (the interaction of a shrine, the map, the costs, the
    recall to an inn) is system 2.6. The client shows the interaction text of a shrine, and
    the backend has no answer for it.
  - The character and the camera look north after a travel: the travel has no direction.
  - A travel with no message 3954, the kind 0 of message 3951, and the picture of a travel
    for the client of an observer are not checked (there is no second client).
  - The height of a travel with numbers is not checked against the ground: the backend has
    no terrain data.
- The position of a character is exact with the position report of the game hook (4.7).
  Open for it:
  - The backend has no movement simulation and no collision data: the check of a position
    report is a speed check. A position in a wall or below the ground passes it.
  - A refused position report has no answer: the client keeps its own position, and the
    backend keeps the last accepted one until the reports are in the limits again. A teleport
    of the server to the last accepted position is later work: the travel of stage 3.4c
    (4.16) has the wire mechanism and the rule for the first reports after it.
  - The fields of a value of entry +0x380 that is not 0 and the first two lists of an input
    entry have static proof only: no entry of a live run had one of them (4.7). The object of
    mask bit 8 has live proof (the interaction block of stage 2.5a, 4.35).
  - A character on a mount needs a larger speed limit. The speed limits come from one live
    session on foot.
  - The movement of a remote player for an observer is not in the backend.
- The camera estimate is the fallback for a client with no position field (4.7). Limits of
  the estimate:
  - With the camera farther than 4.0 m (wheel), the row is behind the character by the
    difference after each movement (4.0 m with the camera at 8.0 m). A camera turn at rest
    then moves the row around the character at that distance.
  - With an obstacle behind the camera at the end of a movement, the row is in front of the
    character by the difference between the distance of the estimate and the real distance
    (up to 3.7 m with a wall behind the camera).
  - With an obstacle behind the camera at the start (the spawn position of a new character
    has a wall behind the camera), a walk straight forward that does not move the camera
    axis from the known position does not show, and the row keeps the old position (1.7 m
    behind the character in one run, until the next camera turn). The same applies to a
    walk straight back of less than 3 m.
  - The backend does not know the camera distance after a wheel input in a movement, or
    after a wheel input while an obstacle is behind the camera. It reads only the newest
    entry of each report: a wheel step in a report that does not arrive is lost.
  - The bits of the forward key and of the wheel are numbers from captures (4.7), not from
    the table of the client. A forward movement with a different input (the bits of an
    automatic run are not known) shows only when the camera axis leaves the known position.
- The housing state of the player (slot 6, type 2768, field `m_isWithinAPlot`) also makes the
  client send its position, with effects on the camera, the compass, and the HUD of the
  client (static proof and game data, `~/Games/soulwarden/work/research/position.md`). The
  backend does not send it: the hook change gives the position with no such effect. When the
  backend gets a housing system (system 5.5), the flag of the entry has no meaning for it:
  each entry has the flag.
- A session of 40 minutes with no position correction was not done: the wrap of the input
  number was tested with a write of the number in the client memory (4.6).
- The S key starts a turn action that moves the character only slowly (animation or state
  data that the backend does not send).
- The other component states of the player. The state for stamina (slot 42) is a static
  candidate only. The mana state (slot 47) has live proof (4.5).
- Equipment (4.11). Open: the draw and the sheathe of a weapon and the change of the active
  weapon (the action system; the client of the owner shows its own weapon in the hand after
  an equip, and on the hip after a world entry); the rule for the ammunition slots; the
  checks of the client that the backend does not have (two weapons of one kind, a two hand
  weapon with an off hand item, the level of an item); a part of a stack (the backend moves
  full stacks); the equip load as a rule of the server; `BindOnEquip`; perks, dyes, skins,
  and the value of flag bit 0 of an item look; the durability loss; the sheathe request
  (3899), the loadouts, and the drop of an equipped item (826); the body of a character in
  the main menu after a change of its equipment (the menu shows the creation data).
- Level and attributes (4.12). Open: the sum rule of the maximum health has no proof in the
  client (the code was in the original server); the bonus level and the rested experience
  (sent as 0); the bonuses of an attribute from equipment and food (`attributeBonuses`,
  `placingBonuses`: sent with no element); the levels 61 to 70, which need the expansion row
  of the player in the client (the entitlements); the effects of an attribute other than the
  health (damage, the threshold abilities); the placing behavior request (6397); a source of
  experience in the game (the systems of a kill, a quest, and gathering call the experience
  function later: a kill does from stage 3.5a, 4.21); the text "N XP to Level M" of the open
  character window, which is one state behind.
- Skills (4.13). Open: the cost of a tree reset from rank 10 on (the currency "Azoth", which
  the backend does not have: it refuses such a reset); the use of an ability and its cooldown
  (the cooldown state, slot 45; system 3.3); the effect of a passive node; the table versions
  of the ability state (not sent); the other entries of slot 65 (territory standing, faction
  reputation, the season track) and the territory pools of slot 67; the trade skill levels
  201 to 250 and the riding skill, which need the expansion; the experience after the largest
  rank (the rows `_PostCap`); a bundle above 2,048 bytes (three chunks or more) has no live
  proof; a refused bar request has no live proof; the sequence of the ability tables is a constant of one client build, read from the
  live client (a different build needs a new read: `tools/research/ability_tables.py`).
- Items (4.10). Open: the perk index list of the client (the backend sends no perk); the
  dye values; the effect of the order counter `chrono` on the bag window; the fields 1 to 4
  of the container state (the backend sends none; the client shows the bag with no one of
  them); a full vector that is shorter than the vector of the client (not sent by the
  backend: the bag keeps its length while the character is in the world); the requests use
  (2917), salvage, repair, and transfer; an entity for an item on the ground (a drop removes
  the item); the weight limit as a rule of the server; the item line of the HUD for a new
  item (the cause values 1, 26 and others were not compared on the screen).
- Messages from the client in the world other than the input report have no handlers
  (`~/Games/soulwarden/work/research/client-messages.md` has the names and layouts).
- The world time (stage 2.2, 4.43): the client gets the record 7036 to its request 4248, the phase 4247, and the clock
  correction 335. Open: the fields `a` and `b` of the record, the PlayerTime component, and the weather of an event.
- Other entities (4.9). The backend has the record of a remote player that stands. Open:
  the movement of a remote player (new positions, the walk animation, the rate of the
  states); an exact position (a remote character does not go to the ground by itself); the
  action of a character (the backend sends the idle action for each character); the dye and
  the appearance of the equipment of a remote player (4.11); entities that are not players;
  region and area-of-interest data; a state bundle of more than 1,024 bytes (the limit of
  the carrier of the backend). With `--observe-players` and two clients, each client sees
  the character of the other one at the position of its world entry: this was not tested
  with two game processes.
- Names of the fields of `RegistrationRequestV3Msg` that 3.4 gives with no name, and the
  first `u32` of the message (`0x970C0A5D` in each capture).
- Authentication is a stub (2.1): no code proves the Steam id of a login. A real check needs
  the Steam client, which the network namespace does not let the game reach.
- The reaction of the client to status 401 from a gateway route, and to `isNewAccount` true
  in the token reply.
- The name of a character is unique for all accounts, with no regard to the letter case
  (2.2.2). The index `characters_active_name` of the database refuses only names that are
  equal in ASCII letter case: the backend makes the full check before each insert.
- A character with no creation data (a seeded character) has the default appearance and no
  clothing in the menu and in the world (2.2.5). It has a white body in the menu when the
  backend has no appearance tables.
- A new character starts at the spawn position of the seeded character. It has the items
  of its archetype that have a quantity in the backstory table (4.11): four armor pieces and
  one sword. The head armor and the weapons of the archetype have the quantity 0 there: the
  original game gave them later (the first quest, system 1.8 and 4.3). The backend keeps the
  archetype (`characters.archetype`). It does not use the `equipment` entries of the creation
  data, and it does not keep `SkipFtue`.
- The backend has the name rule of the client (2.2.2), but not its check of the words of a
  name. The client does not send a name that its rule refuses, and it does not send creation
  data that the backend cannot read: the reaction of the client to the replies
  `NameLengthError` and `NameValidationError` of the name check, and to the other create
  errors of 2.2.4, was not seen.
- The option "Change World" of a character row, and the rename route
  (`PATCH /prod/game/characters/{id}`), have no handler.
- The world content document (`motd/worlds_STEAM_APP_ID.1063730.json`) has no route (404).
  The landing screen and step 4 of the creation work without it.
- Sessions (3.5, 3.6). Not tested: what the client does after OK on the popup of a kick;
  the message "kicked" with reason 2 (the delete of a character that is in the world) in a
  live client; the registration results with reason 9; what the client sends when the user
  closes the game window while the character is in the world (the launcher stops the backend
  when the game process ends). The refusal and the kick as product behavior have test cases
  with made-up clients and no live run of their own: the live checks of 3.6 used the research
  switch with the same bytes.
- The login ticket: the backend does not keep the `TicketId` of a login queue reply, and a
  registration can name each character of the account of its login token (3.4).
- The actor id of a character is new for each world entry (4.2). The client uses the actor
  id of a player as an argument of game messages (for example type 2556): a system that
  keeps such an id in the database needs an id that stays (the research `ids.md` proposes a
  serial in the row of the character).
- The backend checks the route and the target of each client message and counts a message
  that fails (4.2). It does not refuse a message for this cause now.
- The client context instance has one byte. After 255 sessions of one backend process the
  numbers start again: a client whose two connections have 254 sessions of other clients
  between them gets the instance of its last connection, and its world entry then fails
  (3.5). With one client this does not occur.

## 6. Tool window: renderer and input of the client, and the admin interface

The hook DLL draws one Dear ImGui window on the picture of the game (key F8). The window
sends commands to the admin interface of the backend. This section has the client facts that
the hook uses, and the contract of the admin interface. Source of the static proof:
`~/Games/soulwarden/work/research/overlay.md` (verified). Each "live" mark is a check of stage 1.7
on one computer (NVIDIA card, Proton Experimental, VKD3D-Proton), with the trace points of
the hook (`SOULWARDEN_TRACE`, `SOULWARDEN_PROBE`) and with memory reads of the harness.

### 6.1 Renderer of the client (static proof and behavioral proof)

| Fact | Evidence |
| --- | --- |
| The client renders with Direct3D 12. With Proton the calls go to VKD3D-Proton. | Static (import table), game log (`RenderD3D12`) |
| The client has one present call: `call [rax+0x40]` at `0x8D4468`, in the thread function `0x8D4250` (the submission thread of its renderer, command 5). The instructions from `0x8D445C` are `mov rax,[r14]; mov r8d,r15d; mov edx,r12d; mov rcx,r14; call [rax+0x40]`. | Static |
| At `0x8D445C`: `r14` is the swap chain (`IDXGISwapChain3*`), `r12d` the sync interval (1 in the run), `r15d` the present flags (0). The stack pointer is a multiple of 16. | Live (probe) |
| The present thread is not the window thread. In the runs: window thread 348 (it also made the swap chain), present thread 1024 or 1028. | Live (probe, trace of `0x7159680`) |
| NVIDIA Streamline is active with Proton on this computer: the flag byte at `0xA42C608` is 1. The swap chain of the present call is an object of `sl.interposer.dll` (its `Present` function is at that library `+0x29010`), and the command queue of the swap chain is an object of that library too. | Live (memory reads, module of each method table) |
| Global `0xA2E96F0`: the native `ID3D12Device*` (a function of `d3d12core.dll` in its method table). Global `0xA2E96F8`: the device of Streamline (not null in the run). | Live |
| Global `0xA2E96C0`: the queue manager. `[+0x78]` is the record of the direct queue, and `[record+0x10]` is its `ID3D12CommandQueue*`. This pointer is the queue argument of `CreateSwapChainForHwnd` (`rdx` at `0x8B8937`). | Static, live (probe) |
| Global `0xA2E9CB8`: the device object. `[+0x178]` is the swap chain record of the game window: `[+0x00]` the `HWND`, `[+0x70]` the native swap chain, `[+0x78]` the swap chain that the game presents when Streamline is active, `[+0x28]` the `DXGI_SWAP_CHAIN_DESC1`. | Static, live (memory read) |
| The swap chain of the game window: 1256x660 in a development run, format `DXGI_FORMAT_R8G8B8A8_UNORM` (28), swap effect 4, flags `0x840`. `GetDesc` of the swap chain of the present call gives 2 buffers; the description in the record gives 4. | Live. The research gave 2 buffers and the flags `0x800`. |
| A command list of the native device runs on the command queue of Streamline: the tool window draws with this pair for each frame. | Live (runs of 11 minutes) |
| At the present call the back buffer has the state "present": the tool window changes it to "render target" and back. No device removal in the runs. | Live |
| `D3DCompile` of the `d3dcompiler_47.dll` of the Proton prefix compiles the two shaders of Dear ImGui. | Live (`renderer ready` log line) |

The hook of the tool window is at the instructions of the call site (`0x8D445C`), not in the
`Present` function. The call site is the same with Streamline, with the DXGI library of
DXVK, and with the DXGI library of Windows, and no other thread uses it. The hook compares
the 15 bytes at the address with the bytes of the supported build before it changes them. The
hook of the tool window is installed before the trace points, so that a trace point at the
same address does not change the bytes of the compare (the two together were not checked
live).

### 6.2 The frame of the tool window

- The first present call makes the objects: one heap for the render target views (one for
  each buffer of `GetDesc`), one heap for the textures of Dear ImGui, three command
  allocators, one command list, one fence, the Dear ImGui context, and the Direct3D 12 part
  of Dear ImGui (`ImGui_ImplDX12_Init` and `ImGui_ImplDX12_CreateDeviceObjects`). A failure
  of a step turns the tool window off for the start, with one log line (`tool window: OFF
  for this start of the game: CAUSE`). The game continues.
- While the window does not show, the hook does one test in each present call and draws
  nothing.
- While the window shows, each present call makes one Dear ImGui frame and one command
  list: the buffer of `GetCurrentBackBufferIndex`, a render target view for it, the two
  state changes, and `ExecuteCommandLists` and `Signal` on the queue of the game. The hook
  keeps no reference to a back buffer after the frame, thus a resize of the swap chain needs
  no hook.
- The hook stops the tool window (with a log line) when the swap chain changes its format
  or gets more buffers, and when the device is removed.
- The text and the controls have the scale 1 for a picture of 720 lines or less, and a larger
  scale for a larger picture, in steps of 0.25 (1.5 for 1080 lines, 2 for 1440 lines, 3 at
  most).

### 6.3 Input of the client (static proof and behavioral proof)

| Fact | Evidence |
| --- | --- |
| The root window procedure of the game window is the function `0x7159680` (class `GameWindowClass`). It runs on the window thread. | Static, live (trace) |
| The client reads its keys and its pointer from raw input (`WM_INPUT`, branch at `0x7135300`) and text from `WM_CHAR` (branch at `0x7135366`). | Static, live (probes) |
| With Wine, the game window gets the usual key and pointer messages too (`WM_KEYDOWN`, `WM_KEYUP`, `WM_CHAR`, `WM_MOUSEMOVE`, the button messages) while raw input is registered, also for the input of `SendInput`. | Live (message counts of the hook: `tool window: messages of the game window: ...` in a harness run) |
| The text of the harness (`SendInput` with a Unicode character) gives `WM_KEYDOWN`, `WM_CHAR`, and no raw keyboard packet. | Live (message counts) |
| The client sets the pointer position with `SetCursorPos` (import slot `0x7ECA3F8`) and limits the pointer with `ClipCursor` (import slot `0x7ECA580`). Each slot has one user function. | Static |
| The client reads a raw input packet with `GetRawInputData` (import slot `0x7ECA660`; two calls, at `0x7135319` and `0x7135337`, in the raw input branch of the window procedure). The client image has no import of `GetRawInputBuffer`. | Static; live for the read of a pointer packet (stage 2.3a) |
| With Wine, one input event of `SendInput` with a button release and a relative movement (`MOUSEEVENTF_RIGHTUP` and `MOUSEEVENTF_MOVE`) gives one raw pointer packet with the release flag and the movement. | Live (hook log of a harness run: `button flags 0x0008, movement 14 5`) |
| The right pointer button in the world is the block: while the game has the button down, the action sequence of layer 0 of the character is 1248. | Live (memory read) |
| In the world the game hides the pointer of the system (`GetCursorInfo` shows no pointer): Dear ImGui then draws one. On a screen of the game with a pointer (the inventory), the system shows its pointer, and Dear ImGui draws none. | Live |

The hook replaces the window procedure (a detour of `0x7159680`; the bytes at the address
are compared first) and the two import slots. Rules:

- The game never gets the toggle key (F8): not the key messages, and not the raw packet.
  The window changes its state at the key press. A repeat of a key that stays down changes
  nothing.
- While the window shows, the game gets no pointer message, no key message, no character
  message, and no raw packet of a key press, of a button press, of the wheel, or of a pointer
  movement. It gets the raw packets of a key release and of a button release: a key that was
  down when the window opened is then released in the game (live: the character stopped
  when the W key was released after F8). Alt+F4 and each other message go to the game.
- A raw pointer packet can have a button release together with other events (a movement,
  the press of a different button, the wheel). The game gets each packet with a release, and
  it reads only the releases of it: the hook replaces the import slot of the client for
  `GetRawInputData`, and its function clears the other button flags, the wheel data, and the
  movement of the packet that the game reads at that time (finding 3 of the code review of
  stage 1.7; before the correction such a packet did not go to the game, and the game kept
  the button down). Live check: with the right button down in the game (block stance), the
  tool window open, and a release with a movement in one input event, the character left
  the block stance.
- The hook takes the pointer capture for a button that goes down on the tool window. The
  window thread releases it when the button goes up, when the window closes, and when the
  tool window stops (a failure of the renderer): the stop posts a message with no function
  to the game window, and the window procedure of the hook ends the capture before its test
  for the stopped state (finding 4 of that review; live check with the harness command
  `toolwindow stop`).
- The window thread puts the messages in a queue, with the state of the Control, Shift, and
  Alt keys at that time. The present thread gives the queue to Dear ImGui before each
  frame. The window thread makes no Dear ImGui call. The hook uses only the key table of the
  Win32 part of Dear ImGui (`ImGui_ImplWin32_KeyEventToImGuiKey`): the message handler and
  the frame function of that part read the keyboard state of the thread that calls them.
- While the window shows, `SetCursorPos` of the game does nothing and `ClipCursor` of the
  game applies no limit. The limit that the game set last applies again when the window
  closes.
- The second extra pointer button and the horizontal wheel have no function in the window
  (the harness uses that button to make the game window the foreground window).

Live results of stage 1.7 (run with the character in the world): with the window open, the
text of the harness and the keys W, K, and M went into the search field, the position of the
character did not change (8880.00, 4250.01), and no window of the client opened. After F8,
the W key moved the character (8879.46, 4248.97).

### 6.4 Admin interface of the backend

The routes of the tool window. They are on the HTTP listener of the backend (loopback only).
Each start has them when the launcher gave the backend a token: they need no harness gate
and no development option.

```text
GET  /admin/v1/state[?character=ID]
     {"account": true, "catalog": true, "revision": N,
      "characters": [{"id", "name", "level", "inWorld", "session"}, ...],
      "selected": ID, "position": [x, y, z], "items": {...}, "progress": {...},
      "creatures": {...}, "health": {...}}
     The characters of the account of the token. "position": the place of the selected
     character when it is in the world (the tab "Travel" shows it and computes the distance
     of each place from it). "items" and "progress" are for the character
     of the request, or, with no id, for the character of the account that a session plays,
     then one that is in the world, then the first one. "items": the bag ("items": slot, item,
     quantity, gearScore, durability, locked, instance), the equipment, the coin (hundredths),
     and the slot count. "progress": the level, the experience and the limits of the level,
     each attribute (value, spent points), the free points, the health, each weapon mastery
     (level of the screen, experience, free and spent points, nodes, bar), each trade skill.
     The two members are the replies of GET /v1/dev/items and GET /v1/dev/progress.
     "creatures": the creatures that are 150 m or less from the character, the nearest one
     first ("creatures": id, kind, name, level, health, healthMax, dead, spawnPoint,
     position, distance, and from stage 3.4b "mode" (rest, fight, home), "target", "fights",
     and "walks"), the number of all creatures of the world, and the number of spawn
     points that wait (the reply of GET /v1/dev/creatures with a range).
     "health" (stage 3.4b): "inWorld", "health", "healthMax", and "dead" of the selected
     character, "creaturesIgnore" (the creatures ignore this character), and
     "creatureBehavior" (false: the behavior of the creatures is off for this start).
POST /admin/v1/commands
     {"command": NAME, "character": ID, "args": {...}}
       give_item           item (the ItemID text), quantity (1 to 100000), gearScore
       remove_item         slot (a slot of the bag), quantity (no quantity: the full stack)
       set_coin            amount (hundredths of a coin, 0 to 100000000)
       set_level           level (a level of the screen)
       add_experience      amount (1 to 10000000)
       set_attribute       attribute (constitution, focus, strength, dexterity, intelligence), value
       set_weapon_mastery  weapon ("SwordAbilityTable" or "sword"), level (1 to 20), experience
       set_trade_skill     skill ("Mining"), level, experience
       spawn_creature      kind (an id of the creature data), level, dx, dy, dz (meters from the
                           character: east, north, up; with none: 3 m to the north), yaw
       remove_creature     creature (the id of a creature: 16 hexadecimal digits)
       set_creature_health creature, and health (a number) or percent (0 to 100)
       kill_creature       creature
       reset_creatures     no argument: each spawn point that waits is free again
       travel              place (the id of a travel place), or x, y, z (a position in
                           meters: a height from -99 to 999), not the two; screen (auto,
                           on, off: the loading screen of the client, 4.16)
       heal                no argument: the character has its full health; a dead
                           character lives again at its place (4.18)
       set_creatures_ignore  ignore (1 or 0): the creatures ignore the character (4.17)
     heal for a character that is not in the world changes nothing (reply 200 with
     "applied": "stored": each world entry starts with the full health). The ignore value
     is a value of the character in the memory of the backend, also for a character that is
     not in the world.
     travel moves the character to the place (4.16). Reply 200 with "applied": "world", the
     position, the distance, and the place; or with "applied": "stored" for a character
     that is not in the world (its row has the place, and its next world entry is there).
     Reply 404: no place with that id. Reply 400: a place and numbers together, a number
     that is not in the limits, a wrong text for screen, a place id with no travel places
     file.
     The character of a creature command is the reference place (4.14): it must be a
     character of the account of the token, as for each command. remove_creature,
     set_creature_health, and kill_creature change only a creature that is 150 m or less
     from that character (the distance of the list of the state), and the character must
     be in the world. Reply 404 with a message for a character that is not in the world,
     and for a creature that is farther (the same reply as for an id of no creature).
     Reply 200: {"message": "done", "applied": "world" | "stored", ...} ("stored": the
     character is not in the world, and its rows changed). Reply 400: {"message": CAUSE}, and
     nothing changed. A member of "args" can be a number or a text.
GET  /admin/v1/catalog/items?q=TEXT[&limit=N][&offset=N]
     {"loaded": true, "total": N, "offset": N,
      "items": [{"id", "type", "class", "tier", "maxStack", "minGearScore", "maxGearScore"}, ...]}
     The items of the item catalog whose id, type, or class has each word of the text (no
     letter case). limit: 1 to 200, default 50. An item with no type is not in the result.
GET  /admin/v1/catalog/creatures?q=TEXT[&limit=N][&offset=N]
     {"loaded": true, "total": N, "offset": N,
      "kinds": [{"id", "name", "vitalsName", "category", "family", "creatureType", "level",
                 "health", "spawnPoints", "slice"}, ...]}
     The kinds of the creature data whose id, name, category, or family has each word of the
     text. "health" is the health of the kind at its level.
GET  /admin/v1/catalog/places
     {"loaded": true,
      "places": [{"id", "kind", "name", "territory", "territoryId", "position": [x, y, z]}, ...]}
     The travel places of the game data (4.16): each one, with no filter (161 places in the
     supported build). "kind": "shrine" or "inn". With no file: "loaded": false and an empty
     list. The list does not change after the start of the backend: the tool window
     asks for it until it has a reply, and then keeps it.
```

Rules:

- The query of a request with a path below `/admin` is in no log line and in no capture
  file: the record has the text `(not recorded)` in its place (`capture::recorded_target`).
  A sender can put the token in the query by mistake: the route refuses that request (401),
  and the record of the refusal must not keep the token (finding 5 of the code review of
  stage 1.7; test case X7).
- An argument of a command that is not necessary (`quantity`, `gearScore`, `action`,
  `experience`) and that is there with a wrong JSON form (an array, an object, `null`) gives
  the reply 400, and nothing changes. It is not the same as no argument: a `remove_item`
  with no quantity removes the full stack (finding 2 of that review; test case X8).
- A command of the tool window has the character id of the snapshot that the window drew
  when the user gave the command, not the selected character at the time of the send
  (finding 1 of that review).
- The token. The launcher makes 48 hexadecimal characters from `/dev/urandom` for each start
  and gives them to the backend and to the game in the environment variable
  `SOULWARDEN_ADMIN_TOKEN` (not as an option of a command line). The backend also gets the Steam
  id of the game of the start (`SOULWARDEN_ADMIN_STEAM_ID`; with no value, the fallback identity).
  A request has the token in the header `X-Soulwarden-Admin`, never in its path or its query. The
  backend and the hook write the token to no log line, and a capture file of the backend has
  the text `(not recorded)` in place of the header value.
- A request with no token, or with a token that is not the token of the start, gets the
  reply 401 with no data, and nothing changes. A token has 32 characters or more. The
  compare does not stop at the first different character.
- A request with a service host (`X-Soulwarden-Host`: a request of the game, which the hook
  redirects) or with an `Origin` header (a page of a browser) gets the reply 403.
- The token is for one account: the account of the Steam id of the start. The backend finds
  the account from the token, not from the request (rule MP-5). A command names a character
  id (rule MP-9). A character of a different account gives the reply 404, the same as a
  character that is not there.
- A command runs as a task of the world thread and calls the function of the world that the
  development route for it calls (`server/http/character_commands.h` is the one layer of the
  two route sets). The session of the character gets the state change through the encoders.
- The state route and the search route give no log line and no capture file for a reply
  200: the tool window calls the state route each second while it shows. Each command has a
  log line (`admin: command NAME for character ID8: reply STATUS`).
- With no token in its environment the backend has no admin route (reply 404), and the log
  says so. `SOULWARDEN_TOOL_WINDOW=0` makes the launcher give no token.

### 6.5 Switches of the tool window

| Variable | Read by | Effect |
| --- | --- | --- |
| `SOULWARDEN_TOOL_WINDOW` | launcher, hook | `0`: the hook installs no hook of the tool window and makes no Dear ImGui context, and the launcher makes no token. Default: on. |
| `SOULWARDEN_TOOL_WINDOW_KEY` | hook | Virtual key of the window, 1 to 254 (decimal or `0x` text). Default `0x77` (F8). |
| `SOULWARDEN_ADMIN_TOKEN`, `SOULWARDEN_ADMIN_STEAM_ID` | backend, hook (token only) | The launcher sets them. Do not set them by hand for a start of the launcher. |
| `SOULWARDEN_TOOL_WINDOW_FAULT` | hook, only with `SOULWARDEN_HARNESS=1` | A test of a start with a fault: `hook` (the code at the present call is not the code of the build), `device` (no Direct3D 12 device at the first present call), `swapchain` (no description of the swap chain). |

Harness commands (`tools/hx`): `toolwindow` gives `shown` or `hidden`, `toolwindow 1` and
`toolwindow 0` show and hide the window with no key, `toolwindow scale PERCENT` sets the
scale of the text and of the controls for a check of the layout (0: the automatic scale), and
`toolwindow stop` stops the tool window for the rest of the start (a test of the stop).
`mousedown left|right` and `mouseup left|right [DX DY]` press and release a pointer button
(with `DX DY`: the release and a relative movement in one input event).

### 6.6 Limits and items that are not checked

- One computer, one graphics card, Streamline active. A start with no Streamline (the swap
  chain of DXVK) has static proof only: the call site and the globals are the same.
- A picture with HDR (format `R10G10B10A2`), a change of the resolution in the settings of
  the game while the window shows, and frame generation were not checked.
- The layout was checked live at 1256x660. The larger scales were checked with a forced
  scale at that picture size (`toolwindow scale 150` and `200`), not with a larger picture.
- The pointer of the system does not show in a screenshot of `tools/shot`. The pointer that
  Dear ImGui draws in the world does.
- The item search uses the id, the type, and the class of an item. The catalog has no name
  text of an item (only its text key), thus a search for the name on the screen finds
  nothing.
