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
// Stage 1.8c (research quest-engine.md Q3, map.md 2.1; live checks POI1 and the captures of stage 1.8c):
//   2297 TerritoryDetectorComponentServerFacet_AttemptTerritoryUnlock (writer 0x29C0E70, reader 0x29C0F20): reference (actor:16 raw,
//        group:u64, local:u64) territory:u16 achievement:u32 (CRC-32 of the lower-case achievement name; 0 for none). The client sends it
//        when its player goes into the shape of a point of interest (the "charted" achievement) or into the sphere of DiscoveryRadius
//        around its placement (the "discovered" achievement): live check POI1 (40142: d66d9b33 at 150 m from the placement, 39eec8c7 at
//        the shape). Component TerritoryDetector of RootPlayer.
//   6416 TerritoryDetectorComponentServerFacet_AttemptTerritoryExit (writer 0x29C2440): the same with no achievement. The client sends
//        it when its player leaves a shape or a sphere (live check POI1: two 6416 for 40140, at its shape and at 150 m).
//   2617 ObjectivesComponentServerFacet_NotifyOpenUiScreen (sender 0x3A055A0, caller 0x3A50F50): screen:u32 (CRC-32 of the lower-case UI
//        state name: a1126b78 "hud" in the captures of stage 1.8c).
//   3836 ObjectivesComponentServerFacet_AbandonSingleObjective (sender 0x3A04130, reader 0x39E8A10): instance:u64.
//   5600 ObjectivesComponentServerFacet_ResetObjective (handler 0x3A62210, reader 0x39E8A10): instance:u64.
// Stage 1.8b (research quest-engine.md Q3, conversations.md "Control messages"; the live checks of stage 1.8b):
//   3772 ConversationComponentServerFacet_RequestAcceptObjective (sender 0x3722700, caller 0x376DFF0, reader 0x26FEC60): objective:u32 (the
//        CRC-32 of the lower-case ObjectiveID). The Conversation facet of RootPlayer. No answer: the Objectives state gives the result.
//   794  ConversationComponentServerFacet_RequestCompleteObjective (sender 0x37228F0, caller 0x376E2C0): objective:u32. Live check P1: the
//        client sends it at "Complete the Quest!" of the dialogue of the ready quest (e4d66020 for 06_getoffbeach).
//   6703 (see above): the server answers 6707 when a task of the character waits for the cutscene (live check C1).
//   6707 PlayerCutsceneComponentClientFacet_OnServerCutsceneRequestGranted (server to client, handler 0x2EFE880): cutscene:u32. Live check
//        C1: the client then plays the cutscene and sends 6706.
//   6706 PlayerCutsceneComponentServerFacet_OnClientCutsceneStarted (sender 0x2EDA550, caller 0x2EF13F0): cutscene:u32.
//   6606 PlayerCutsceneComponentServerFacet_NotifyCinematicStarted (sender 0x2EDA180) and 6542 ..._NotifyCinematicEnded (sender
//        0x2ED9F90): cinematic:u32 (the CRC-32 of the lower-case name; no live check).
//   5070 WhisperPlayerComponentClientFacet_RequestStartWhisper (server to client, reader 0x4304980): whisper:u32 (the CRC-32 of the
//        lower-case WhisperId). The Whisper component of the entity "Whisper" of the player slice. Live check W1: the client plays the
//        whisper and shows its subtitles.

namespace soulwarden::backend::server::rep {

inline constexpr std::uint32_t kTypeObjectiveAutoPinning = 2709;
inline constexpr std::uint32_t kTypeUpdateTrackedObjectives = 1458;
inline constexpr std::uint32_t kTypeRequestStartCutscene = 6703;
inline constexpr std::uint32_t kTypeTerritoryEntered = 2297;
inline constexpr std::uint32_t kTypeTerritoryLeft = 6416;
inline constexpr std::uint32_t kTypeUiScreenOpened = 2617;
inline constexpr std::uint32_t kTypeAbandonObjective = 3836;
inline constexpr std::uint32_t kTypeAcceptObjective = 3772;
inline constexpr std::uint32_t kTypeCompleteObjective = 794;
inline constexpr std::uint32_t kTypeCutsceneGranted = 6707;
inline constexpr std::uint32_t kTypeCutsceneStarted = 6706;
inline constexpr std::uint32_t kTypeCinematicStarted = 6606;
inline constexpr std::uint32_t kTypeCinematicEnded = 6542;
inline constexpr std::uint32_t kTypeStartWhisper = 5070;
inline constexpr std::uint32_t kTypeResetObjective = 5600;
/** The TerritoryDetector component of the player slice (entity RootPlayer), by its name in slice-ids.json (live log of stage 1.8c). */
inline constexpr const char* kTerritoryDetectorComponentName = "RootPlayer/efc5d83c-a790-6a1f-f6f2-150e05d4d093";

/** The Objectives component (entity RootPlayer) and the PlayerCutscene component of the player slice, by their names in slice-ids.json. */
inline constexpr const char* kObjectivesComponentName = "RootPlayer/a42a47db-4f14-41c6-81d1-19a95ba99f84";
inline constexpr const char* kPlayerCutsceneComponentName = "RootPlayer/ea877e94-f1e7-4e27-8d90-f80c0938656d";
/** The Whisper component of the entity "Whisper" of the player slice (the name of the tool slice_ids.py; research chat.md). */
inline constexpr const char* kWhisperComponentName = "Whisper/9d2f173a-ae75-4a1e-8f6f-d676c587f313";

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
/** @return The body of 6703, or no value. Stage 1.8b: the same form (one u32 after the call) for 3772, 794, 6706, 6606, and 6542. */
[[nodiscard]] std::optional<CutsceneRequest> read_cutscene_request(std::span<const std::uint8_t> body);
/** @return The body of 6707 for the client of an entity group id G (target: the PlayerCutscene component plus G). */
[[nodiscard]] std::vector<std::uint8_t> cutscene_granted_body(std::uint64_t entity, std::uint32_t cutscene);
/** @return The body of 5070 (`target`: the facet target of the Whisper component of the character). */
[[nodiscard]] std::vector<std::uint8_t> whisper_body(std::uint64_t target, std::uint64_t entity, std::uint32_t whisper);

struct TerritoryReport {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::uint16_t territory{0};
    /** The CRC-32 of the achievement of 2297 (0 for none, and for 6416). */
    std::uint32_t achievement{0};
};
/** @return The body of 2297 (`entered`) or of 6416, or no value. */
[[nodiscard]] std::optional<TerritoryReport> read_territory_report(std::span<const std::uint8_t> body, bool entered);

/** The body of 2617 (`value` is the screen CRC), 3836 and 5600 (`value` is the instance). */
struct ObjectiveCall {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::uint64_t value{0};
};
/** @return The body, or no value. `wide`: an instance (u64); else a screen CRC (u32). */
[[nodiscard]] std::optional<ObjectiveCall> read_objective_call(std::span<const std::uint8_t> body, bool wide);

} // namespace soulwarden::backend::server::rep
