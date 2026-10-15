#include "support/cases.h"

namespace soulwarden::backend::tests {

std::vector<Case> session_cases();
std::vector<Case> session_end_cases();
std::vector<Case> world_cases();
std::vector<Case> world_model_cases();
std::vector<Case> inventory_cases();
std::vector<Case> equipment_cases();
std::vector<Case> review_cases();
std::vector<Case> review_inventory_cases();
std::vector<Case> progress_cases();
std::vector<Case> skill_cases();
std::vector<Case> review_progress_cases();
std::vector<Case> admin_cases();
std::vector<Case> position_cases();
std::vector<Case> creature_cases();
std::vector<Case> combat_cases();
std::vector<Case> world_spawn_cases();
std::vector<Case> fight_cases();
std::vector<Case> navigation_cases();
std::vector<Case> review_fight_cases();
std::vector<Case> review_path_cases();
std::vector<Case> review_corrections_cases();
std::vector<Case> patrol_cases();
std::vector<Case> review_patrol_cases();
std::vector<Case> loot_cases();
std::vector<Case> ability_cases();
std::vector<Case> review_loot_cases();
std::vector<Case> review_scope_cases();
std::vector<Case> itemization_cases();
std::vector<Case> review_itemization_cases();
std::vector<Case> status_cases();
std::vector<Case> review_status_cases();
std::vector<Case> spell_cases();
std::vector<Case> motion_cases();
std::vector<Case> review_spell_cases();
std::vector<Case> review_motion_cases();
std::vector<Case> projectile_cases();
std::vector<Case> review_projectile_cases();
std::vector<Case> spell_kind_cases();
std::vector<Case> review_spells2_cases();
std::vector<Case> ownership_cases();
std::vector<Case> review_dlc_cases();
std::vector<Case> crit_cases();
std::vector<Case> passive_cases();

std::vector<Case> all_cases() {
    std::vector<Case> cases;
    for (const auto& group : {session_cases(), session_end_cases(), world_cases(), world_model_cases(), inventory_cases(),
                              equipment_cases(), review_cases(), review_inventory_cases(), progress_cases(), skill_cases(),
                              review_progress_cases(), admin_cases(), position_cases(), creature_cases(), combat_cases(),
                              world_spawn_cases(), fight_cases(), navigation_cases(), review_fight_cases(), review_path_cases(),
                              review_corrections_cases(), patrol_cases(), review_patrol_cases(), loot_cases(), ability_cases(), review_loot_cases(), review_scope_cases(), itemization_cases(),
                              review_itemization_cases(), status_cases(), review_status_cases(), spell_cases(), motion_cases(), review_spell_cases(),
                              review_motion_cases(), projectile_cases(), review_projectile_cases(), spell_kind_cases(), review_spells2_cases(),
                              ownership_cases(), review_dlc_cases(), crit_cases(), passive_cases()}) {
        cases.insert(cases.end(), group.begin(), group.end());
    }
    return cases;
}

} // namespace soulwarden::backend::tests
