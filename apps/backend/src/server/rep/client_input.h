#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

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
// The entries are newest first. Each entry is a difference from the entry before it in the
// message, and the first one is a difference from a default entry. The backend reads the
// first entry only: its fields up to the camera position, and then the fields up to the
// position of the character (protocol notes 4.7):
//
//   target   := kind:u8 [17 bytes if the kind is not 3]          (client reader 0x416EB00)
//   [block if mask bit 8: the layout is not known]
//   [size size size if flags bit 0: the action report]
//   bit := 9
//   [flags bit 11: world position if mask bit 9; bit := 10]
//   [flags bit 8: world position if mask bit `bit`; 16 bytes and u64 if mask bit `bit`+1; bit += 2]
//   [u8 if mask bit `bit`: a value that is not 0 has blocks with a layout that is not known]
//   bit += 1
//   [flags bit 14: 8 bytes if mask bit `bit`; bit += 1]
//   size size size                 the counts of three lists (the item layouts are not known)
//   [flags bit 15 and mask bit `bit`: world position: the position of the character]
//
// The client sets flag bit 15 from the value "within a plot" of its housing component. The
// game hook makes the client set it in each entry (apps/game-hook, client/hooks/movement), thus
// each input report of a client with the hook has the position of its character. A client
// with no hook sends no position field.
//
// The action words of an entry have one bit for each action of the input provider of the
// client. The backend knows the bits of three actions from captures (client_input.cpp).

namespace soulwarden::backend::server::rep {

/** Kind of the target data of an entry that has no target data. */
inline constexpr std::uint8_t kInputTargetNone = 3;
/** Flag of an entry: the client fills and sends the position of its character. */
inline constexpr std::uint64_t kInputFlagPosition = 1ULL << 15;

/** How far the reader got after the camera position of an entry. */
enum class InputTail {
    /** The reader read the entry to the place of the position of the character. */
    read,
    /** The entry has a block whose layout the backend does not know. */
    unknownBlock,
    /** The body ended before the place of the position of the character. */
    shortBody,
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
     * `read`: the reader got to the place of the position of the character. A different value
     * is not an error: the entry then gives no position, and the session keeps the last one.
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
};

/**
 * @return The newest entry of an input report, or nothing when the body does not parse to the
 *         camera position of that entry. A body that does not parse after it gives the entry
 *         with no position of the character.
 */
[[nodiscard]] std::optional<ClientInput> read_client_input(std::span<const std::uint8_t> body);

} // namespace soulwarden::backend::server::rep
