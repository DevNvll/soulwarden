#include "server/rep/time_messages.h"

namespace soulwarden::backend::server::rep {
namespace {

/** The fields a and b of a record: the defaults of the client (0x40718B0). No reader of them is known. */
constexpr float kUnknownField = -1.0F;

} // namespace

hub::Bytes encode_time_and_phase(std::uint64_t group, const world::TimeRecord& record, world::DayPhase phase) {
    hub::Writer body;
    body.u64(time_target(group));
    body.u64(group);
    body.f32(kUnknownField);
    body.f32(kUnknownField);
    body.f32(record.offset);
    body.u32(record.cycle.dawnTime);
    body.u32(record.cycle.dawnDuration);
    body.u32(record.cycle.duskTime);
    body.u32(record.cycle.duskDuration);
    body.u32(record.cycle.dayLength);
    body.u32(record.cycle.nightLength);
    body.u64(record.anchor);
    for (const float start : record.starts) {
        body.f32(start);
    }
    body.u32(static_cast<std::uint32_t>(phase));
    return body.data();
}

hub::Bytes encode_day_phase(std::uint64_t group, world::DayPhase phase) {
    hub::Writer body;
    body.u64(time_target(group));
    body.u64(group);
    body.u32(static_cast<std::uint32_t>(phase));
    return body.data();
}

hub::Bytes encode_clock_sync(std::uint64_t serverNs) {
    hub::Writer body;
    body.u64(serverNs);
    return body.data();
}

} // namespace soulwarden::backend::server::rep
