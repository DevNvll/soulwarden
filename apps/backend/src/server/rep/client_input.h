#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Input report of the client in the world:
// Javelin::ClientMessages::ActionListComponentServerFacet_ReceiveInputFromClient (type 1589).
// The client sends it 30 times each second on the unreliable carrier channel.
//
//   body   := component:u64 entity:u64 header count:u8 entry*
//             (client readers at image RVA 0x4867C70, 0x2A2E490)
//   header := u8 u8 u8 u8 size64 size size u8 [flag:u8, size if set] u64 u8 size size size u8
//             pairs:size (size size)* size64 size64 size size
//             (client reader at image RVA 0x2A2E6B0)
//   entry  := mask:3 bytes (little-endian) sequence:u16 optional fields
//             (client reader at image RVA 0x2A25D90)
//
// The entries are newest first: the newest input of the client, and then the inputs before it
// (a client sends an input in up to 5 reports, so that the loss of a datagram loses no input).
// Each entry is a difference from the entry before it in the message, and the first one is a
// difference from a default entry. The backend reads each entry to its end (protocol notes 4.7):
//
//   target   := kind:u8 [17 bytes if the kind is not 3]          (client reader 0x416EB00)
//   [object if mask bit 8: present:u8, and 130 or more bytes if it is 1 (client reader 0x35EBC30)]
//   [size size size if flags bit 0: the action report]
//   bit := 9
//   [flags bit 11: world position if mask bit 9; bit := 10]
//   [flags bit 8: world position if mask bit `bit`; 16 bytes and u64 if mask bit `bit`+1; bit += 2]
//   [u8 if mask bit `bit`: the value of entry +0x380]; bit += 1
//   [value of entry +0x380 not 0: world position if mask bit `bit`; 32 bytes if mask bit `bit`+1; bit += 2]
//   [flags bit 14: 8 bytes if mask bit `bit`; bit += 1]
//   list list list                 count:size and the items of each list: the hits of a projectile
//                                  with no flight (104 bytes each), the shots (192 bytes each),
//                                  and the melee contacts (96 bytes each)
//   [flags bit 15: world position if mask bit `bit`: the position of the character; bit += 1]
//   [3 half floats if mask bit `bit`]; bit += 1
//   [u64 if flags bit 16] [u8 if flags bit 18]
//   [u8 if mask bit `bit`] [u32 if mask bit `bit`+1] [half float if flags bit 0 and mask bit `bit`+2]
//
// A melee contact (client reader 0x2A18490; the client fills the list only with its setting
// javelin.enable-client-authoritative-melee on, protocol notes 2.3):
//
//   provider:u32  group := actor id:16 bytes G:u64  receiver := actor id:16 bytes G:u64 target:u64
//   point:3 f32  normal:3 f32  direction:3 f32
//
// The receiver names the hit volume of the entity that the weapon of the character touched.
// The item has no damage value: the world computes the damage (world/combat.h).
//
// The client sets flag bit 15 from the value "within a plot" of its housing component. The
// game hook makes the client set it in each entry (apps/game-hook, client/hooks/movement), thus
// each input report of a client with the hook has the position of its character. A client
// with no hook sends no position field.
//
// The action words of an entry have one bit for each action of the input provider of the
// client: the position of the action in the list of its input profile, in the sequence of
// the CRC-32 values of the action names (client code at image RVA 0x6501F30). The backend
// reads the bits of the actions that it uses (client_input.cpp).

namespace soulwarden::backend::server::rep {

/** Kind of the target data of an entry that has no target data. */
inline constexpr std::uint8_t kInputTargetNone = 3;
/** Flag of an entry: the client fills and sends the position of its character. */
inline constexpr std::uint64_t kInputFlagPosition = 1ULL << 15;

/** How far the reader got after the camera position of an entry. */
enum class InputTail {
    /** The reader read the entry to its end. */
    read,
    /** The entry has a value that the client does not send: a list that is longer than the body, a flag that is not 0 or 1. */
    badValue,
    /** The body ended before the end of the entry. */
    shortBody,
};

/** One melee contact of an input entry: the weapon of the character touched a hit volume of an entity. */
struct InputContact {
    /** Number of the input provider that was active. 0xFFFFFFFF: none (each live contact of a sword). */
    std::uint32_t provider{0};
    /** The entity group of the receiver: its actor id and its entity group id. */
    std::array<std::uint8_t, 16> groupActor{};
    std::uint64_t group{0};
    /** The receiver: the actor id and the entity group id of the entity, and the facet target of its hit volume. */
    std::array<std::uint8_t, 16> actor{};
    std::uint64_t entity{0};
    std::uint64_t facet{0};
    /** The contact in the world: the point, the normal of the surface, and the direction of the weapon. */
    std::array<float, 3> point{};
    std::array<float, 3> normal{};
    std::array<float, 3> direction{};
};

/**
 * One shot of an input entry (an item of the second list, 192 bytes; client readers 0x2A1A0D0, 0x2A1A590; protocol notes
 * 4.27): the projectile of a ranged attack, of a magic attack, or of an ability.
 *
 *   0 origin:v3  12 direction:v3  24 offset:v3  36 point:v3 (the hit near the muzzle)  48 identifier:16 bytes  64 u32
 *   68 owner := actor id:16 bytes G:u64   92 receiver := actor id:16 bytes G:u64 facet target:u64 (the hit near the muzzle)
 *   124 group := actor id:16 bytes G:u64  148 token:u64  156 u64  164 f32  168 f32  172 charge:f32  176 ammunition:u32
 *   180 attack:u32  184 row:u32  188 u8 u8  190 category:u8  191 u8
 *
 * The attack is the CRC-32 of the lower-case ranged attack name ("basic", "basic_heavy"; the projectile of Gravity Well has
 * "gravitywell": live check L7 of stage 3.3a), the row the CRC-32 of the lower-case damage row (0 for a magic shot), and the
 * ammunition the CRC-32 of the lower-case ammunition id (DefaultArrow for a bow with no equipped arrow; 0 for a magic shot).
 * The item has no number of its own: two shots differ only in their values (live check L2: 46 shots).
 */
struct InputShot {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{};
    /** The point of the hit near the muzzle (wire 36), and its receiver (wire 92): `hasReceiver` is false for a receiver with no actor id. */
    std::array<float, 3> point{};
    bool hasReceiver{false};
    std::array<std::uint8_t, 16> receiverActor{};
    std::uint64_t receiver{0};
    std::uint64_t facet{0};
    float charge{0.0F};
    std::uint32_t ammo{0};
    std::uint32_t attack{0};
    std::uint32_t row{0};
};

/** What the backend reads of each entry of a report for the attacks of the character. */
struct InputStep {
    /** Input number of the entry. */
    std::uint16_t sequence{0};
    /** The attack inputs of the client: the pointer button went down, is down, went up. */
    bool attackPress{false};
    bool attackHold{false};
    bool attackRelease{false};
    /** The key of a weapon of the paperdoll came: 1 or 2. 0: none. */
    std::uint8_t weaponKey{0};
    /**
     * The press of a key of the ability bar came (the bits "ability1", "ability2", "ability3":
     * 402, 115, 139; live check L1 of stage 3.3a): 1, 2, or 3 for the place 0, 1, or 2. 0: none.
     * The bit is in the first entry of a press only (the hold and the release have their own bits).
     */
    std::uint8_t abilityKey{0};
    /**
     * The key of a quick slot came (Use_Quickslot_1 to 4: the bits 404, 117, 140, 513; live check I7 of stage 1.3b): 1
     * to 4 for the paperdoll slots 14 to 17. 0: none.
     */
    std::uint8_t quickKey{0};
    /** The key of the heart rune came (rune_ability: bit 450; live check I6 of stage 1.3b). */
    bool runeKey{false};
    /** Number of items of the two projectile lists. */
    std::uint32_t projectileHits{0};
    std::uint32_t shots{0};
    /** The shots of the second list: the projectile of a ranged or magic attack, or of an ability. */
    std::vector<InputShot> shotItems;
    /**
     * The aim input of the client is on ("aim_draw", bit 126; the right pointer button of the default profile): the bow and
     * the musket aim. For the log only.
     */
    bool aim{false};
    std::vector<InputContact> contacts;
};

/** The newest input entry of one input report. */
struct ClientInput {
    /** Input sequence number. The client adds one for each simulation step (30 Hz). */
    std::uint16_t sequence{0};
    /** Movement and camera axes, each in the range -1 to 1. Zero is the rest position. */
    std::array<float, 4> axes{};
    /** True while the key that moves the character forward is down. */
    bool forward{false};
    /** True for one step of the pointer wheel. The wheel changes the distance of the camera. */
    bool wheel{false};
    /** True when the entry has the look direction of the camera. */
    bool hasLook{false};
    /**
     * Direction of the camera in radians (client reader at image RVA 0x87B4A0): yaw from north,
     * positive to the left, and pitch, positive down.
     */
    float lookYaw{0.0F};
    float lookPitch{0.0F};
    /** True when the entry has the camera position. */
    bool hasCamera{false};
    /** Position of the camera in the world (meters). It is not the position of the character. */
    float cameraX{0.0F};
    float cameraY{0.0F};
    float cameraZ{0.0F};
    /** Flags of the entry. 0 when the entry has no flags field: the value of the default entry. */
    std::uint64_t flags{0};
    /** Kind of the target data of the entry. */
    std::uint8_t targetKind{kInputTargetNone};
    /**
     * `read`: the reader read the newest entry to its end. A different value is not an error:
     * the entry then gives no position, and the session keeps the last one.
     */
    InputTail tail{InputTail::read};
    /** True when the entry has the position of the character (flag bit 15 and the field). */
    bool hasCharacter{false};
    /**
     * Position of the character in the world after the physics step of the client (meters).
     * The client cuts the height off to a step of 1100 / 65535 m: the value is 0 m to 0.017 m
     * below the height of the character.
     */
    float characterX{0.0F};
    float characterY{0.0F};
    float characterZ{0.0F};
    /** The 16 bits of the height on the wire. 0 and 0xFFFF are the two limits of the height range. */
    std::uint16_t characterHeight{0};
    /** Number of entries of the report, and how many of them the reader read to their end. */
    std::uint8_t entries{0};
    /**
     * The attack data of each entry that the reader read to its end, newest first. The reader
     * stops at the first entry that it cannot read: an entry is a difference from the one
     * before it.
     */
    std::vector<InputStep> steps;
};

/**
 * @return The newest entry of an input report with the attack data of each entry, or nothing
 *         when the body does not parse to the camera position of the newest entry. A body that
 *         does not parse after it gives the entry with no position of the character.
 */
[[nodiscard]] std::optional<ClientInput> read_client_input(std::span<const std::uint8_t> body);

} // namespace soulwarden::backend::server::rep
