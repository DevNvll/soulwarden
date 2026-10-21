#include "server/rep/quest_messages.h"

#include "server/hub/wire.h"

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

} // namespace soulwarden::backend::server::rep
