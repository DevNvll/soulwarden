#include "server/rep/client_input.h"

#include "server/hub/wire.h"

namespace soulwarden::backend::server::rep {
namespace {

// Field mask bits of an input entry (client reader at image RVA 0x2A25D90).
constexpr std::uint32_t kEntryFlags = 0x01;
constexpr std::uint32_t kEntryByte8 = 0x02;
constexpr std::uint32_t kEntryByteC = 0x04;
constexpr std::uint32_t kEntryByte28 = 0x08;
constexpr std::uint32_t kEntryWord2C = 0x10;
constexpr std::uint32_t kEntryStepTime = 0x20;
constexpr std::uint32_t kEntryLook = 0x40;
constexpr std::uint32_t kEntryCamera = 0x80;
/** A block of the entry (entry +0x140, client reader at image RVA 0x35EBC30). Its layout is not known. */
constexpr std::uint32_t kEntryBlock140 = 0x100;
/** The first mask bit of the fields after the block. The later fields have no fixed bit. */
constexpr unsigned kFirstTailBit = 9;
// Flags of an entry that add a field before the position of the character (client reader at
// image RVA 0x2A25D90).
constexpr std::uint64_t kFlagActionReport = 1ULL << 0;
constexpr std::uint64_t kFlagPositionPair = 1ULL << 8;
constexpr std::uint64_t kFlagPosition360 = 1ULL << 11;
constexpr std::uint64_t kFlagValue3F8 = 1ULL << 14;
/** Bytes of the target data after its kind: one byte, a world position, and three 16-bit values. */
constexpr std::size_t kTargetDataSize = 17;
/** Height range of a world position on the wire (client reader at image RVA 0x2A19B20). */
constexpr float kWorldHeightMin = -100.0F;
constexpr float kWorldHeightMax = 1000.0F;

// Bits of the action words of an entry. A word has 32 bits, and bit 0 is the lowest bit of the
// first word. The table of the client that gives each action its bit is not known. These
// numbers come from captures: each of the three inputs set the same bits in each run, together
// with other bits (the forward key: 43, 416, 542; the wheel: 237, 392, 394, 530 and 103, 219,
// 495, 510).
constexpr unsigned kActionWordBits = 32;
/** The key that moves the character forward (W). The bit stays while the key is down. */
constexpr unsigned kActionForward = 14;
/** One step of the pointer wheel that moves the camera nearer. The bit stays for one input. */
constexpr unsigned kActionWheelNearer = 27;
/** One step of the pointer wheel that moves the camera away. */
constexpr unsigned kActionWheelAway = 51;

/** @return True when the two action words that start at word `firstWord` have the bit of an action. */
bool has_action(std::uint64_t words, unsigned firstWord, unsigned action) {
    const unsigned firstBit = firstWord * kActionWordBits;
    return action >= firstBit && action < firstBit + 2 * kActionWordBits && ((words >> (action - firstBit)) & 1) != 0;
}

/** Reads the packet header. The backend uses none of its fields. */
void skip_header(hub::Reader& reader) {
    reader.raw(4);
    reader.size64();
    reader.size64();
    reader.size64();
    reader.u8();
    if (reader.u8() != 0) {
        reader.size64();
    }
    reader.u64();
    reader.u8();
    reader.size64();
    reader.size64();
    reader.size64();
    reader.u8();
    const std::uint64_t pairs = reader.size64();
    for (std::uint64_t index = 0; index < pairs && reader.ok(); ++index) {
        reader.size64();
        reader.size64();
    }
    reader.size64();
    reader.size64();
    reader.size64();
    reader.size64();
}

/** Reads a world position: x and y as 32-bit floats, the height as 16 bits of its range. */
void skip_world_position(hub::Reader& reader) {
    reader.raw(10);
}

/**
 * Reads the entry from the end of the camera position to the position of the character. A
 * stop is not an error: the entry then has no position of the character.
 */
void read_tail(hub::Reader& reader, std::uint32_t mask, ClientInput& input) {
    const auto has = [mask](unsigned bit) { return (mask & (1U << bit)) != 0; };
    input.targetKind = reader.u8();
    if (input.targetKind != kInputTargetNone) {
        reader.raw(kTargetDataSize);
    }
    if ((mask & kEntryBlock140) != 0) {
        input.tail = InputTail::unknownBlock;
        return;
    }
    if ((input.flags & kFlagActionReport) != 0) {
        reader.size64();
        reader.size64();
        reader.size64();
    }
    unsigned bit = kFirstTailBit;
    if ((input.flags & kFlagPosition360) != 0) {
        if (has(bit)) {
            skip_world_position(reader);
        }
        ++bit;
    }
    if ((input.flags & kFlagPositionPair) != 0) {
        if (has(bit)) {
            skip_world_position(reader);
        }
        if (has(bit + 1)) {
            reader.raw(16);
            reader.u64();
        }
        bit += 2;
    }
    // Entry +0x380. A value that is not 0 adds a world position and a block.
    if (has(bit) && reader.u8() != 0) {
        input.tail = InputTail::unknownBlock;
        return;
    }
    ++bit;
    if ((input.flags & kFlagValue3F8) != 0) {
        if (has(bit)) {
            reader.raw(8);
        }
        ++bit;
    }
    for (int list = 0; list < 3; ++list) {
        if (reader.size64() != 0) {
            input.tail = InputTail::unknownBlock;
            return;
        }
    }
    if (!reader.ok()) {
        input.tail = InputTail::shortBody;
        return;
    }
    if ((input.flags & kInputFlagPosition) == 0 || !has(bit)) {
        return;
    }
    const float x = reader.f32();
    const float y = reader.f32();
    const std::uint16_t height = reader.u16();
    if (!reader.ok()) {
        input.tail = InputTail::shortBody;
        return;
    }
    input.characterX = x;
    input.characterY = y;
    input.characterHeight = height;
    input.characterZ = kWorldHeightMin + static_cast<float>(height) / 65535.0F * (kWorldHeightMax - kWorldHeightMin);
    input.hasCharacter = true;
}

} // namespace

std::optional<ClientInput> read_client_input(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    reader.u64(); // Component id.
    reader.u64(); // Entity id.
    skip_header(reader);
    if (reader.u8() == 0 || !reader.ok()) {
        return std::nullopt;
    }
    const auto maskBytes = reader.raw(3);
    if (maskBytes.empty()) {
        return std::nullopt;
    }
    const std::uint32_t mask = maskBytes[0] | (static_cast<std::uint32_t>(maskBytes[1]) << 8)
                               | (static_cast<std::uint32_t>(maskBytes[2]) << 16);
    ClientInput input;
    input.sequence = reader.u16();
    if ((mask & kEntryFlags) != 0) {
        input.flags = reader.size64();
    }
    if ((mask & kEntryByte8) != 0) {
        reader.u8();
    }
    if ((mask & kEntryByteC) != 0) {
        reader.u8();
    }
    if ((mask & kEntryByte28) != 0) {
        reader.u8();
    }
    if ((mask & kEntryWord2C) != 0) {
        reader.u16();
    }
    // Action words: they are in pairs (one value, the first word in its low half), and one
    // word is alone when the count is odd.
    const unsigned actionWords = reader.u8();
    for (unsigned index = 0; index < actionWords && reader.ok(); index += 2) {
        const std::uint64_t words = reader.size64();
        input.forward = input.forward || has_action(words, index, kActionForward);
        input.wheel = input.wheel || has_action(words, index, kActionWheelNearer)
                      || has_action(words, index, kActionWheelAway);
    }
    for (float& axis : input.axes) {
        axis = static_cast<float>(reader.u8()) / 256.0F * 2.0F - 1.0F;
    }
    if ((mask & kEntryStepTime) != 0) {
        reader.f32();
    }
    if ((mask & kEntryLook) != 0) {
        input.lookYaw = reader.f32();
        input.lookPitch = reader.f32();
        input.hasLook = true;
    }
    if ((mask & kEntryCamera) != 0) {
        input.cameraX = reader.f32();
        input.cameraY = reader.f32();
        input.cameraZ = kWorldHeightMin
                        + static_cast<float>(reader.u16()) / 65535.0F * (kWorldHeightMax - kWorldHeightMin);
        input.hasCamera = true;
    }
    if (!reader.ok()) {
        return std::nullopt;
    }
    read_tail(reader, mask, input);
    return input;
}

} // namespace soulwarden::backend::server::rep
