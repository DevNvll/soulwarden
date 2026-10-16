#pragma once

// The stamina data of the cases of stage 3.4f-B (tests/ranged_b_tests.cpp): the values of the retail data (stamina-data.json of the
// supported build: the Stamina component of the player slice, the base vitals, the dodge action and its fragments), written here with no
// file of the game.

#include <optional>
#include <stdexcept>
#include <string>

#include "gamedata/stamina_data.h"

namespace soulwarden::backend::tests::stamina {

inline constexpr float kMaximum = 100.0F;
inline constexpr float kRegenRate = 33.33F;
inline constexpr float kRegenDelay = 1.0F;
inline constexpr float kWinded = 2.0F;
inline constexpr float kDodgeCost = 45.0F;
inline constexpr float kSlowDodgeCost = 50.0F;
inline constexpr float kPay = 0.1F;
inline constexpr float kImmuneEnd = 0.5F;
inline constexpr float kCanTransition = 0.5F;
inline constexpr float kBufferStart = 0.266667F;

inline gamedata::DodgeFragment dodge_fragment(const char* name) {
    gamedata::DodgeFragment fragment;
    fragment.name = name;
    fragment.pay = kPay;
    fragment.immuneStart = 0.0F;
    fragment.immuneEnd = kImmuneEnd;
    fragment.canTransition = kCanTransition;
    fragment.cancel = {{kBufferStart, 0.5F, true}, {0.5F, std::nullopt, false}};
    return fragment;
}

inline gamedata::StaminaTables stamina_tables() {
    gamedata::StaminaTables tables;
    tables.minimum = 0.0F;
    tables.maximum = kMaximum;
    tables.regenRate = kRegenRate;
    tables.blockRegenRate = 10.0F;
    tables.maxWindedTime = kWinded;
    tables.regenDelay = kRegenDelay;
    tables.regenDelayOnEmpty = kRegenDelay;
    tables.fullyDepletedDelay = 1.0F;
    tables.dodgeAction = "Dodge";
    tables.dodgeExitDelay = 1.0F;
    tables.dodge = {gamedata::DodgeClass{"Dodge_Roll_Fast", "Dodge_Roll_Fast", kDodgeCost},
                    gamedata::DodgeClass{"Dodge_Roll", "Dodge_Roll", kDodgeCost},
                    gamedata::DodgeClass{"Dodge_Roll_Slow", "Dodge_Roll_Slow", kSlowDodgeCost}};
    tables.fragments = {dodge_fragment("Dodge_Roll_Fast"), dodge_fragment("Dodge_Roll"), dodge_fragment("Dodge_Roll_Slow")};
    return tables;
}

/** Gives a host the stamina data of the cases. */
template <class Host>
void set_stamina(Host& host) {
    if (!host.stamina.set(stamina_tables())) {
        throw std::runtime_error("stamina test: the stamina data is not valid: " + host.stamina.error());
    }
}

} // namespace soulwarden::backend::tests::stamina
