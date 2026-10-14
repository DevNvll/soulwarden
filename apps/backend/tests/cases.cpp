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

std::vector<Case> all_cases() {
    std::vector<Case> cases;
    for (const auto& group : {session_cases(), session_end_cases(), world_cases(), world_model_cases(), inventory_cases(),
                              equipment_cases(), review_cases(), review_inventory_cases(), progress_cases(), skill_cases(),
                              review_progress_cases(), admin_cases(), position_cases(), creature_cases(), combat_cases(),
                              world_spawn_cases(), fight_cases(), navigation_cases(), review_fight_cases()}) {
        cases.insert(cases.end(), group.begin(), group.end());
    }
    return cases;
}

} // namespace soulwarden::backend::tests
