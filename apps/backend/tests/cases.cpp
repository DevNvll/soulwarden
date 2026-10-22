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
std::vector<Case> review_crit_cases();
std::vector<Case> heal_cases();
std::vector<Case> attack_sequence_cases();
std::vector<Case> review_heal_cases();
std::vector<Case> review_combos_cases();
std::vector<Case> gawh_cases();
std::vector<Case> review_gawh_cases();
std::vector<Case> interaction_cases();
std::vector<Case> review_interaction_cases();
std::vector<Case> station_cases();
std::vector<Case> person_cases();
std::vector<Case> review_persons_25c_cases();
std::vector<Case> quest_person_cases();
std::vector<Case> review_wrecking_cases();
std::vector<Case> ranged_cases();
std::vector<Case> review_station_cases();
std::vector<Case> ranged_b_cases();
std::vector<Case> review_ranged_cases();
std::vector<Case> review_casters_cases();
std::vector<Case> ranged_c_cases();
std::vector<Case> review_areas_cases();
std::vector<Case> support_cases();
std::vector<Case> chest_cases();
std::vector<Case> loot_bag_cases();
std::vector<Case> mana_cases();
std::vector<Case> review_support_cases();
std::vector<Case> review_chests_cases();
std::vector<Case> world_time_cases();
std::vector<Case> review_loot_bags_cases();
std::vector<Case> review_world_time_cases();
std::vector<Case> gathering_cases();
std::vector<Case> user_test_2_cases();
std::vector<Case> review_gathering_cases();
std::vector<Case> crafting_cases();
std::vector<Case> quest_cases();
std::vector<Case> review_quests_18a_cases();
std::vector<Case> review_quests_18c_cases();
std::vector<Case> review_quests_18b_cases();
std::vector<Case> review_quests_18d_cases();
std::vector<Case> quest_closure_cases();
std::vector<Case> review_quests_18e_cases();
std::vector<Case> behavior_tree_cases();
std::vector<Case> quest_reward_cases();
std::vector<Case> quest_event_cases();
std::vector<Case> review_b4_cases();
std::vector<Case> build_preset_cases();
std::vector<Case> review_user_test_2_cases();
std::vector<Case> bow_cases();
std::vector<Case> review_b0_cases();
std::vector<Case> combat_feel_cases();
std::vector<Case> review_b1_cases();
std::vector<Case> boltcaster_cases();
std::vector<Case> review_u3_cases();
std::vector<Case> haste_cases();
std::vector<Case> review_b2_cases();
std::vector<Case> bow_rules_cases();
std::vector<Case> review_b3_cases();
std::vector<Case> menu_equipment_cases();
std::vector<Case> review_37a_cases();
std::vector<Case> refining_cases();
std::vector<Case> review_u4_cases();
std::vector<Case> procedural_cases();
std::vector<Case> review_37b_cases();

std::vector<Case> all_cases() {
    std::vector<Case> cases;
    for (const auto& group : {session_cases(), session_end_cases(), world_cases(), world_model_cases(), inventory_cases(),
                              equipment_cases(), review_cases(), review_inventory_cases(), progress_cases(), skill_cases(),
                              review_progress_cases(), admin_cases(), position_cases(), creature_cases(), combat_cases(),
                              world_spawn_cases(), fight_cases(), navigation_cases(), review_fight_cases(), review_path_cases(),
                              review_corrections_cases(), patrol_cases(), review_patrol_cases(), loot_cases(), ability_cases(), review_loot_cases(), review_scope_cases(), itemization_cases(),
                              review_itemization_cases(), status_cases(), review_status_cases(), spell_cases(), motion_cases(), review_spell_cases(),
                              review_motion_cases(), projectile_cases(), review_projectile_cases(), spell_kind_cases(), review_spells2_cases(),
                              ownership_cases(), review_dlc_cases(), crit_cases(), passive_cases(), review_crit_cases(), heal_cases(), attack_sequence_cases(), review_heal_cases(), review_combos_cases(), gawh_cases(), review_gawh_cases(), review_wrecking_cases(), interaction_cases(), review_interaction_cases(), station_cases(), ranged_cases(), review_station_cases(), ranged_b_cases(), review_ranged_cases(), review_casters_cases(), ranged_c_cases(), review_areas_cases(), support_cases(), chest_cases(), review_support_cases(), loot_bag_cases(), mana_cases(), review_chests_cases(), world_time_cases(), review_loot_bags_cases(), review_world_time_cases(), gathering_cases(), user_test_2_cases(), review_gathering_cases(), build_preset_cases(), review_user_test_2_cases(), bow_cases(), review_b0_cases(), review_b1_cases(), combat_feel_cases(), boltcaster_cases(), review_u3_cases(), haste_cases(), review_b2_cases(), bow_rules_cases(), review_b3_cases(), crafting_cases(), review_b4_cases(), menu_equipment_cases(), review_37a_cases(), refining_cases(), review_u4_cases(), procedural_cases(), review_37b_cases(), quest_cases(), review_quests_18a_cases(), quest_event_cases(), review_quests_18c_cases(), person_cases(), review_persons_25c_cases(), quest_person_cases(), review_quests_18b_cases(), quest_reward_cases(), review_quests_18d_cases(), quest_closure_cases(), review_quests_18e_cases(), behavior_tree_cases()}) {
        cases.insert(cases.end(), group.begin(), group.end());
    }
    return cases;
}

} // namespace soulwarden::backend::tests
