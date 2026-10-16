#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The stamina rules of a player and the dodge (stage 3.4f-B, docs/systems/stamina-and-dodge.md): the Stamina component of the player
// slice (the maximum, the regeneration rate, the winded time), the delays of the base vitals, the stamina factors of each load class,
// the dodge action (the fragment and the cost of each load class, the delay at its exit), the times of the dodge fragments (the
// payment, the immune window, the mark CanTransition, the windows of a second dodge), and the exhausted state.
//
// The data has the values of the game files and no rule: the rules are in the world (world/stamina.h). The data is game data: the tool
// tools/gamedata/stamina.sh makes the file from the game copy of the user, and the file stays outside the repository. Without the file
// the backend starts: a character keeps its full stamina, and a dodge has no immune window.

namespace soulwarden::backend::gamedata {

/** File name of the stamina data in the game data directory of the backend. */
inline constexpr const char* kStaminaDataFile = "stamina-data.json";

/** The load classes of a character (the equip load: light is Fast, medium Normal, heavy Slow in the data of the client). */
enum class LoadClass : std::uint8_t { light, medium, heavy, overburdened };
inline constexpr std::size_t kLoadClassCount = 4;

/** One window of the transition CanDodgeCancel of a dodge fragment: a second dodge in it (seconds of the fragment clock). */
struct DodgeCancel {
    float start{0.0F};
    /** The end of the window; no value: to the end of the fragment. */
    std::optional<float> end;
    /** True: a press in the window starts the next dodge at the end of the window (CAGE-TransitionBuffered); false: at once. */
    bool buffered{false};
};

/** The times of a dodge fragment, from the start of the fragment (seconds; the standing variants of player.adb). */
struct DodgeFragment {
    std::string name;
    /** The mark PayStaminaCost: the cost is paid here. */
    float pay{0.0F};
    /** The clip Invulnerability: a hit in [start, end) does not hurt the character. */
    float immuneStart{0.0F};
    float immuneEnd{0.0F};
    /** The mark CanTransition: the dodge action stops the regeneration before it (DisableStaminaRegen) and sets the delay at it. */
    float canTransition{0.0F};
    std::vector<DodgeCancel> cancel;
};

/** The dodge of one load class: its fragment and its cost (the cost id of PayStaminaCost and its row in StaminaCosts_Player). */
struct DodgeClass {
    std::string fragment;
    std::string costId;
    float cost{0.0F};
};

struct StaminaTables {
    /** The Stamina component of the player slice: m_min, m_initMax, m_regenRate (stamina in one second), m_maxWindedTime. */
    float minimum{0.0F};
    float maximum{100.0F};
    float regenRate{0.0F};
    float blockRegenRate{0.0F};
    float maxWindedTime{0.0F};
    /** BaseVitals_Player: the delay of the regeneration after a payment, and after a payment that empties the stamina (seconds). */
    float regenDelay{0.0F};
    float regenDelayOnEmpty{0.0F};
    float fullyDepletedDelay{0.0F};
    /** EncumbranceLimits, row Player: the factor of a stamina cost and of the regeneration for each load class (LoadClass order). */
    std::array<float, kLoadClassCount> costMult{1.0F, 1.0F, 1.0F, 1.0F};
    std::array<float, kLoadClassCount> regenMult{1.0F, 1.0F, 1.0F, 1.0F};
    /**
     * The dodge: the name of its action (the condition InAction of a passive rule), the delay that its exit sets (SetStaminaRegenDelay
     * OnExit), the class of each load (light, medium, heavy).
     */
    std::string dodgeAction;
    float dodgeExitDelay{0.0F};
    std::array<DodgeClass, 3> dodge{};
    std::vector<DodgeFragment> fragments;
    /** The conditional costs of the dodge that need a rule of a weapon (a passive of the hatchet, the spear): not applied. */
    std::size_t costsNotApplied{0};
    /** The exhausted state: it starts at this part of the maximum (0) and ends at this part (1). */
    float exhaustedEnter{0.0F};
    float exhaustedExit{1.0F};
};

class StaminaData {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a member of a wrong form, a number that is not
     * finite or out of its range, a class whose fragment is not in the file, times of a fragment that do not fit together.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** Sets the data with no file (tests), with the same check. @return False, with no data, when it is not correct. */
    [[nodiscard]] bool set(StaminaTables tables);

    [[nodiscard]] bool loaded() const {
        return m_loaded;
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const StaminaTables& tables() const {
        return m_tables;
    }
    /** @return The dodge of a load class (an overburdened character has none: the action of the client refuses it), or null. */
    [[nodiscard]] const DodgeClass* dodge_of(LoadClass load) const;
    /** @return The fragment of a dodge class, or null. */
    [[nodiscard]] const DodgeFragment* fragment_of(const DodgeClass& dodge) const;

private:
    StaminaTables m_tables;
    bool m_loaded{false};
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata
