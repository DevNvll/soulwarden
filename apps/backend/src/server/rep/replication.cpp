#include "server/rep/replication.h"

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroupsPerMask = 8;
constexpr unsigned kFieldsPerMask = 7;
constexpr std::uint8_t kMoreFieldMasks = 0x80;

} // namespace

hub::Bytes StateWriter::finish() const {
    hub::Writer out;
    for (std::size_t first = 0; first < m_groups.size(); first += kGroupsPerMask) {
        std::uint8_t groupMask = 0;
        for (unsigned bit = 0; bit < kGroupsPerMask && first + bit < m_groups.size(); ++bit) {
            if (!m_groups[first + bit].empty()) {
                groupMask |= static_cast<std::uint8_t>(1U << bit);
            }
        }
        out.u8(groupMask);
        for (unsigned bit = 0; bit < kGroupsPerMask && first + bit < m_groups.size(); ++bit) {
            const auto& fields = m_groups[first + bit];
            if (fields.empty()) {
                continue;
            }
            if (m_mask == FieldMask::wide) {
                std::uint64_t fieldMask = 0;
                for (const auto& [index, value] : fields) {
                    fieldMask |= 1ULL << index;
                }
                out.size64(fieldMask);
                for (const auto& [index, value] : fields) {
                    out.raw(value.data());
                }
                continue;
            }
            const unsigned lastMask = fields.rbegin()->first / kFieldsPerMask;
            for (unsigned mask = 0; mask <= lastMask; ++mask) {
                std::uint8_t fieldMask = mask < lastMask ? kMoreFieldMasks : 0;
                for (unsigned index = 0; index < kFieldsPerMask; ++index) {
                    if (fields.count(mask * kFieldsPerMask + index) != 0) {
                        fieldMask |= static_cast<std::uint8_t>(1U << index);
                    }
                }
                out.u8(fieldMask);
                for (unsigned index = 0; index < kFieldsPerMask; ++index) {
                    const auto found = fields.find(mask * kFieldsPerMask + index);
                    if (found != fields.end()) {
                        out.raw(found->second.data());
                    }
                }
            }
        }
    }
    return out.data();
}

void RecordWriter::fragment(std::uint32_t slot, std::uint32_t type, std::span<const std::uint8_t> state) {
    m_fragments.size(slot);
    m_fragments.size(type);
    m_fragments.raw(state);
    ++m_count;
}

hub::Bytes RecordWriter::finish() const {
    hub::Writer out;
    out.size(m_interest);
    out.u8(m_count);
    out.raw(m_fragments.data());
    return out.data();
}

} // namespace soulwarden::backend::server::rep
