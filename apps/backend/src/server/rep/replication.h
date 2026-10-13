#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "server/hub/wire.h"

// State replication from the game server to the client ("ReplicatedStateBundle" payload).
//
//   payload  := record*
//   record   := interest:size fragments:u8 fragment*     (client reader at image RVA 0x6AF20D0)
//   fragment := slot:size type:size state                (client reader at image RVA 0x6AF2340)
//   state    := groupMask:u8 (one byte for each 8 groups of the type), then for each group
//               in the mask: fieldMask:u8 value* (7 fields for each mask byte; bit 7 means
//               that one more mask byte follows)        (client reader at image RVA 0x17B4110)
//
// Some state types have their own reader. The movement state of a character (ALC) has
//   state    := groupMask:u8, then for each group in the mask: fieldMask:size64 value*
//               (one bit for each field)                 (client reader at image RVA 0x2A436B0)
//
// An interest is one entity group of the client. Interest 1 is the player of the client.
// The slot of a fragment is the index of a component in the component list of the entity
// group. The first fragment of a new entity group is the metadata state (slot 0): it gives
// the slice asset and the actor id. tools/st prints the groups and fields of a state type.

namespace soulwarden::backend::server::rep {

/** How a state type writes the field mask of a group. */
enum class FieldMask {
    /** One byte for each 7 fields, with a continuation bit. The common form. */
    chained,
    /** One variable-length 64-bit number with one bit for each field. */
    wide,
};

/** Builds the wire form of one replicated state. */
class StateWriter {
public:
    /** @param groupCount Number of field groups of the state type (tools/st prints it). */
    explicit StateWriter(unsigned groupCount, FieldMask mask = FieldMask::chained)
        : m_groups(groupCount), m_mask(mask) {}

    /** @return The writer for the value of one field. Call one time for each field. */
    hub::Writer& field(unsigned group, unsigned index) {
        return m_groups.at(group)[index];
    }

    [[nodiscard]] hub::Bytes finish() const;

private:
    std::vector<std::map<unsigned, hub::Writer>> m_groups;
    FieldMask m_mask;
};

/** Builds one state record: the fragments of one interest. */
class RecordWriter {
public:
    explicit RecordWriter(std::uint32_t interest) : m_interest(interest) {}

    void fragment(std::uint32_t slot, std::uint32_t type, std::span<const std::uint8_t> state);

    [[nodiscard]] hub::Bytes finish() const;

private:
    std::uint32_t m_interest;
    std::uint8_t m_count{0};
    hub::Writer m_fragments;
};

} // namespace soulwarden::backend::server::rep
