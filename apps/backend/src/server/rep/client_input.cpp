#include "server/rep/client_input.h"

#include <algorithm>

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
/** An object of the entry (entry +0x140, client reader at image RVA 0x35EBC30). Its meaning is not known. */
constexpr std::uint32_t kEntryObject140 = 0x100;
/** The first mask bit of the fields after the block. The later fields have no fixed bit. */
constexpr unsigned kFirstTailBit = 9;
// Flags of an entry that add a field before the position of the character (client reader at
// image RVA 0x2A25D90).
constexpr std::uint64_t kFlagActionReport = 1ULL << 0;
constexpr std::uint64_t kFlagPositionPair = 1ULL << 8;
constexpr std::uint64_t kFlagPosition360 = 1ULL << 11;
constexpr std::uint64_t kFlagValue3F8 = 1ULL << 14;
// Flags of an entry that add a field after the position of the character.
constexpr std::uint64_t kFlagValue468 = 1ULL << 16;
constexpr std::uint64_t kFlagValue35C = 1ULL << 18;
/** Bytes of one item of each of the three lists of an entry (client readers 0x2A1A310, 0x2A1A0D0, 0x2A19E60). */
constexpr std::size_t kProjectileHitSize = 104;
constexpr std::size_t kShotSize = 192;
constexpr std::size_t kContactSize = 96;
/** Bytes of the target data after its kind: one byte, a world position, and three 16-bit values. */
constexpr std::size_t kTargetDataSize = 17;
/** Height range of a world position on the wire (client reader at image RVA 0x2A19B20). */
constexpr float kWorldHeightMin = -100.0F;
constexpr float kWorldHeightMax = 1000.0F;

// Bits of the action words of an entry. A word has 32 bits, and bit 0 is the lowest bit of the
// first word. The bit of an action is its position in the list of the action names of the
// input profile of the client (596 names of the default profile and its includes, in the
// sequence of their CRC-32 values; protocol notes 4.7). The numbers are a fact of the
// supported client build. Each one has live proof from captures: an input sets its bit
// together with the bits of other names of the same key (the forward key: 43, 416, 542; the
// wheel: 237, 392, 394, 530 and 103, 219, 495, 510).
constexpr unsigned kActionWordBits = 32;
/** The key that moves the character forward (W). The bit stays while the key is down. */
constexpr unsigned kActionForward = 14;
/** One step of the pointer wheel that moves the camera nearer. The bit stays for one input. */
constexpr unsigned kActionWheelNearer = 27;
/** One step of the pointer wheel that moves the camera away. */
constexpr unsigned kActionWheelAway = 51;
/**
 * The primary attack ("attack_primary", "attack_primary_hold", "attack_primary_release": the
 * left pointer button). The first bit stays for one input, the second one while the button
 * is down, and the third one for one input.
 */
constexpr unsigned kActionAttack = 555;
constexpr unsigned kActionAttackHold = 191;
constexpr unsigned kActionAttackRelease = 567;
/** The keys of the two weapons of the paperdoll ("Use_MainHandSlot_1", "Use_MainHandSlot_2"). */
constexpr unsigned kActionWeapon1 = 9;
constexpr unsigned kActionWeapon2 = 355;
/**
 * The press of the three keys of the ability bar ("ability1", "ability2", "ability3"; Q, R, F of
 * the default profile). Live proof of stage 3.3a (L1): the bit is in the first entry of a press,
 * with the bit "abilityN_virtual"; the next entries have the hold bit and then the release bit.
 */
constexpr unsigned kActionAbility[3] = {402, 115, 139};
/**
 * The keys of the four quick slots ("Use_Quickslot_1" to "Use_Quickslot_4"; 3 to 6 of the default profile) and of the
 * heart rune ("rune_ability"; G). Live proof of stage 1.3b (I7, I6): one entry with the bit, and no message.
 */
constexpr unsigned kActionQuickslot[4] = {404, 117, 140, 513};
constexpr unsigned kActionRune = 450;
/** The aim input ("aim_draw"): bit 126 (research abilities-ranged.md Q2; live check L2 of stage 3.3b-S3). */
constexpr unsigned kActionAim = 126;
/**
 * The self cast input ("self_target": bit 430 of the table of the research, combat-actions input-bits.json; LCTRL of the default
 * profile). Live check of stage 3.3b-S6: the key LCTRL in the aim of Divine Embrace gives the bits 1, 57, 313, 455, 546, and 430 in the
 * second entry; the client then plays the self cast (its cast bar of 1.0 s).
 */
constexpr unsigned kActionSelfTarget = 430;
/** The dodge input ("dodge": bit 322 of the table of the research combat-actions.md; the left Shift of the default profile; stage 3.3b-M1). */
constexpr unsigned kActionDodge = 322;
/**
 * The block press ("block_press_virtual": bit 484 of the table of the research, combat-actions input-bits.json; the right pointer
 * button of the default profile; stage 3.3b-M2). The action data of the great axe uses it: the swing of Charge
 * (Ability_GreatAxe_Charge_Swing) comes on IfInput block_press_virtual.
 */
constexpr unsigned kActionBlockPress = 484;
/** Offsets in a shot item of the second list (reader 0x2A1A590; protocol notes 4.27). */
constexpr std::size_t kShotPoint = 36;
constexpr std::size_t kShotReceiver = 92;
constexpr std::size_t kShotCharge = 172;

/** The values of an entry that the next entry of the message has when it does not send them. */
struct EntryBase {
    std::uint64_t flags{0};
    /** Value of entry +0x380. A value that is not 0 adds two fields. */
    std::uint8_t value380{0};
};

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

/** Reads a flag of one byte. @return False for a value that is not 0 or 1. */
bool read_flag(hub::Reader& reader, bool& value) {
    const std::uint8_t byte = reader.u8();
    value = byte == 1;
    return byte <= 1;
}

void skip_text(hub::Reader& reader) {
    reader.raw(reader.size());
}

/**
 * Reads the object of mask bit 8 (client reader at image RVA 0x35EBC30). The backend uses none
 * of its fields. @return False for a flag that is not 0 or 1.
 */
bool skip_object140(hub::Reader& reader) {
    bool present = false;
    bool flag = false;
    if (!read_flag(reader, present)) {
        return false;
    }
    if (!present) {
        return true;
    }
    reader.raw(16 + 8 + 8);    // +0x10, +0x20, +0x28
    reader.raw(1 + 1 + 8 + 4); // +0x30, +0x31, +0x38, +0x40
    skip_text(reader);         // +0x48
    reader.raw(4 + 4);         // +0x70, +0x74
    reader.f32();              // +0x78
    if (!read_flag(reader, flag)) { // +0x80
        return false;
    }
    for (int text = 0; text < 4; ++text) { // +0x88, +0xB0, +0xD8, +0x100
        skip_text(reader);
    }
    reader.f32(); // +0x7C
    bool vectors = false;
    if (!read_flag(reader, vectors)) { // +0x81
        return false;
    }
    if (vectors) {
        reader.raw(12 + 12); // +0x130, +0x140
    }
    if (!read_flag(reader, flag)) { // +0x150
        return false;
    }
    reader.raw(4 + 1 + 4); // +0x154, +0x158, +0x15C
    reader.raw(4 * 4);     // +0x128, +0x44, +0x160, +0x164
    reader.size64();       // +0x168
    reader.u32();          // +0x170
    reader.size();         // +0x1A0
    skip_text(reader);     // +0x178
    reader.raw(4 + 1);     // +0x1A4, +0x1A8
    if (!read_flag(reader, flag)) { // +0x1A9
        return false;
    }
    reader.u32();      // +0x1E8
    skip_text(reader); // +0x1C0
    reader.raw(4 + 8); // +0x1F0, +0x1F8
    reader.u32();      // +0x200
    return read_flag(reader, flag); // +0x204
}

std::array<float, 3> read_vector(hub::Reader& reader) {
    std::array<float, 3> vector{};
    for (float& value : vector) {
        value = reader.f32();
    }
    return vector;
}

/** Reads one melee contact (client reader at image RVA 0x2A18490). */
InputContact read_contact(hub::Reader& reader) {
    InputContact contact;
    contact.provider = reader.u32();
    const auto groupActor = reader.raw(contact.groupActor.size());
    contact.group = reader.u64();
    const auto actor = reader.raw(contact.actor.size());
    contact.entity = reader.u64();
    contact.facet = reader.u64();
    contact.point = read_vector(reader);
    contact.normal = read_vector(reader);
    contact.direction = read_vector(reader);
    if (reader.ok()) {
        std::copy(groupActor.begin(), groupActor.end(), contact.groupActor.begin());
        std::copy(actor.begin(), actor.end(), contact.actor.begin());
    }
    return contact;
}

/**
 * Reads the count of a list of an entry. @return False when the body has not the bytes of the
 * items: the count is then not a count of the client.
 */
bool read_count(hub::Reader& reader, std::size_t itemSize, std::uint32_t& count) {
    count = reader.size();
    return reader.ok() && count <= reader.left() / itemSize;
}

/**
 * Reads the entry from the end of the camera position to its end. A stop is not an error: the
 * entry then has no position of the character and no attack data.
 */
void read_tail(hub::Reader& reader, std::uint32_t mask, EntryBase& base, ClientInput& input, InputStep& step) {
    const auto has = [mask](unsigned bit) { return (mask & (1U << bit)) != 0; };
    input.targetKind = reader.u8();
    if (input.targetKind != kInputTargetNone) {
        reader.raw(kTargetDataSize);
    }
    if ((mask & kEntryObject140) != 0 && !skip_object140(reader)) {
        input.tail = reader.ok() ? InputTail::badValue : InputTail::shortBody;
        return;
    }
    if ((input.flags & kFlagActionReport) != 0) {
        reader.size64();
        reader.size64();
        reader.size64();
    }
    // From here each field takes the next mask bit, and a field of a flag takes its bit only
    // when the entry has the flag.
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
    // Entry +0x380. A value that is not 0 adds a world position and a reference.
    if (has(bit)) {
        base.value380 = reader.u8();
    }
    ++bit;
    if (base.value380 != 0) {
        if (has(bit)) {
            skip_world_position(reader);
        }
        if (has(bit + 1)) {
            reader.raw(32);
        }
        bit += 2;
    }
    if ((input.flags & kFlagValue3F8) != 0) {
        if (has(bit)) {
            reader.raw(8);
        }
        ++bit;
    }
    // The three lists. An entry has only its own items: it does not take them from the entry
    // before it.
    std::uint32_t contacts = 0;
    if (!read_count(reader, kProjectileHitSize, step.projectileHits)) {
        input.tail = InputTail::shortBody;
        return;
    }
    reader.raw(step.projectileHits * kProjectileHitSize);
    if (!read_count(reader, kShotSize, step.shots)) {
        input.tail = InputTail::shortBody;
        return;
    }
    for (std::uint32_t index = 0; index < step.shots; ++index) {
        const auto item = reader.raw(kShotSize);
        if (item.size() != kShotSize) {
            break;
        }
        hub::Reader fields(item);
        InputShot shot;
        shot.origin = read_vector(fields);
        shot.direction = read_vector(fields);
        read_vector(fields); // The offset of the aim (wire 24).
        shot.point = read_vector(fields);
        fields.raw(kShotReceiver - kShotPoint - 12);
        const auto actor = fields.raw(shot.receiverActor.size());
        shot.receiver = fields.u64();
        shot.facet = fields.u64();
        fields.raw(kShotCharge - kShotReceiver - 32);
        shot.charge = fields.f32();
        shot.ammo = fields.u32();
        shot.attack = fields.u32();
        shot.row = fields.u32();
        if (!fields.ok()) {
            break;
        }
        std::copy(actor.begin(), actor.end(), shot.receiverActor.begin());
        // A receiver with no actor id is no receiver (a miss of the query near the muzzle: the actor id 0, G 0xFFFFFFFF).
        shot.hasReceiver = std::any_of(shot.receiverActor.begin(), shot.receiverActor.end(), [](std::uint8_t byte) { return byte != 0; });
        step.shotItems.push_back(shot);
    }
    if (!read_count(reader, kContactSize, contacts)) {
        input.tail = InputTail::shortBody;
        return;
    }
    step.contacts.reserve(contacts);
    for (std::uint32_t index = 0; index < contacts; ++index) {
        step.contacts.push_back(read_contact(reader));
    }
    if ((input.flags & kInputFlagPosition) != 0) {
        if (has(bit)) {
            const float x = reader.f32();
            const float y = reader.f32();
            const std::uint16_t height = reader.u16();
            if (reader.ok()) {
                input.characterX = x;
                input.characterY = y;
                input.characterHeight = height;
                input.characterZ =
                    kWorldHeightMin + static_cast<float>(height) / 65535.0F * (kWorldHeightMax - kWorldHeightMin);
                input.hasCharacter = true;
            }
        }
        ++bit;
    }
    // The velocity of the character (three 16-bit floats), and the values of entry +0x468,
    // +0x35C, +0x470, +0x474, and +0x478. The backend uses none of them.
    if (has(bit)) {
        reader.raw(6);
    }
    ++bit;
    if ((input.flags & kFlagValue468) != 0) {
        reader.u64();
    }
    if ((input.flags & kFlagValue35C) != 0) {
        reader.u8();
    }
    if (has(bit)) {
        reader.u8();
    }
    if (has(bit + 1)) {
        reader.u32();
    }
    if ((input.flags & kFlagActionReport) != 0 && has(bit + 2)) {
        reader.u16();
    }
    if (!reader.ok()) {
        // The body ends before the end of the entry: the entry is not complete, and a part of
        // it is not a position. The position field was read before the missing bytes.
        input.tail = InputTail::shortBody;
        input.hasCharacter = false;
    }
}

/**
 * Reads one entry. @param base The values of the entry before it in the message: the entry
 * replaces the ones that it sends.
 * @return Nothing when the body does not parse to the camera position of the entry.
 */
std::optional<ClientInput> read_entry(hub::Reader& reader, EntryBase& base, InputStep& step) {
    const auto maskBytes = reader.raw(3);
    if (maskBytes.empty()) {
        return std::nullopt;
    }
    const std::uint32_t mask = maskBytes[0] | (static_cast<std::uint32_t>(maskBytes[1]) << 8)
                               | (static_cast<std::uint32_t>(maskBytes[2]) << 16);
    ClientInput input;
    input.sequence = reader.u16();
    step.sequence = input.sequence;
    if ((mask & kEntryFlags) != 0) {
        base.flags = reader.size64();
    }
    input.flags = base.flags;
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
    // word is alone when the count is odd. Each entry has its own words.
    const unsigned actionWords = reader.u8();
    for (unsigned index = 0; index < actionWords && reader.ok(); index += 2) {
        const std::uint64_t words = reader.size64();
        input.forward = input.forward || has_action(words, index, kActionForward);
        input.wheel = input.wheel || has_action(words, index, kActionWheelNearer)
                      || has_action(words, index, kActionWheelAway);
        step.attackPress = step.attackPress || has_action(words, index, kActionAttack);
        step.attackHold = step.attackHold || has_action(words, index, kActionAttackHold);
        step.attackRelease = step.attackRelease || has_action(words, index, kActionAttackRelease);
        if (has_action(words, index, kActionWeapon1)) {
            step.weaponKey = 1;
        } else if (has_action(words, index, kActionWeapon2)) {
            step.weaponKey = 2;
        }
        for (std::uint8_t place = 0; place < 3; ++place) {
            if (step.abilityKey == 0 && has_action(words, index, kActionAbility[place])) {
                step.abilityKey = static_cast<std::uint8_t>(place + 1);
            }
        }
        for (std::uint8_t place = 0; place < 4; ++place) {
            if (step.quickKey == 0 && has_action(words, index, kActionQuickslot[place])) {
                step.quickKey = static_cast<std::uint8_t>(place + 1);
            }
        }
        step.runeKey = step.runeKey || has_action(words, index, kActionRune);
        step.aim = step.aim || has_action(words, index, kActionAim);
        step.selfTarget = step.selfTarget || has_action(words, index, kActionSelfTarget);
        step.dodge = step.dodge || has_action(words, index, kActionDodge);
        step.blockPress = step.blockPress || has_action(words, index, kActionBlockPress);
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
    read_tail(reader, mask, base, input, step);
    return input;
}

} // namespace

std::optional<ClientInput> read_client_input(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    reader.u64(); // Component id.
    reader.u64(); // Entity id.
    skip_header(reader);
    const std::uint8_t count = reader.u8();
    if (count == 0 || !reader.ok()) {
        return std::nullopt;
    }
    EntryBase base;
    InputStep step;
    std::optional<ClientInput> newest = read_entry(reader, base, step);
    if (!newest) {
        return std::nullopt;
    }
    newest->entries = count;
    if (newest->tail != InputTail::read) {
        return newest;
    }
    newest->steps.push_back(std::move(step));
    for (unsigned index = 1; index < count; ++index) {
        InputStep older;
        const std::optional<ClientInput> entry = read_entry(reader, base, older);
        if (!entry || entry->tail != InputTail::read) {
            break;
        }
        newest->steps.push_back(std::move(older));
    }
    return newest;
}

} // namespace soulwarden::backend::server::rep
