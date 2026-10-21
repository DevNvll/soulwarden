#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The client messages of the quests (stage 1.8a; research quests.md 6.1, quest-engine.md Q3; docs/systems/quests.md). Each body starts
// with the facet target and the entity group id (protocol notes 4.2).
//
//   2709 ObjectivesComponentServerFacet_SetObjectiveAutoPinning (sender 0x3A632C0, reader 0x39EF030): count:size (36 at most), one flag
//        byte (0 or 1) for each objective type of the client enum. The client sends it in OnEnable of its objectives facet (0x3A52510):
//        the first one of a session is the sign that the facet is local and enabled (live check L1 of stage 1.8a).
//   1458 ObjectivesComponentServerFacet_UpdateTrackedObjectives (sender 0x3A05DA0, reader 0x39F1590): count:size (8 at most), instance:u64
//        each: the pinned list of the client after each change of it (live check L1: first an empty list, then the list with the pin).
//   6703 PlayerCutsceneComponentServerFacet_OnClientRequestStartCutscene (sender 0x2EDA740): cutscene:u32 (CRC-32 of the name). Stage
//        1.8a only writes it to the log (live check L2: the client sends it 2 s after the bit Start_IGC_Aelstrom_WW, CRC 4817DD08; the
//        answer is stage 1.8b).

namespace soulwarden::backend::server::rep {

inline constexpr std::uint32_t kTypeObjectiveAutoPinning = 2709;
inline constexpr std::uint32_t kTypeUpdateTrackedObjectives = 1458;
inline constexpr std::uint32_t kTypeRequestStartCutscene = 6703;

/** The Objectives component (entity RootPlayer) and the PlayerCutscene component of the player slice, by their names in slice-ids.json. */
inline constexpr const char* kObjectivesComponentName = "RootPlayer/a42a47db-4f14-41c6-81d1-19a95ba99f84";
inline constexpr const char* kPlayerCutsceneComponentName = "RootPlayer/ea877e94-f1e7-4e27-8d90-f80c0938656d";

/** The reader of 2709 takes 36 flags at most (`count < 0x25`, research quests.md 6.1). */
inline constexpr std::size_t kAutoPinFlags = 36;
/** The reader of 1458 takes 8 instances at most. */
inline constexpr std::size_t kTrackedLimit = 8;

struct AutoPinning {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::vector<bool> flags;
};
/** @return The body of 2709, or no value for a body that does not have its form (more than 36 flags, a flag that is not 0 or 1). */
[[nodiscard]] std::optional<AutoPinning> read_auto_pinning(std::span<const std::uint8_t> body);

struct TrackedObjectives {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::vector<std::uint64_t> instances;
};
/** @return The body of 1458, or no value (more than 8 instances, a body with more or fewer bytes). */
[[nodiscard]] std::optional<TrackedObjectives> read_tracked_objectives(std::span<const std::uint8_t> body);

struct CutsceneRequest {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::uint32_t cutscene{0};
};
/** @return The body of 6703, or no value. */
[[nodiscard]] std::optional<CutsceneRequest> read_cutscene_request(std::span<const std::uint8_t> body);

} // namespace soulwarden::backend::server::rep
