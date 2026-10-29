#include "server/rep/quest_messages.h"

#include "server/hub/wire.h"
#include "server/rep/lock_messages.h"

namespace soulwarden::backend::server::rep {

std::optional<AutoPinning> read_auto_pinning(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    AutoPinning message;
    message.target = reader.u64();
    message.entity = reader.u64();
    const std::uint32_t count = reader.size();
    if (!reader.ok() || count > kAutoPinFlags) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint8_t flag = reader.u8();
        if (flag > 1) {
            return std::nullopt; // The reader of the client takes a bool byte of 0 or 1 only.
        }
        message.flags.push_back(flag == 1);
    }
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::optional<TrackedObjectives> read_tracked_objectives(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    TrackedObjectives message;
    message.target = reader.u64();
    message.entity = reader.u64();
    const std::uint32_t count = reader.size();
    if (!reader.ok() || count > kTrackedLimit) {
        return std::nullopt;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        message.instances.push_back(reader.u64());
    }
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::optional<CutsceneRequest> read_cutscene_request(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    CutsceneRequest message;
    message.target = reader.u64();
    message.entity = reader.u64();
    message.cutscene = reader.u32();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::vector<std::uint8_t> cutscene_granted_body(std::uint64_t entity, std::uint32_t cutscene) {
    hub::Writer out;
    out.u64(kPlayerCutsceneComponent + entity);
    out.u64(entity);
    out.u32(cutscene);
    return out.data();
}

std::vector<std::uint8_t> whisper_body(std::uint64_t target, std::uint64_t entity, std::uint32_t whisper) {
    hub::Writer out;
    out.u64(target);
    out.u64(entity);
    out.u32(whisper);
    return out.data();
}

std::optional<TerritoryReport> read_territory_report(std::span<const std::uint8_t> body, bool entered) {
    hub::Reader reader(body);
    TerritoryReport message;
    message.target = reader.u64();
    message.entity = reader.u64();
    for (int part = 0; part < 16; ++part) {
        (void)reader.u8(); // reference.actor: a level entity of the client (the server does not need it: research map.md 2.1).
    }
    (void)reader.u64(); // reference.group
    (void)reader.u64(); // reference.local
    message.territory = reader.u16();
    if (entered) {
        message.achievement = reader.u32();
    }
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::optional<ObjectiveCall> read_objective_call(std::span<const std::uint8_t> body, bool wide) {
    hub::Reader reader(body);
    ObjectiveCall message;
    message.target = reader.u64();
    message.entity = reader.u64();
    message.value = wide ? reader.u64() : reader.u32();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

std::optional<CommunityGoalSelection> read_community_goal_selection(std::span<const std::uint8_t> body) {
    hub::Reader reader(body);
    CommunityGoalSelection message;
    message.target = reader.u64();
    message.entity = reader.u64();
    message.mission = reader.u32();
    message.destination = reader.u32();
    if (!reader.ok() || reader.left() != 0) {
        return std::nullopt;
    }
    return message;
}

} // namespace soulwarden::backend::server::rep
