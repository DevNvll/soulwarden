#include "world/encounters.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

#include "core/logging/log.h"
#include "gamedata/loot_tables.h"
#include "gamedata/quest_data.h"
#include "world/projectiles.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

float distance3(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

const char* member_kind_text(gamedata::EncounterMemberKind kind) {
    switch (kind) {
    case gamedata::EncounterMemberKind::creature:
        return "creature";
    case gamedata::EncounterMemberKind::object:
        return "start object";
    case gamedata::EncounterMemberKind::unknown:
        break;
    }
    return "unknown";
}

/** @return The distance from a point to the segment of a capsule, less its radius (0 or less: the point is in the capsule). */
float segment_gap(const state::WorldPosition& point, const WorldCapsule& capsule) {
    const float ax = capsule.b.x - capsule.a.x;
    const float ay = capsule.b.y - capsule.a.y;
    const float az = capsule.b.z - capsule.a.z;
    const float length = ax * ax + ay * ay + az * az;
    float t = length > 1.0e-6F
                  ? ((point.x - capsule.a.x) * ax + (point.y - capsule.a.y) * ay + (point.z - capsule.a.z) * az) / length
                  : 0.0F;
    t = std::clamp(t, 0.0F, 1.0F);
    const state::WorldPosition nearest{capsule.a.x + ax * t, capsule.a.y + ay * t, capsule.a.z + az * t};
    return distance3(point, nearest) - capsule.radius;
}

// The reach of the body of a character beyond its place (the capsule of the player slice: 0.41 m and 0.8 m; a bound of the query).
constexpr float kBodyReach = 3.0F;

std::string short_slice(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

const char* encounter_phase_text(EncounterPhase phase) {
    switch (phase) {
    case EncounterPhase::waiting:
        return "waiting";
    case EncounterPhase::running:
        return "running";
    case EncounterPhase::succeeded:
        return "succeeded";
    case EncounterPhase::failed:
        return "failed";
    }
    return "unknown";
}

const char* encounter_end_text(EncounterEnd end) {
    switch (end) {
    case EncounterEnd::succeeded:
        return "success";
    case EncounterEnd::failed:
        return "failure";
    case EncounterEnd::dormant:
        return "no character near the placement";
    case EncounterEnd::command:
        return "a command";
    }
    return "unknown";
}

Encounters::Encounters(World& world, const gamedata::EncounterData* data) : m_world(world), m_data(data) {}

std::int64_t Encounters::cell_of(float value) {
    return static_cast<std::int64_t>(std::floor(value / encounter_rules::kCellSize));
}

void Encounters::load() {
    m_loaded = true;
    if (data() == nullptr) {
        return;
    }
    m_placements.reserve(m_data->recipes().size());
    for (const gamedata::EncounterRecipe& recipe : m_data->recipes()) {
        EncounterPlacement placement;
        placement.recipe = &recipe;
        resolve(placement);
        const auto index = static_cast<std::uint32_t>(m_placements.size());
        ++m_counts.placements;
        if (placement.refused.empty()) {
            ++m_counts.taken;
            // Stage QG9a: an instance recipe runs only in the space of an instance of its mode (start_space), not at a placement of the
            // main world.
            if (recipe.mode.empty()) {
                m_cells[{cell_of(recipe.position.x), cell_of(recipe.position.y)}].push_back(index);
            }
        }
        m_placements.push_back(std::move(placement));
    }
    // Stage QG9b [a rule of this project from the quest data]: the instance recipes of a mode with more than one recipe that is not a daily
    // alternative (QuestSeraphim: one recipe for each seraph; QuestYonas: the hallway, the Isabella intro, the boss) start only for a character
    // with an active task of the mode that names the recipe: its success event, or a kill category of a creature member that a destroyed
    // objective of it tracks. The mode script that chooses and orders them is not in the client.
    std::map<std::string, std::vector<std::uint32_t>> byMode;
    for (std::uint32_t index = 0; index < m_placements.size(); ++index) {
        const gamedata::EncounterRecipe& recipe = *m_placements[index].recipe;
        if (!recipe.mode.empty() && !recipe.daily) {
            byMode[gamedata::lower_text(recipe.mode)].push_back(index);
        }
    }
    const gamedata::LootTables* tables = m_world.rewards().tables();
    for (const auto& [mode, list] : byMode) {
        // Stage QG9d: the task keys of each instance recipe (mode_recipe_task: the end of the wait for the quest objects of the mode root);
        // the gate only for a mode with more than one recipe.
        for (const std::uint32_t index : list) {
            EncounterPlacement& placement = m_placements[index];
            const gamedata::EncounterRecipe& recipe = *placement.recipe;
            placement.gated = list.size() >= 2;
            const auto add = [&](std::string key) {
                if (std::find(placement.taskKeys.begin(), placement.taskKeys.end(), key) == placement.taskKeys.end()) {
                    placement.taskKeys.push_back(std::move(key));
                }
            };
            if (!recipe.successEvent.empty()) {
                add("e:" + gamedata::lower_text(recipe.successEvent));
            }
            for (const gamedata::EncounterStage& stage : recipe.stages) {
                for (std::size_t number = 0; number <= stage.more.size(); ++number) {
                    const gamedata::EncounterObjective& objective = objective_of(stage, number);
                    if (objective.kind != gamedata::EncounterObjectiveKind::destroyed || objective.tracked.empty()) {
                        continue;
                    }
                    for (const gamedata::EncounterStage& owner : recipe.stages) {
                        for (const gamedata::EncounterSpawn& spawn : owner.spawns) {
                            if (!counts_toward(objective, spawn)) {
                                continue;
                            }
                            // Code review of stage QG9b, finding QG9BR-R4: the game event of a tracked start object of the recipe (the
                            // pressure plate of the Isabella intro of QuestYonas: PressurePlate_Ancient_interact) names the recipe too. The
                            // task of that event comes before the task of the success event of the recipe.
                            if (spawn.memberIndex < m_data->members().size()) {
                                const gamedata::EncounterMember& member = m_data->members()[spawn.memberIndex];
                                if (member.kind == gamedata::EncounterMemberKind::object && !member.event.empty()) {
                                    add("e:" + gamedata::lower_text(member.event));
                                }
                            }
                            const gamedata::CreatureKind* kind = spawn.memberIndex < placement.kinds.size() ? placement.kinds[spawn.memberIndex] : nullptr;
                            if (kind == nullptr) {
                                continue;
                            }
                            const gamedata::LootVitals* vitals = tables != nullptr ? tables->vitals_of(kind->vitals) : nullptr;
                            const std::vector<std::string> categories = tables != nullptr
                                                                            ? tables->kill_categories(vitals, kind->vitals, kind->category, true)
                                                                            : std::vector<std::string>{gamedata::lower_text(kind->vitals)};
                            for (const std::string& category : categories) {
                                add("k:" + category);
                            }
                        }
                    }
                }
            }
        }
    }
    log::write("world: encounters: %llu quest encounter recipes, %llu taken by the runner, %zu refused (docs/systems/encounters.md)",
               static_cast<unsigned long long>(m_counts.placements), static_cast<unsigned long long>(m_counts.taken),
               static_cast<std::size_t>(m_counts.placements - m_counts.taken));
}

void Encounters::resolve(EncounterPlacement& placement) {
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    placement.refused = recipe.refused;
    const std::vector<gamedata::EncounterMember>& members = m_data->members();
    placement.kinds.assign(members.size(), nullptr);
    placement.templates.assign(members.size(), nullptr);
    placement.partKinds.assign(members.size(), {});
    const auto refuse = [&](std::string cause) {
        if (std::find(placement.refused.begin(), placement.refused.end(), cause) == placement.refused.end()) {
            placement.refused.push_back(std::move(cause));
        }
    };
    if (!recipe.autoActivate) {
        refuse("the manager does not start the encounter by itself");
    }
    if (recipe.stageOrder.empty()) {
        refuse("the manager names no stage");
    }
    if (recipe.detectorShapes.empty()) {
        refuse("the detector of the participants has no shape that the runner tests");
    }
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    const gamedata::InteractionData* interactions = m_world.interactions().data();
    const gamedata::GatheringData* gathering = m_world.gathering().data();
    // Stage QG5c4: each objective of each stage is one that the runner runs: a timed, a destroyed, a spawned, or (stage QG5c4) an
    // entity-state objective with a trigger; the other objectives of a stage are destroyed or spawned objectives with no time limit.
    for (const gamedata::EncounterStage& stage : recipe.stages) {
        for (std::size_t index = 0; index <= stage.more.size(); ++index) {
            const gamedata::EncounterObjective& objective = objective_of(stage, index);
            // Stage QG9b: the end rules of an instance recipe (gamedata::StageEnd): a wait with a time limit or after its spawns and its
            // child stages, a stage with no objective at its start, and a boss phase of the BossPhaseComponent of its boss.
            const bool instance = !recipe.mode.empty();
            const gamedata::EncounterMember* boss = objective.kind == gamedata::EncounterObjectiveKind::bossPhase ? m_data->member(objective.phaseOf)
                                                                                                                   : nullptr;
            if (objective.kind == gamedata::EncounterObjectiveKind::other) {
                refuse("the objective " + objective.type + " of the stage " + stage.name + ": the runner does not run it");
            } else if (objective.kind == gamedata::EncounterObjectiveKind::entityState && objective.triggers.empty()
                       && !(instance && (objective.end == gamedata::StageEnd::time || objective.end == gamedata::StageEnd::after))) {
                refuse("the stage " + stage.name + " waits for a change of an entity that no trigger of the data sends");
            } else if (objective.kind == gamedata::EncounterObjectiveKind::none && !(instance && objective.end == gamedata::StageEnd::start)) {
                refuse("the stage " + stage.name + " has no objective");
            } else if (objective.kind == gamedata::EncounterObjectiveKind::bossPhase
                       && (!instance || boss == nullptr || objective.phaseIndex < 0
                           || static_cast<std::size_t>(objective.phaseIndex) >= boss->phases.size())) {
                refuse("the boss phase stage " + stage.name + " names no phase of a boss member of the data");
            } else if (index > 0 && ((objective.kind != gamedata::EncounterObjectiveKind::destroyed
                                      && objective.kind != gamedata::EncounterObjectiveKind::spawned)
                                     || objective.timeLimit > 0.0F)) {
                refuse("the objective " + std::to_string(index + 1) + " of the stage " + stage.name
                       + " is not a destroyed or a spawned objective with no time limit");
            }
        }
    }
    // Stage QG9b, the setting of the mode QuestMedusa (encounter_rules::kQuestMedusaRuns; a pick of the orchestrator): off, its recipes are
    // refused with one cause.
    if (const std::string_view off = encounter_rules::mode_off(recipe.mode, m_world.options().questMedusaRuns); !off.empty()) {
        refuse(std::string(off));
    }
    // Stage QG5c5, the season rule (encounter_rules::kDarknessAtAllTimes; a project rule of the orchestrator): no season schedule.
    if (recipe.season == "darkness" && !encounter_rules::kDarknessAtAllTimes) {
        refuse("the season rule: a Darkness event runs only in its season, and the backend has no season schedule "
               "(encounter_rules::kDarknessAtAllTimes)");
    }
    // Stage QG5c5: a WaitForParentObjective stage ends with its parent: a stage of the manager has no parent, and a child stage after it in
    // the sequence of its parent would never start.
    for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
        const gamedata::EncounterStage& stage = recipe.stages[index];
        if (stage.objective.kind != gamedata::EncounterObjectiveKind::waitParent) {
            continue;
        }
        if (stage.parent < 0) {
            // Stage QG9a [agent]: in an instance recipe a WaitForParentObjective stage of the manager (the setup of Medea, which spawns the
            // caster that the boss fight tracks) runs to the end of the encounter, and the manager starts the next stage at once.
            if (recipe.mode.empty()) {
                refuse("the stage " + stage.name + " waits for its parent, and it is a stage of the manager");
            }
            continue;
        }
        const std::vector<std::uint32_t>& siblings = recipe.stages[static_cast<std::size_t>(stage.parent)].children;
        if (!siblings.empty() && siblings.back() != index) {
            refuse("the stage " + stage.name + " waits for its parent, and a child stage after it would never start");
        }
    }
    // The kind of each creature member and the template of each start object (the other game data files), and the cause of a creature
    // member with no kind.
    std::map<std::uint32_t, std::string> noKind;
    for (const gamedata::EncounterStage& stage : recipe.stages) {
        for (const gamedata::EncounterSpawn& spawn : stage.spawns) {
            if (spawn.skip) {
                continue; // Stage QG9a: a start object that the runner does not make (the crystal of Medea).
            }
            if (spawn.memberIndex >= members.size()) {
                refuse("a spawn of the stage " + stage.name + " names a member that the file does not have");
                continue;
            }
            const gamedata::EncounterMember& member = members[spawn.memberIndex];
            if (member.kind == gamedata::EncounterMemberKind::creature && !member.parts.empty()) {
                // Stage QG5c5: a group member: the kind of each part ("SLICE#N" of creatures.json). The member has no kind of its own
                // when no part has one.
                std::vector<const gamedata::CreatureKind*>& kinds = placement.partKinds[spawn.memberIndex];
                kinds.assign(member.parts.size(), nullptr);
                std::string first;
                for (std::size_t number = 0; number < member.parts.size(); ++number) {
                    std::string cause;
                    const std::optional<std::uint32_t> kind =
                        creatures != nullptr && creatures->loaded() ? creatures->member_kind(member.slice + "#" + std::to_string(number), &cause)
                                                                    : std::nullopt;
                    if (kind) {
                        kinds[number] = &creatures->kinds()[*kind];
                    } else if (first.empty()) {
                        first = cause.empty() ? std::string("the backend has no creature data") : cause;
                    }
                }
                if (std::none_of(kinds.begin(), kinds.end(), [](const gamedata::CreatureKind* kind) { return kind != nullptr; })) {
                    noKind[spawn.memberIndex] = "no part of the group has a kind (" + first + ")";
                }
            } else if (member.kind == gamedata::EncounterMemberKind::creature) {
                std::string cause;
                const std::optional<std::uint32_t> kind = creatures != nullptr && creatures->loaded()
                                                              ? creatures->member_kind(member.slice, &cause) : std::nullopt;
                if (kind) {
                    placement.kinds[spawn.memberIndex] = &creatures->kinds()[*kind];
                } else {
                    noKind[spawn.memberIndex] = cause.empty() ? std::string("the backend has no creature data") : cause;
                }
            } else if (member.kind == gamedata::EncounterMemberKind::object && !(member.proximity && !member.interact)) {
                const gamedata::EncounterObject* object = interactions != nullptr ? interactions->encounter_object(member.slice) : nullptr;
                const std::optional<gamedata::GatheringLink> link = gathering != nullptr ? gathering->encounter_object(member.slice)
                                                                                         : std::nullopt;
                if (object != nullptr && (link || member.proximity)) {
                    placement.templates[spawn.memberIndex] = object;
                }
            }
        }
    }
    // A spawn that the runner makes or counts: a creature with its kind, a start object with its template, an area trigger, a member that
    // ends itself.
    const auto makes = [&](const gamedata::EncounterSpawn& spawn) {
        if (spawn.memberIndex >= members.size()) {
            return false;
        }
        const gamedata::EncounterMember& member = members[spawn.memberIndex];
        if (member.kind == gamedata::EncounterMemberKind::creature && !member.parts.empty()) {
            const std::vector<const gamedata::CreatureKind*>& kinds = placement.partKinds[spawn.memberIndex];
            return std::any_of(kinds.begin(), kinds.end(), [](const gamedata::CreatureKind* kind) { return kind != nullptr; });
        }
        return member.kind == gamedata::EncounterMemberKind::creature ? placement.kinds[spawn.memberIndex] != nullptr
               : member.kind == gamedata::EncounterMemberKind::object
                   ? placement.templates[spawn.memberIndex] != nullptr || (member.proximity && !member.interact)
                   : member.lifetime > 0.0F;
    };
    // The spawns of a stage and of its child stages (the stage first).
    const auto below = [&](std::uint32_t index) {
        std::vector<std::pair<std::uint32_t, const gamedata::EncounterSpawn*>> out;
        std::vector<std::uint32_t> pending{index};
        while (!pending.empty()) {
            const std::uint32_t at = pending.back();
            pending.pop_back();
            for (const gamedata::EncounterSpawn& spawn : recipe.stages[at].spawns) {
                out.emplace_back(at, &spawn);
            }
            pending.insert(pending.end(), recipe.stages[at].children.begin(), recipe.stages[at].children.end());
        }
        return out;
    };
    // Stage QG5c5: the members of one spawn of an entry that an objective counts: the ones that the runner makes, and the ones that it
    // does not make because the client does not show them (a creature member with no kind; a part of a group with no kind).
    const auto tally = [&](const gamedata::EncounterObjective& objective, const gamedata::EncounterSpawn& spawn) {
        std::pair<std::uint32_t, std::uint32_t> out{0, 0};
        if (spawn.memberIndex >= members.size()) {
            return out;
        }
        const gamedata::EncounterMember& member = members[spawn.memberIndex];
        if (member.kind == gamedata::EncounterMemberKind::creature && !member.parts.empty()) {
            for (std::size_t number = 0; number < member.parts.size(); ++number) {
                if (counts_toward(objective, spawn, static_cast<std::int32_t>(number))) {
                    (placement.partKinds[spawn.memberIndex][number] != nullptr ? out.first : out.second) += 1;
                }
            }
            return out;
        }
        if (counts_toward(objective, spawn)) {
            out.first = makes(spawn) ? 1 : 0;
            out.second = noKind.count(spawn.memberIndex) != 0 ? 1 : 0;
        }
        return out;
    };
    // Stage QG5c5: the spawns of the stages of the manager order before the stage of the manager above `index` (with their child stages):
    // a destroyed objective with a tracked slice counts their members too (the spyglass of Scallywag Shore).
    const auto earlier = [&](std::uint32_t index) {
        std::int32_t top = static_cast<std::int32_t>(index);
        while (recipe.stages[static_cast<std::size_t>(top)].parent >= 0) {
            top = recipe.stages[static_cast<std::size_t>(top)].parent;
        }
        std::vector<std::pair<std::uint32_t, const gamedata::EncounterSpawn*>> out;
        for (const std::uint32_t before : recipe.stageOrder) {
            if (static_cast<std::int32_t>(before) == top) {
                break;
            }
            const auto spawns = below(before);
            out.insert(out.end(), spawns.begin(), spawns.end());
        }
        return out;
    };
    // Stage QG5c4: the objectives that are met at the start of their stage [agent: the runner cannot make a creature that the client does
    // not show; the objectives of one stage track different member slices, and the stage keeps its fight]: in a stage with more than one
    // objective, an objective with a tracked slice whose spawns are only creature members with no kind, when an other objective of the
    // stage is not such an objective.
    placement.voided.assign(recipe.stages.size(), {});
    for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
        const gamedata::EncounterStage& stage = recipe.stages[index];
        std::vector<bool>& voided = placement.voided[index];
        voided.assign(stage.more.size() + 1, false);
        if (stage.more.empty()) {
            continue;
        }
        const auto spawns = below(index);
        for (std::size_t at = 0; at < voided.size(); ++at) {
            const gamedata::EncounterObjective& objective = objective_of(stage, at);
            std::uint32_t made = 0;
            std::uint32_t hidden = 0;
            for (const auto& [owner, spawn] : spawns) {
                if (objective.tracked.empty()) {
                    continue;
                }
                const auto [shown, notShown] = tally(objective, *spawn);
                made += shown;
                hidden += notShown;
            }
            voided[at] = !objective.tracked.empty() && made == 0 && hidden > 0;
        }
        if (std::all_of(voided.begin(), voided.end(), [](bool value) { return value; })) {
            voided.assign(voided.size(), false); // A stage of such objectives only: the stage needs them (the cause of the member).
        }
    }
    // A creature member with no kind or a member that the runner does not make, that a stage needs (stage QG5c3: a stage or a stage above
    // it counts it; a timed stage needs no spawn).
    for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
        const gamedata::EncounterStage& stage = recipe.stages[index];
        for (const gamedata::EncounterSpawn& spawn : stage.spawns) {
            if (spawn.memberIndex >= members.size() || spawn.skip) {
                continue;
            }
            const gamedata::EncounterMember& member = members[spawn.memberIndex];
            const std::string name = short_slice(member.slice);
            const bool needed = spawn_needed(placement, index, spawn);
            if (member.kind == gamedata::EncounterMemberKind::creature) {
                // Stage QG5c5: a group member has a creature when one of its parts has a kind.
                const bool none = member.parts.empty() ? placement.kinds[spawn.memberIndex] == nullptr : noKind.count(spawn.memberIndex) != 0;
                if (none && needed) {
                    refuse("the creature member " + name + ": " + noKind[spawn.memberIndex]);
                }
            } else if (member.kind == gamedata::EncounterMemberKind::object) {
                if (member.proximity && !member.interact) {
                    continue; // Stage QG5c3: an area trigger: the runner finds the character in its sphere; no record, no row.
                }
                if (interactions == nullptr || interactions->encounter_object(member.slice) == nullptr) {
                    refuse("the start object " + name + " has no template of the interaction data (encounterObjects)");
                } else if (placement.templates[spawn.memberIndex] == nullptr) {
                    refuse("the start object " + name + " has no row of the gathering data (encounterObjects)");
                } else if (const std::optional<gamedata::GatheringLink> link = gathering != nullptr ? gathering->encounter_object(member.slice)
                                                                                                    : std::nullopt;
                           !member.proximity && link && link->row < gathering->rows().size() && !gathering->rows()[link->row].unsupported.empty()) {
                    // Code review of stage QG5c4, finding QG5C4R-R1: with no gathering data (a missing or a refused file) a proximity
                    // object has no row and no lookup: no call through a null pointer at the load.
                    // Stage QG5c4: the gathering system refuses the gather of such a row (a rule that the backend cannot check): no
                    // character can count the object for its stage.
                    const gamedata::GatherableRow& row = gathering->rows()[link->row];
                    refuse("the start object " + name + ": its gatherable row " + row.id + " has a rule that the backend cannot check ("
                           + row.unsupported.front() + ")");
                }
            } else if (needed && !(member.lifetime > 0.0F)) {
                // A member that is no creature and no start object counts only as a visual of a stage that does not need it, or (stage
                // QG5c3) as a member that ends itself.
                refuse("the member " + name + " is " + member_kind_text(member.kind) + ": " + member.cause);
            }
        }
    }
    // Code review of stage QG5c2, finding QG5C2R-R3: the spawns that the runner makes and that count for a destroyed or a spawned objective
    // (its stage and its child stages) must reach its required count, or a time limit with succeedOnTimeout ends it. Stage QG5c3: a
    // destroyed stage with no tracked slice and no spawn below it counts each death of a member of its instance (it needs that time limit).
    // Code review of stage QG5c3, finding QG5C3R-R3: a spawn due at or after the time limit of its own stage gives no count (the end of its
    // stage comes first). Stage QG5c4: each objective of the stage, not one that is met at its start.
    // Code review of stage QG5c4, finding QG5C4R-R4: a spawn comes only when its own stage and each stage above it run at its time. Its
    // earliest time from the start of a stage above it adds the earlier child stages that end only at their time (a timed stage with one
    // objective: the sequence of the child stages); a stage with a time limit at or before that time ends first.
    const auto comes = [&](std::uint32_t owner, const gamedata::EncounterSpawn& spawn) {
        double at = static_cast<double>(spawn.delay.count()) / 1000.0;
        for (std::int32_t current = static_cast<std::int32_t>(owner); current >= 0;) {
            const gamedata::EncounterStage& stage = recipe.stages[static_cast<std::size_t>(current)];
            if (stage.objective.timeLimit > 0.0F && at >= static_cast<double>(stage.objective.timeLimit)) {
                return false;
            }
            if (stage.parent < 0) {
                break;
            }
            for (const std::uint32_t sibling : recipe.stages[static_cast<std::size_t>(stage.parent)].children) {
                if (static_cast<std::int32_t>(sibling) == current) {
                    break;
                }
                const gamedata::EncounterStage& before = recipe.stages[sibling];
                if (before.objective.kind == gamedata::EncounterObjectiveKind::timed && before.more.empty()) {
                    at += static_cast<double>(before.objective.timeLimit);
                }
            }
            current = stage.parent;
        }
        return true;
    };
    // Code review of stage QG5c5, finding QG5C5R-R3: a member of an earlier stage counts for a later stage only when it can still wait at its
    // start: no KillSpawnsAction and no DestroySliceAction that names it (its slice or its alias) in the end actions of the top stage of its
    // owner and of its child stages, in the start and the end actions of each top stage between (with its child stages), and in the start
    // actions of the top stage of the consumer.
    const auto top_of = [&](std::uint32_t index) {
        std::int32_t top = static_cast<std::int32_t>(index);
        while (recipe.stages[static_cast<std::size_t>(top)].parent >= 0) {
            top = recipe.stages[static_cast<std::size_t>(top)].parent;
        }
        return static_cast<std::uint32_t>(top);
    };
    const auto subtree = [&](std::uint32_t root) {
        std::vector<std::uint32_t> out;
        std::vector<std::uint32_t> pending{root};
        while (!pending.empty()) {
            const std::uint32_t current = pending.back();
            pending.pop_back();
            if (current >= recipe.stages.size() || std::find(out.begin(), out.end(), current) != out.end()) {
                continue;
            }
            out.push_back(current);
            pending.insert(pending.end(), recipe.stages[current].children.begin(), recipe.stages[current].children.end());
        }
        return out;
    };
    const auto survives = [&](std::uint32_t owner, const gamedata::EncounterSpawn& spawn, std::uint32_t consumer) {
        const auto names = [&](const std::vector<gamedata::EncounterDestroy>& actions) {
            return std::any_of(actions.begin(), actions.end(), [&](const gamedata::EncounterDestroy& action) {
                for (const std::string* path : {&spawn.member, &spawn.alias}) {
                    if (!path->empty() && (*path == action.slice || *path == action.alias
                                           || std::find(action.aliasSlices.begin(), action.aliasSlices.end(), *path) != action.aliasSlices.end())) {
                        return true;
                    }
                }
                return false;
            });
        };
        const std::uint32_t first = top_of(owner);
        const std::uint32_t last = top_of(consumer);
        for (const std::uint32_t at : subtree(first)) {
            if (recipe.stages[at].killSpawnsAtEnd || names(recipe.stages[at].destroyAtEnd)) {
                return false;
            }
        }
        const auto from = std::find(recipe.stageOrder.begin(), recipe.stageOrder.end(), first);
        for (auto top = from == recipe.stageOrder.end() ? from : std::next(from); top != recipe.stageOrder.end(); ++top) {
            if (*top == last) {
                // Code review of stage QG7, finding QG7R-R4: the actions that run before the consumer starts in its own top stage, in the
                // order of the run: the start actions of each stage on the path, and the start and the end actions of each earlier child (a
                // child of a parent in the order of its children; its end actions after its own children).
                std::vector<std::pair<std::uint32_t, bool>> pending{{*top, false}};
                while (!pending.empty()) {
                    const auto [at, ending] = pending.back();
                    pending.pop_back();
                    const gamedata::EncounterStage& step = recipe.stages[at];
                    if (ending) {
                        if (step.killSpawnsAtEnd || names(step.destroyAtEnd)) {
                            return false;
                        }
                        continue;
                    }
                    if (step.killSpawnsAtStart || names(step.destroyAtStart)) {
                        return false;
                    }
                    if (at == consumer) {
                        return true;
                    }
                    pending.emplace_back(at, true);
                    for (auto child = step.children.rbegin(); child != step.children.rend(); ++child) {
                        pending.emplace_back(*child, false);
                    }
                }
                return true;
            }
            for (const std::uint32_t at : subtree(*top)) {
                const gamedata::EncounterStage& between = recipe.stages[at];
                if (between.killSpawnsAtStart || between.killSpawnsAtEnd || names(between.destroyAtStart) || names(between.destroyAtEnd)) {
                    return false;
                }
            }
        }
        return true;
    };
    placement.anyMember.assign(recipe.stages.size(), false);
    placement.fills.assign(recipe.stages.size(), {});
    for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
        const gamedata::EncounterStage& stage = recipe.stages[index];
        const auto spawns = below(index);
        const auto before = earlier(index);
        placement.fills[index].assign(stage.more.size() + 1, 0);
        for (std::size_t at = 0; at <= stage.more.size(); ++at) {
            const gamedata::EncounterObjective& objective = objective_of(stage, at);
            if ((objective.kind != gamedata::EncounterObjectiveKind::destroyed && objective.kind != gamedata::EncounterObjectiveKind::spawned)
                || placement.voided[index][at]) {
                continue;
            }
            // Stage QG5c5: each member that the objective counts gives one count (a group: each part that it counts); a destroyed
            // objective with a tracked slice counts the members of the stages before it too.
            std::uint32_t possible = 0;
            std::uint32_t hidden = 0;
            for (const auto& [owner, spawn] : spawns) {
                if (comes(owner, *spawn)) {
                    const auto [shown, notShown] = tally(objective, *spawn);
                    possible += spawn->count * shown;
                    hidden += spawn->count * notShown;
                }
            }
            if (objective.kind == gamedata::EncounterObjectiveKind::destroyed && !objective.tracked.empty()) {
                for (const auto& [owner, spawn] : before) {
                    if (spawn->skip || spawn->memberIndex >= members.size()) {
                        continue;
                    }
                    const gamedata::EncounterMemberKind kind = members[spawn->memberIndex].kind;
                    // Stage QG9a: in an instance recipe a creature member of a manager stage that still runs (a WaitForParentObjective
                    // stage of the manager) counts too. Stage QG9b: each creature member of an earlier stage of the manager that no action
                    // removes before the stage (the werewolf lord of _Wave 0 - Wait, the guardian of the setup of the Heartforge) [data: the
                    // stage tracks a slice that only the earlier stage spawns; a rule of this project: the member waits].
                    const bool background = !recipe.mode.empty() && kind == gamedata::EncounterMemberKind::creature;
                    if ((kind == gamedata::EncounterMemberKind::object || background) && comes(owner, *spawn) && survives(owner, *spawn, index)) {
                        possible += spawn->count * tally(objective, *spawn).first;
                    }
                }
                // Stage QG9b: in an instance recipe the spawns of the stages above it count too (the parent makes them at its start, before
                // its child stages: the seraph and the 20 m trigger of Boss_Encounter, the mammoth of Phase_01_Boss).
                for (std::int32_t up = recipe.mode.empty() ? -1 : stage.parent; up >= 0; up = recipe.stages[static_cast<std::size_t>(up)].parent) {
                    for (const gamedata::EncounterSpawn& spawn : recipe.stages[static_cast<std::size_t>(up)].spawns) {
                        if (!spawn.skip && spawn.memberIndex < members.size() && comes(static_cast<std::uint32_t>(up), spawn)) {
                            possible += spawn.count * tally(objective, spawn).first;
                        }
                    }
                }
            }
            const bool timedEnd = stage.objective.timeLimit > 0.0F && stage.objective.succeedOnTimeout;
            const std::string label = at == 0 ? stage.name : stage.name + " (its objective " + std::to_string(at + 1) + ")";
            if (objective.end == gamedata::StageEnd::start && !recipe.mode.empty()) {
                continue; // Stage QG9b: a destroyed stage of an instance recipe with nothing to count ends at its start (the tool rule).
            }
            if (at == 0 && objective.kind == gamedata::EncounterObjectiveKind::destroyed && spawns.empty() && objective.tracked.empty()) {
                placement.anyMember[index] = true;
                if (!timedEnd) {
                    refuse("the stage " + label + " counts the deaths of the members of its encounter and has no time limit that ends it");
                }
                continue;
            }
            if (!timedEnd && objective.requiredCount > possible && possible > 0 && possible + hidden >= objective.requiredCount
                && objective.kind == gamedata::EncounterObjectiveKind::destroyed) {
                // Stage QG5c5 [agent: the rule of stage QG5c4 for the creatures that the client does not show, for the parts of a group]:
                // the parts that the runner does not make (the swarmers of the portal waves) give the count that the made parts cannot
                // give, at the start of the stage; the objective needs each member that the runner makes.
                placement.fills[index][at] = objective.requiredCount - possible;
                continue;
            }
            if (!timedEnd && objective.requiredCount > possible) {
                refuse("the stage " + label + " needs " + std::to_string(objective.requiredCount) + " "
                       + (objective.kind == gamedata::EncounterObjectiveKind::destroyed ? "destroyed" : "spawned")
                       + " spawns, and the members that the runner makes can give only " + std::to_string(possible));
            }
        }
    }
}

bool Encounters::counts_toward(const gamedata::EncounterObjective& objective, const gamedata::EncounterSpawn& spawn, std::int32_t part) const {
    const std::string& tracked = objective.tracked;
    // Stage QG5c5: a spawn of an alias counts for an objective that tracks the alias (the pylons, the cores).
    if (tracked.empty() || tracked == spawn.member || (!spawn.alias.empty() && tracked == spawn.alias)) {
        return true;
    }
    const gamedata::EncounterMember* member = m_data->member(spawn.member);
    if (member == nullptr) {
        return false;
    }
    // The body of the member (stage QG5c5: of a part of a group) is the tracked slice, a slice of the tracked alias, or the alias itself.
    const auto names = [&](const std::string& body, const std::vector<std::string>& slices) {
        const auto has = [&](const std::string& path) {
            return path == body || std::find(slices.begin(), slices.end(), path) != slices.end();
        };
        return has(tracked) || std::any_of(objective.trackedSlices.begin(), objective.trackedSlices.end(), has);
    };
    if (member->parts.empty()) {
        return names(member->body, member->bodySlices);
    }
    if (part >= 0) {
        return static_cast<std::size_t>(part) < member->parts.size()
               && names(member->parts[static_cast<std::size_t>(part)].body, member->parts[static_cast<std::size_t>(part)].bodySlices);
    }
    return std::any_of(member->parts.begin(), member->parts.end(),
                       [&](const gamedata::EncounterPart& each) { return names(each.body, each.bodySlices); });
}

bool Encounters::children_done(const EncounterInstance& instance, std::uint32_t index) const {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    for (const std::uint32_t child : recipe.stages[index].children) {
        if (recipe.stages[child].objective.kind != gamedata::EncounterObjectiveKind::waitParent && !instance.stages[child].ended) {
            return false;
        }
    }
    return true;
}

std::optional<std::uint32_t> Encounters::placement_of(const std::string& key) const {
    const std::string lower = gamedata::lower_text(key);
    for (std::uint32_t index = 0; index < m_placements.size(); ++index) {
        if (m_placements[index].recipe->key == lower) {
            return index;
        }
    }
    return std::nullopt;
}

EncounterInstance* Encounters::instance_of(std::uint32_t placement, std::uint64_t generation) {
    // Stage QG9a: the generation of a placement is unique (one counter for each placement, also across the spaces of its instances).
    const auto live = m_byGeneration.find({placement, generation});
    if (live == m_byGeneration.end()) {
        return nullptr;
    }
    const auto found = m_instances.find(live->second);
    return found != m_instances.end() && found->second.generation == generation && found->second.placement == placement ? &found->second
                                                                                                                         : nullptr;
}

void Encounters::schedule(WorldClock::time_point at, const Due& due) {
    m_due.emplace(at, due);
}

void Encounters::tick() {
    if (!m_loaded) {
        if (data() == nullptr) {
            return; // No data yet: the system loads at the first tick with data (a test sets its data after the start of the world).
        }
        load();
    }
    if (data() == nullptr || (m_counts.taken == 0 && m_payouts.empty())) {
        return;
    }
    take_events();
    take_due();
    const WorldClock::time_point now = m_world.clock().now();
    if (!m_lastScan || now - *m_lastScan >= encounter_rules::kScanInterval) {
        m_lastScan = now;
        const LoadTimer timer(m_scanLoad);
        scan();
    }
}

void Encounters::scan() {
    ++m_counts.scans;
    const WorldClock::time_point now = m_world.clock().now();
    // The placement rule: the cells near each character (MP-8: no look at each placement of the world).
    std::set<std::uint32_t> near;
    m_world.for_each_character([&](const Entity& character) {
        // Stage DG1: the placements are placements of the main world (the instance recipes are a later stage).
        if (character.space != kMainSpace) {
            return;
        }
        const std::int64_t lowX = cell_of(character.position.x - encounter_rules::kSpawnEnter);
        const std::int64_t highX = cell_of(character.position.x + encounter_rules::kSpawnEnter);
        const std::int64_t lowY = cell_of(character.position.y - encounter_rules::kSpawnEnter);
        const std::int64_t highY = cell_of(character.position.y + encounter_rules::kSpawnEnter);
        for (std::int64_t x = lowX; x <= highX; ++x) {
            for (std::int64_t y = lowY; y <= highY; ++y) {
                const auto cell = m_cells.find({x, y});
                if (cell == m_cells.end()) {
                    continue;
                }
                for (const std::uint32_t index : cell->second) {
                    ++m_counts.placementsLooked;
                    if (horizontal_distance(character.position, m_placements[index].recipe->position) <= encounter_rules::kSpawnEnter) {
                        near.insert(index);
                    }
                }
            }
        }
    });
    for (const std::uint32_t index : near) {
        EncounterPlacement& placement = m_placements[index];
        if (!placement.live && (!placement.respawnAt || now >= *placement.respawnAt)) {
            make_instance(index);
        }
    }
    // The leave rule and the participants of each instance (the characters near its root: the position index).
    std::vector<EntityId> dormant;
    for (auto& [root, instance] : m_instances) {
        const Entity* entity = m_world.find(root);
        if (entity == nullptr) {
            continue;
        }
        if (instance.space != kMainSpace) {
            // Stage QG9a: an encounter of an instance is no placement of the main world: it has no dormant end (the instance system ends
            // it with its space).
            update_participants(instance);
            continue;
        }
        bool someone = false;
        m_world.for_each_near(entity->space, entity->position, encounter_rules::kSpawnLeave, [&](Entity& other) {
            ++m_counts.entitiesLooked;
            someone = someone || other.kind == EntityKind::player;
        });
        if (!someone) {
            dormant.push_back(root);
            continue;
        }
        update_participants(instance);
    }
    for (const EntityId root : dormant) {
        end_instance(root, EncounterEnd::dormant);
    }
    // Stage QG9a: the instance recipes of the spaces of the instances.
    scan_spaces();
    // Stage QG5c3: the proximity objects of the running stages (the starts of the ambushes). Stage QG5c4: the triggers of the running
    // entity-state stages.
    look_proximity();
    look_triggers();
    look_phases();
    // A fight with no participant: the encounter fails (a reset; a rule of this project, docs/systems/encounters.md).
    std::vector<EntityId> failed;
    for (const auto& [root, instance] : m_instances) {
        if (instance.phase != EncounterPhase::running || !instance.participants.empty()) {
            continue;
        }
        const bool fighting = std::any_of(instance.members.begin(), instance.members.end(), [](const auto& entry) {
            return !entry.second.object && !entry.second.noEntity && !entry.second.done;
        });
        if (fighting) {
            failed.push_back(root);
        }
    }
    for (const EntityId root : failed) {
        const auto found = m_instances.find(root);
        if (found != m_instances.end()) {
            log::write("world: encounters: the encounter %s (generation %llu) has a fight and no participant: it fails",
                       m_placements[found->second.placement].recipe->key.c_str(), static_cast<unsigned long long>(found->second.generation));
            manager_status(found->second, encounter_rules::kFailed);
            end_instance(root, EncounterEnd::failed);
        }
    }
}

void Encounters::look_proximity() {
    if (m_proximity.empty()) {
        return;
    }
    // The rule of the proximity quest objects (stage QG5a, docs/systems/quests.md; data: m_isProximityGatherable and the sphere of the
    // detection volume of the slice): an accepted position of a character (no camera estimate, no cutscene, no cinematic) that lives, in
    // the sphere (3D), and for an InteractOnlyOnQuest row an Active task with the tag. Each such character finds the object at this look: the
    // game event of its row and its tag (one plan of the quest system each); then the object goes, and its stage counts it [agent: the
    // object is shared, and the look of the scan; the cost: the characters near the few objects of the running stages, MP-8].
    // Code review of stage QG5c3, finding QG5C3R-R2: each finder of an object keeps its write: a finder whose quest write the store refused
    // stays in the ledger of the object and is tried again at the next looks (also out of the sphere: it found the object), at most
    // kFindAttempts times; the object goes, and its stage counts it one time, when each finder of the ledger has its write or ran out of
    // attempts and at least one write was taken [agent: the rule of the credit ledger of the success].
    std::vector<std::pair<EntityId, std::vector<std::string>>> found;
    for (const auto& [id, object] : m_proximity) {
        EncounterInstance* instance = instance_of(object.placement, object.generation);
        if (instance == nullptr || instance->phase != EncounterPhase::running) {
            continue;
        }
        const auto member = instance->members.find(id);
        if (member == instance->members.end() || member->second.done || !instance->stages[member->second.stage].started
            || instance->stages[member->second.stage].ended) {
            continue;
        }
        ++m_counts.proximityLooks;
        const float radius = object.member->radius;
        std::vector<std::string> finders;
        for (const auto& [characterId, ledger] : object.failures) {
            finders.push_back(characterId);
        }
        // Stage QG9b: the space of the instance (an area trigger of an instance recipe: the setup of Medusa, the 20 m trigger of a seraph).
        m_world.for_each_near(instance->space, object.place, radius, [&](Entity& entity) {
            ++m_counts.entitiesLooked;
            if (entity.kind != EntityKind::player || entity.life.dead || entity.positionEstimated || entity.quests.cutsceneStarted
                || entity.quests.pendingCinematic != 0 || distance3(entity.position, object.place) > radius
                || object.paid.count(entity.characterId) != 0 || object.failures.count(entity.characterId) != 0) {
                return;
            }
            if (object.member->onlyOnQuest && !m_world.quests().quest_object_open(entity, object.member->tag)) {
                return; // The client shows the object only during the task (client 0x8261FC0): no find for a character with no task.
            }
            finders.push_back(entity.characterId);
        });
        if (!finders.empty()) {
            std::sort(finders.begin(), finders.end());
            found.emplace_back(id, std::move(finders));
        }
    }
    for (const auto& [id, finders] : found) {
        const auto entry = m_proximity.find(id);
        if (entry == m_proximity.end()) {
            continue;
        }
        Proximity& object = entry->second;
        EncounterInstance* instance = instance_of(object.placement, object.generation);
        if (instance == nullptr) {
            continue;
        }
        for (const std::string& characterId : finders) {
            Entity* character = m_world.find_character(characterId);
            if (character == nullptr) {
                object.failures.erase(characterId); // A finder that left the world gets no event (the rule of the credit).
                continue;
            }
            std::vector<QuestEvent> events;
            if (!object.member->event.empty()) {
                QuestEvent applied;
                applied.kind = QuestEventKind::gameEvent;
                applied.keys = {object.member->event};
                events.push_back(std::move(applied));
            }
            if (!object.member->tag.empty()) {
                QuestEvent interacted;
                interacted.kind = QuestEventKind::interact;
                interacted.keys = {object.member->tag};
                events.push_back(std::move(interacted));
            }
            // Code review of stage QG5c4, finding QG5C4R-R3: a retry credits only the tasks that the find matched at its first attempt.
            const auto retry = object.failures.find(characterId);
            for (std::size_t position = 0; retry != object.failures.end() && position < events.size(); ++position) {
                events[position].restricted = true;
                events[position].attempt = retry->second.attempts;
                events[position].matched = position < retry->second.matched.size() ? retry->second.matched[position]
                                                                                     : std::vector<std::pair<std::uint64_t, std::uint8_t>>{};
            }
            std::vector<std::vector<std::pair<std::uint64_t, std::uint8_t>>> matched;
            if (const QuestError error = m_world.quests().object_destroyed(*character, {}, events, &matched); error != QuestError::none) {
                FindRetry& ledger = object.failures[characterId];
                if (ledger.attempts == 0) {
                    ledger.matched = std::move(matched);
                }
                const std::uint32_t attempts = ++ledger.attempts;
                ++m_counts.findRetries;
                log::write("world: encounters: the quest change of the find of the proximity object %016llx by %s was refused (%s, attempt %u)",
                           static_cast<unsigned long long>(id), characterId.c_str(), quest_error_text(error), attempts);
                if (attempts >= encounter_rules::kFindAttempts) {
                    object.failures.erase(characterId);
                    ++m_counts.creditsDropped;
                    log::write("world: encounters: the store did not take the find of %s in %u attempts: no event", characterId.c_str(),
                               attempts);
                }
                continue;
            }
            object.failures.erase(characterId);
            object.paid.insert(characterId);
            log::write("world: encounters: character %s is in the sphere of the proximity object %016llx of the encounter %s (%s, %.1f m)",
                       characterId.c_str(), static_cast<unsigned long long>(id), m_placements[object.placement].recipe->key.c_str(),
                       short_slice(object.member->slice).c_str(), static_cast<double>(object.member->radius));
        }
        if (object.paid.empty() || !object.failures.empty()) {
            continue; // No write yet, or a finder waits for its write: the object stays for the next look.
        }
        ++m_counts.objectsFound;
        m_proximity.erase(entry);
        if (!instance->members.at(id).noEntity) {
            m_memberOf.erase(id);
            m_world.interactions().remove_object(id);
        }
        count_destroyed(*instance, id);
    }
}

void Encounters::update_participants(EncounterInstance& instance) {
    if (instance.phase != EncounterPhase::running) {
        return;
    }
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const Entity* root = m_world.find(instance.root);
    if (root == nullptr) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    std::set<std::string> inside;
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    const gamedata::CreatureCapsule* player = creatures != nullptr && creatures->player_capsule() ? &*creatures->player_capsule() : nullptr;
    // The detector (OnlyPlayerTarget, m_ignoreHeight 0): a sphere in three dimensions; stage QG5c3: a cylinder or an axis box, and a character
    // in one of the shapes of the entity of the tracker is in the detector.
    m_world.for_each_near(root->space, root->position, recipe.detectorReach + kBodyReach, [&](Entity& entity) {
        ++m_counts.entitiesLooked;
        if (entity.kind == EntityKind::player && in_detector(recipe, root->position, capsule_of(entity, player))) {
            inside.insert(entity.characterId);
        }
    });
    for (const std::string& characterId : inside) {
        auto found = instance.participants.find(characterId);
        if (found != instance.participants.end()) {
            found->second.graceUntil.reset();
            continue;
        }
        if (instance.participants.size() >= encounter_rules::kMaxParticipants) {
            continue;
        }
        instance.participants.emplace(characterId, EncounterParticipant{now, std::nullopt});
        ++m_counts.participantsAdded;
        if (Entity* character = m_world.find_character(characterId)) {
            link(*character, instance.root, true);
            log::write("world: encounters: character %s is a participant of the encounter %s (generation %llu)", characterId.c_str(),
                       recipe.key.c_str(), static_cast<unsigned long long>(instance.generation));
            if (!recipe.participantEvent.empty()) {
                // m_participantAddedGameEventId (game data: 0503_DryadAltar_WaveParticipant): the event of a new participant.
                QuestEvent event;
                event.kind = QuestEventKind::gameEvent;
                event.keys = {recipe.participantEvent};
                (void)m_world.quests().event(*character, event);
            }
        }
    }
    for (auto participant = instance.participants.begin(); participant != instance.participants.end();) {
        if (inside.count(participant->first) != 0) {
            ++participant;
            continue;
        }
        if (!participant->second.graceUntil) {
            // Stage QG9b: an encounter slice with no ParticipantTracker (the detector of the areas of its mode root) has the grace of its
            // manager (m_gracePeriod) [a rule of this project].
            const std::uint32_t grace = recipe.trackerComponent == 0 && recipe.detectorFromMode ? recipe.managerGrace : recipe.participantGrace;
            participant->second.graceUntil = now + std::chrono::seconds(grace);
        }
        if (now >= *participant->second.graceUntil) {
            if (Entity* character = m_world.find_character(participant->first)) {
                link(*character, instance.root, false);
            }
            log::write("world: encounters: character %s is no participant of the encounter %s any more (out of the sphere for %u s)",
                       participant->first.c_str(), recipe.key.c_str(), recipe.participantGrace);
            ++m_counts.participantsLeft;
            participant = instance.participants.erase(participant);
            continue;
        }
        ++participant;
    }
}

bool Encounters::in_detector(const gamedata::EncounterRecipe& recipe, const state::WorldPosition& root, const WorldCapsule& body) {
    return in_shapes(recipe.detectorShapes, root, body);
}

bool Encounters::in_shapes(const std::vector<gamedata::EncounterShape>& shapes, const state::WorldPosition& place, const WorldCapsule& body) {
    // Stage QG5c3: the detection volume overlaps the body of the character (its capsule: upright, from its feet; capsule_of), not only its
    // feet [agent: the backend has no shape of the physics; a character on the ground at the height of the root is at the floor of the
    // cylinder of Torment]. The center of a shape is its offset from the place in world axes; stage QG9d: the tool turns m_center by the
    // rotation of the world transform of its entity (client 0x1710C60, encounter_recipes.local_offset), so the offset here needs no turn.
    // An axis box is aligned with the world axes [agent: the query of the server facet is not in the client]. Stage QG5c4: the same test
    // for the shapes of a trigger at its place, and for a detector shape at its shape entity (the box at the top of a tarot tower).
    const float low = std::min(body.a.z, body.b.z) - body.radius;
    const float high = std::max(body.a.z, body.b.z) + body.radius;
    for (const gamedata::EncounterShape& shape : shapes) {
        const state::WorldPosition center{place.x + shape.center[0], place.y + shape.center[1], place.z + shape.center[2]};
        const float dx = body.a.x - center.x;
        const float dy = body.a.y - center.y;
        switch (shape.kind) {
        case gamedata::EncounterShape::Kind::sphere:
            if (segment_gap(center, body) <= shape.radius) {
                return true;
            }
            break;
        case gamedata::EncounterShape::Kind::cylinder:
            if (dx * dx + dy * dy <= (shape.radius + body.radius) * (shape.radius + body.radius)
                && high >= center.z - shape.height * 0.5F && low <= center.z + shape.height * 0.5F) {
                return true;
            }
            break;
        case gamedata::EncounterShape::Kind::aabb:
            if (std::fabs(dx) <= shape.extents[0] + body.radius && std::fabs(dy) <= shape.extents[1] + body.radius
                && high >= center.z - shape.extents[2] && low <= center.z + shape.extents[2]) {
                return true;
            }
            break;
        }
    }
    return false;
}

void Encounters::look_triggers() {
    if (m_armed.empty()) {
        return;
    }
    // Stage QG5c4: the trigger of an EntityActiveStateChangedObjective (game data: m_targetEntityId, m_completeOnDeactivated, and the
    // DetectionVolumeEventComponent of the slice whose m_eventsOnEnter deactivates the target; the Trigger_Start of a tarot tower is a sphere
    // of 20 m, PlayerOnly, m_detectDeadTargets 1). A character with an accepted position (no camera estimate) in a shape of the trigger ends
    // the stage [data: the volume and its filter; agent: the look at the scan of the runner (500 ms), the capsule of the character]. The
    // cost: the characters near the few triggers of the running stages (MP-8).
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    const gamedata::CreatureCapsule* player = creatures != nullptr && creatures->player_capsule() ? &*creatures->player_capsule() : nullptr;
    std::vector<Armed> armed;
    armed.swap(m_armed);
    std::vector<Armed> ended;
    for (const Armed& entry : armed) {
        EncounterInstance* instance = instance_of(entry.placement, entry.generation);
        if (instance == nullptr || instance->phase != EncounterPhase::running || entry.stage >= instance->stages.size()
            || !instance->stages[entry.stage].started || instance->stages[entry.stage].ended) {
            continue; // The stage or its instance ended: the trigger goes.
        }
        const gamedata::EncounterStage& stage = m_placements[entry.placement].recipe->stages[entry.stage];
        std::string finder;
        for (const gamedata::EncounterTrigger& trigger : stage.objective.triggers) {
            ++m_counts.triggerLooks;
            m_world.for_each_near(instance->space, trigger.position, trigger.reach + kBodyReach, [&](Entity& entity) {
                ++m_counts.entitiesLooked;
                if (!finder.empty() || entity.kind != EntityKind::player || entity.positionEstimated || (entity.life.dead && !trigger.deadTargets)) {
                    return;
                }
                if (in_shapes(trigger.shapes, trigger.position, capsule_of(entity, player))) {
                    finder = entity.characterId;
                }
            });
            if (!finder.empty()) {
                log::write("world: encounters: character %s is in the trigger %s of the stage %s of the encounter %s", finder.c_str(),
                           trigger.entity.c_str(), stage.name.c_str(), m_placements[entry.placement].recipe->key.c_str());
                break;
            }
        }
        if (finder.empty()) {
            m_armed.push_back(entry);
        } else {
            ended.push_back(entry);
        }
    }
    for (const Armed& entry : ended) {
        EncounterInstance* instance = instance_of(entry.placement, entry.generation);
        if (instance == nullptr || instance->phase != EncounterPhase::running || instance->stages[entry.stage].ended) {
            continue;
        }
        ++m_counts.triggersFound;
        // The change of the target completes its own objective (the first of the stage); the stage ends when each objective is met (its
        // child stages first, if any). Code review of stage QG5c4, finding QG5C4R-R2: the trigger does not meet the other objectives.
        EncounterStageRun& run = instance->stages[entry.stage];
        if (!run.met.empty()) {
            run.met[0] = true;
        }
        publish_objectives(*instance);
        if (std::all_of(run.met.begin(), run.met.end(), [](bool value) { return value; })) {
            objective_met(*instance, entry.stage);
        }
    }
}

void Encounters::link(Entity& character, EntityId root, bool on) {
    std::vector<EntityId>& links = character.encounterLinks;
    const auto found = std::find(links.begin(), links.end(), root);
    if (on == (found != links.end())) {
        return;
    }
    if (on) {
        links.push_back(root);
    } else {
        links.erase(found);
        m_world.quests().encounter_unlinked(character, root);
    }
    character.encounterLinked = true;
    m_world.touch(character.id, ComponentKey::objectives);
}

void Encounters::make_instance(std::uint32_t index, SpaceId space) {
    EncounterPlacement& placement = m_placements[index];
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    EncounterBody body;
    body.asset = recipe.asset;
    body.subId = recipe.sub;
    body.managerSlot = recipe.managerSlot;
    body.placeSlot = recipe.placeSlot;
    body.objectivesSlot = recipe.objectivesSlot;
    body.rotation = recipe.rotation;
    body.placement = index;
    body.generation = ++placement.generation;
    body.managerStatus = encounter_rules::kNotStarted;
    body.objectiveCrc = gamedata::quest_crc(recipe.rootName);
    for (const gamedata::EncounterStage& stage : recipe.stages) {
        body.stages.push_back(EncounterStageState{stage.slot, 0, {}});
    }
    Entity* root = m_world.make_encounter(body, recipe.position, space);
    if (root == nullptr) {
        if (space == kMainSpace) {
            placement.respawnAt = m_world.clock().now() + encounter_rules::kScanInterval * 20;
        }
        return;
    }
    // The objective of the root: its instance id is the entity id of the root (a new instance has a new id: a new banner of the client).
    root->encounter.objectiveInstance = root->id;
    EncounterInstance instance;
    instance.placement = index;
    instance.generation = body.generation;
    instance.root = root->id;
    instance.space = space;
    instance.stages.resize(recipe.stages.size());
    instance.made = m_world.clock().now();
    if (space == kMainSpace) {
        placement.live = true;
        placement.respawnAt.reset();
        m_live[index] = root->id;
    } else {
        m_spaceLive[{index, space}] = root->id;
    }
    m_byGeneration[{index, body.generation}] = root->id;
    EncounterInstance& stored = m_instances[root->id] = instance;
    ++m_counts.made;
    log::write("world: encounters: the encounter %s (%s) is in the world: root %016llx, generation %llu, at %.2f %.2f %.2f, space %u",
               recipe.key.c_str(), short_slice(recipe.slice).c_str(), static_cast<unsigned long long>(root->id),
               static_cast<unsigned long long>(stored.generation), static_cast<double>(recipe.position.x),
               static_cast<double>(recipe.position.y), static_cast<double>(recipe.position.z), static_cast<unsigned>(space));
    if (recipe.activateDelay.count() > 0) {
        schedule(m_world.clock().now() + recipe.activateDelay, Due{DueKind::activate, index, stored.generation, 0, 0, 0});
    } else {
        activate(stored);
    }
}

void Encounters::activate(EncounterInstance& instance) {
    if (instance.phase != EncounterPhase::waiting) {
        return;
    }
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    instance.phase = EncounterPhase::running;
    manager_status(instance, encounter_rules::kInProgress);
    instance.current = 0;
    // Code review of stage QG5c3, finding QG5C3R-R1: the first stage can end the encounter before it returns (a spawned objective that its
    // spawn meets at once): the instance is found again by its placement and its generation.
    const std::uint32_t placement = instance.placement;
    const std::uint64_t generation = instance.generation;
    start_stage(instance, recipe.stageOrder.front());
    // The participants of the start (the characters near the root now).
    if (EncounterInstance* live = instance_of(placement, generation)) {
        update_participants(*live);
    }
}

void Encounters::start_stage(EncounterInstance& instance, std::uint32_t index) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    EncounterStageRun& run = instance.stages[index];
    run.started = true;
    // Stage QG5c4: one count for each objective of the stage; an objective of creature members that the client does not show is met now.
    const std::vector<bool>& voided = m_placements[instance.placement].voided[index];
    run.counts.assign(stage.more.size() + 1, 0);
    run.met.assign(stage.more.size() + 1, false);
    for (std::size_t at = 0; at < run.met.size() && at < voided.size(); ++at) {
        if (voided[at]) {
            run.met[at] = true;
            ++m_counts.objectivesVoided;
        }
    }
    // Stage QG5c5: the count of the parts that the runner does not make (EncounterPlacement::fills: the swarmers of the portal waves).
    const std::vector<std::uint32_t>& fills = m_placements[instance.placement].fills[index];
    for (std::size_t at = 0; at < run.counts.size() && at < fills.size(); ++at) {
        if (fills[at] > 0) {
            run.counts[at] += fills[at];
            if (at == 0) {
                run.destroyed = run.counts[0];
            }
            m_counts.partsFilled += fills[at];
            log::write("world: encounters: the encounter %s (stage %s, objective %zu) counts %u parts that the client does not show",
                       recipe.key.c_str(), stage.name.c_str(), at + 1, fills[at]);
        }
    }
    stage_status(instance, index, encounter_rules::kStageStarted);
    log::write("world: encounters: the encounter %s (generation %llu) starts the stage %s", recipe.key.c_str(),
               static_cast<unsigned long long>(instance.generation), stage.name.c_str());
    if (stage.objective.kind == gamedata::EncounterObjectiveKind::entityState && !stage.objective.triggers.empty()) {
        // Stage QG5c4: the stage waits for a character in a shape of its trigger (look_triggers, at each scan).
        m_armed.push_back(Armed{instance.placement, instance.generation, index});
    }
    // Stage QG5c4 (static read D of stage QG5c4): the start of each timed leaf of the stage goes into field 0 of the root Objectives before
    // the client activates the leaf (the same state as the completion of the leaf before it): the HUD timer counts down from m_timeLimit.
    for (std::size_t number = 0; number <= stage.more.size(); ++number) {
        const gamedata::EncounterObjective& objective = objective_of(stage, number);
        if (objective.leaf >= 0 && objective.timeLimit > 0.0F) {
            if (Entity* root = m_world.find(instance.root)) {
                root->encounter.taskStarts.emplace_back(static_cast<std::uint8_t>(objective.leaf),
                                                        m_world.clock().wall_time_ns(m_world.clock().now()));
            }
        }
    }
    if (stage.killSpawnsAtStart) {
        remove_members(instance, false, true);
    }
    destroy_slices(instance, stage.destroyAtStart);
    const WorldClock::time_point now = m_world.clock().now();
    if (stage.objective.timeLimit > 0.0F) {
        const auto time = std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(stage.objective.timeLimit * 1000.0F)));
        schedule(now + time, Due{DueKind::stageTime, instance.placement, instance.generation, index, 0, 0});
    }
    const std::uint32_t placementIndex = instance.placement;
    const std::uint64_t generation = instance.generation;
    for (std::uint32_t spawn = 0; spawn < stage.spawns.size(); ++spawn) {
        if (stage.spawns[spawn].delay.count() > 0) {
            schedule(now + stage.spawns[spawn].delay, Due{DueKind::spawn, instance.placement, instance.generation, index, spawn, 0});
        } else {
            this->spawn(instance, index, spawn);
            if (instance_of(placementIndex, generation) == nullptr || instance.stages[index].ended) {
                return; // A spawned objective ended the stage (and maybe the encounter).
            }
        }
    }
    publish_objectives(instance);
    // Stage QG9a [agent]: a creature member of an earlier stage that the destroyed objective of this stage tracks and that has no target takes
    // the nearest participant as its target now (the caster of the setup of Medea: its aggro at its spawn found no participant, and the
    // boss fight starts after the intro cutscene; the boss tree that would start the fight is not in the client).
    if (stage.objective.kind == gamedata::EncounterObjectiveKind::destroyed && !stage.objective.tracked.empty() && !recipe.mode.empty()) {
        for (const auto& [id, member] : instance.members) {
            if (member.object || member.noEntity || member.done || member.stage >= recipe.stages.size()
                || member.spawn >= recipe.stages[member.stage].spawns.size()
                || !counts_toward(stage.objective, recipe.stages[member.stage].spawns[member.spawn], member.part)) {
                continue;
            }
            Entity* creature = m_world.find(id);
            if (creature != nullptr && creature->kind == EntityKind::creature && !creature->creature.dead
                && creature->creature.mind.mode == CreatureMode::rest) {
                provoke(instance, *creature);
            }
        }
    }
    // Stage QG9a: an EncounterEventObjective of an instance recipe (Intro_Complete) is met at the start of its stage [agent: no slice or table
    // of the client sends the event; the IGC stage before it is the intro] (a due entry: the end does not run inside this start).
    if (stage.objective.kind == gamedata::EncounterObjectiveKind::event) {
        schedule(now, Due{DueKind::eventMet, instance.placement, instance.generation, index, 0, 0});
    }
    // Stage QG9b: the end rules of an instance recipe after the last spawn of the timeline of the stage (then the child stages:
    // objective_met). A stage with no objective [client 0x26EDAA0, 0x26F71E0: its counters are equal; static read F of stage QG9b, F5]; an
    // entity-state wait with no sender and a destroyed stage with nothing to count [agent: the mode script that ends them is not in the client;
    // the client completes such a wait only at a change of its target, 0x271A210, F4].
    if (!recipe.mode.empty() && (stage.objective.end == gamedata::StageEnd::start || stage.objective.end == gamedata::StageEnd::after)) {
        std::chrono::milliseconds last{0};
        for (const gamedata::EncounterSpawn& entry : stage.spawns) {
            last = std::max(last, entry.delay);
        }
        schedule(now + last, Due{DueKind::endRule, instance.placement, instance.generation, index, 0, 0});
    }
    // Code review of stage QG9a, finding QG9AR-R4: a destroyed stage that starts counts the tracked members that died before (a boss of the
    // background setup killed during the intro). Stage QG9b: a boss phase stage looks at the phases of its boss now (a phase can end before).
    if (!recipe.mode.empty()) {
        count_earlier_deaths(instance, index);
        if (instance_of(placementIndex, generation) == nullptr || instance.stages[index].ended) {
            return;
        }
        if (stage.objective.kind == gamedata::EncounterObjectiveKind::bossPhase) {
            schedule(now, Due{DueKind::phases, instance.placement, instance.generation, index, 0, 0});
        }
    }
    // Stage QG5c3: the child stages run one after the other in the order of m_stages of the stage (a rule of this project from the data:
    // each subwave of an ambush has a time limit just above its last spawn delay; docs/systems/encounters.md). The first starts now.
    if (!stage.children.empty()) {
        run.nextChild = 1;
        start_stage(instance, stage.children.front());
        if (instance_of(placementIndex, generation) == nullptr) {
            return;
        }
    }
    // Stage QG9a [agent]: a WaitForParentObjective stage of the manager of an instance recipe runs in the background to the end of the
    // encounter: the manager starts its next stage now.
    if (stage.parent < 0 && stage.objective.kind == gamedata::EncounterObjectiveKind::waitParent && !recipe.mode.empty()
        && instance.current + 1 < recipe.stageOrder.size() && recipe.stageOrder[instance.current] == index) {
        ++instance.current;
        start_stage(instance, recipe.stageOrder[instance.current]);
    }
}

void Encounters::spawn(EncounterInstance& instance, std::uint32_t index, std::uint32_t entry, std::size_t tries) {
    EncounterPlacement& placement = m_placements[instance.placement];
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    EncounterStageRun& run = instance.stages[index];
    if (instance.phase != EncounterPhase::running || !run.started || run.ended) {
        return; // A spawn of a stage that ended (its due entry came later): no member.
    }
    const gamedata::EncounterSpawn& spawn = stage.spawns[entry];
    if (spawn.skip || spawn.memberIndex >= m_data->members().size()) {
        ++m_counts.spawnsSkipped; // Stage QG9a: a start object that the runner does not make (the crystal of Medea).
        return;
    }
    const gamedata::EncounterMember& member = m_data->members()[spawn.memberIndex];
    if (!recipe.mode.empty() && member.kind == gamedata::EncounterMemberKind::creature && member.parts.empty() && tries < encounter_rules::kFloorTries) {
        // Code review of stage QG9c, finding QG9CR-R1: the floor rule below needs the navigation tile of each spawn location in the world.
        // A tile that loads (or that no character wanted yet) is not the terrain: the spawn waits for it and tries again.
        bool ready = true;
        for (const gamedata::EncounterPlace& location : spawn.locations) {
            ready = m_world.navigation().line_ready(location.position, location.position, instance.space) && ready;
        }
        if (spawn.locations.empty()) {
            ready = m_world.navigation().line_ready(recipe.position, recipe.position, instance.space);
        }
        if (!ready) {
            ++m_counts.spawnsWaited;
            schedule(m_world.clock().now() + encounter_rules::kFloorWait,
                     Due{DueKind::spawn, instance.placement, instance.generation, index, entry, tries + 1});
            return;
        }
    }
    const bool needed = spawn_needed(placement, index, spawn);
    std::uint32_t made = 0;
    // Stage QG5c5: the part of each member that this entry made (-1: not a part of a group), for the spawned objectives below.
    std::vector<std::int32_t> madeParts;
    for (std::uint32_t number = 0; number < spawn.count; ++number) {
        if (instance.members.size() >= encounter_rules::kMaxMembers) {
            log::write("world: encounters: the encounter %s has %zu members: no more", recipe.key.c_str(), instance.members.size());
            return;
        }
        state::WorldPosition place = recipe.position;
        float yaw = recipe.yaw;
        if (!spawn.locations.empty()) {
            const gamedata::EncounterPlace& location = spawn.locations[number % spawn.locations.size()];
            place = location.position;
            yaw = location.yaw;
        }
        if (member.kind == gamedata::EncounterMemberKind::creature && !member.parts.empty()) {
            // Stage QG5c5: a group member: one creature for each part with a kind, at its place in the member slice (the offset turned by
            // the yaw of the spawn location), with the yaw of its entity, and with its patrol path in the world [data: the spawner
            // entities, their m_worldTM, their AIPathComponent; agent: the yaw only, no tilt]. A part that the client does not show is not
            // made (its count: EncounterPlacement::fills).
            const float turn = yaw * 3.14159265358979F / 180.0F;
            const float cosine = std::cos(turn);
            const float sine = std::sin(turn);
            const auto world_of = [&](float x, float y, float z) {
                return state::WorldPosition{place.x + cosine * x - sine * y, place.y + sine * x + cosine * y, place.z + z};
            };
            for (std::size_t part = 0; part < member.parts.size(); ++part) {
                const gamedata::CreatureKind* kind = placement.partKinds[spawn.memberIndex].size() > part
                                                         ? placement.partKinds[spawn.memberIndex][part] : nullptr;
                if (kind == nullptr) {
                    ++m_counts.partsHidden;
                    ++m_counts.spawnsSkipped;
                    continue;
                }
                if (instance.members.size() >= encounter_rules::kMaxMembers) {
                    log::write("world: encounters: the encounter %s has %zu members: no more", recipe.key.c_str(), instance.members.size());
                    break;
                }
                const gamedata::EncounterPart& data = member.parts[part];
                gamedata::PatrolRoute route;
                route.policy = data.route.policy;
                for (const gamedata::PatrolWaypoint& point : data.route.points) {
                    gamedata::PatrolWaypoint waypoint = point;
                    waypoint.position = world_of(point.position.x, point.position.y, point.position.z);
                    waypoint.yaw = point.yaw + yaw;
                    route.points.push_back(std::move(waypoint));
                }
                Entity* creature = m_world.creatures().spawn_member(*kind, world_of(data.offset[0], data.offset[1], data.offset[2]),
                                                                    yaw + data.yaw, instance.placement, instance.generation,
                                                                    route.points.empty() ? nullptr : &route, instance.space);
                if (creature == nullptr) {
                    log::write("world: encounters: the encounter %s did not make the part %zu (%s) of its member %s", recipe.key.c_str(), part,
                               data.name.c_str(), short_slice(member.slice).c_str());
                    continue;
                }
                ++m_counts.membersMade;
                ++m_counts.partsMade;
                EncounterMemberRun run{index, entry, false, false, false, false};
                run.part = static_cast<std::int32_t>(part);
                instance.members[creature->id] = run;
                m_memberOf[creature->id] = {instance.placement, instance.generation};
                ++instance.stages[index].spawned;
                ++made;
                madeParts.push_back(static_cast<std::int32_t>(part));
                if (recipe.overrideAggroDelay) {
                    if (recipe.aggroDelay > 0.0F) {
                        const auto delay = std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(recipe.aggroDelay * 1000.0F)));
                        schedule(m_world.clock().now() + delay, Due{DueKind::aggro, instance.placement, instance.generation, index, 0, creature->id});
                    } else {
                        provoke(instance, *creature);
                    }
                }
                log::write("world: encounters: the encounter %s (generation %llu, stage %s) makes the part %zu (%s, %s) of the group %s: entity "
                           "%016llx at %.2f %.2f %.2f%s",
                           recipe.key.c_str(), static_cast<unsigned long long>(instance.generation), stage.name.c_str(), part, data.name.c_str(),
                           kind->id.c_str(), short_slice(member.slice).c_str(), static_cast<unsigned long long>(creature->id),
                           static_cast<double>(creature->position.x), static_cast<double>(creature->position.y),
                           static_cast<double>(creature->position.z), route.points.empty() ? "" : " (with its path)");
            }
            continue;
        }
        Entity* entity = nullptr;
        if ((member.kind == gamedata::EncounterMemberKind::object && member.proximity && !member.interact)
            || (member.kind == gamedata::EncounterMemberKind::unknown && member.lifetime > 0.0F && needed)) {
            // Stage QG5c3: a member with no entity: an area trigger (a character in its sphere finds it) or a member that ends itself (its
            // end counts for its stage). The backend makes no record of it [agent: a visual; the client needs no record for the stage].
            const EntityId id = m_world.reserve_id();
            const bool proximity = member.kind == gamedata::EncounterMemberKind::object;
            instance.members[id] = EncounterMemberRun{index, entry, proximity, false, proximity, true};
            ++run.spawned;
            ++made;
            if (proximity) {
                m_proximity[id] = Proximity{instance.placement, instance.generation, place, &member, {}, {}};
                ++m_counts.objectsMade;
            } else {
                const auto lifetime = std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(member.lifetime * 1000.0F)));
                schedule(m_world.clock().now() + lifetime, Due{DueKind::memberEnd, instance.placement, instance.generation, index, entry, id});
            }
            log::write("world: encounters: the encounter %s (generation %llu, stage %s) counts the %s %s (no entity) at %.2f %.2f %.2f",
                       recipe.key.c_str(), static_cast<unsigned long long>(instance.generation), stage.name.c_str(),
                       proximity ? "area trigger" : "member that ends itself", short_slice(member.slice).c_str(),
                       static_cast<double>(place.x), static_cast<double>(place.y), static_cast<double>(place.z));
            continue;
        }
        if (member.kind == gamedata::EncounterMemberKind::creature && placement.kinds[spawn.memberIndex] != nullptr) {
            state::WorldPosition stand = place;
            if (!recipe.mode.empty()) {
                // Stage QG9c [agent; live: Lady Asena of QuestWolf at 4255.0 6079.0 63.0, 6.7 m above the floor of the cave: the client showed
                // no body, and no hit reached her]: a creature member of an instance recipe whose spawn location is above the floor of
                // the data (the start of the intro of a boss tree, which jumps down with Phase_Intro or Pounce: the tree is not in the
                // client) stands on the floor below that location.
                if (const std::optional<float> floor = m_world.navigation().ground_height(place);
                    floor && place.z - *floor > encounter_rules::kAboveFloor) {
                    stand.z = *floor;
                    ++m_counts.membersLowered;
                    log::write("world: encounters: the encounter %s puts the creature %s on the floor below its spawn location (%.2f m lower)",
                               recipe.key.c_str(), short_slice(member.slice).c_str(), static_cast<double>(place.z - *floor));
                }
            }
            entity = m_world.creatures().spawn_member(*placement.kinds[spawn.memberIndex], stand, yaw, instance.placement, instance.generation,
                                                      nullptr, instance.space);
            if (entity != nullptr) {
                ++m_counts.membersMade;
            }
        } else if (member.kind == gamedata::EncounterMemberKind::object && placement.templates[spawn.memberIndex] != nullptr) {
            const gamedata::EncounterObject& object = *placement.templates[spawn.memberIndex];
            const std::string key = recipe.key + "/" + std::to_string(instance.generation) + "/" + std::to_string(index) + "/"
                                    + std::to_string(entry) + "/" + std::to_string(number);
            const Interactions::SpawnedContainer child =
                m_world.interactions().spawn_container(object.templateIndex, place, yaw, key, instance.space);
            entity = child.entity;
            if (entity != nullptr) {
                // The variation of the slice (the VariationData state): without it the client shows no prompt (live check L3).
                entity->object.variationSlot = object.variationSlot;
                entity->object.variant = object.variant;
                m_world.touch(entity->id, ComponentKey::variation);
                m_world.gathering().encounter_object_made(*entity, member.slice);
                if (member.proximity) {
                    // Stage QG5c3: a proximity object (the start of an ambush): no GatherOption counts it; a character in its sphere finds it.
                    m_proximity[entity->id] = Proximity{instance.placement, instance.generation, place, &member, {}, {}};
                } else {
                    m_objects[entity->id] = member.slice;
                }
                ++m_counts.objectsMade;
            }
        } else {
            // A visual of the stage that the backend does not make, or (stage QG5c3) a creature member that the client does not show in a
            // stage that does not need it (resolve).
            ++m_counts.spawnsSkipped;
            continue;
        }
        if (entity == nullptr) {
            log::write("world: encounters: the encounter %s did not make its member %s at %.2f %.2f %.2f", recipe.key.c_str(),
                       short_slice(member.slice).c_str(), static_cast<double>(place.x), static_cast<double>(place.y),
                       static_cast<double>(place.z));
            continue;
        }
        instance.members[entity->id] =
            EncounterMemberRun{index, entry, member.kind == gamedata::EncounterMemberKind::object, false, member.proximity, false};
        if (member.kind == gamedata::EncounterMemberKind::creature && !member.phases.empty() && !recipe.mode.empty()) {
            // Stage QG9b: a boss with the phases of its BossPhaseComponent starts in its first phase (a wave that ended already ends it at the
            // next look).
            instance.members[entity->id].phase = 0;
            schedule(m_world.clock().now(), Due{DueKind::phases, instance.placement, instance.generation, index, 0, 0});
        }
        m_memberOf[entity->id] = {instance.placement, instance.generation};
        ++run.spawned;
        ++made;
        madeParts.push_back(-1);
        if (member.kind == gamedata::EncounterMemberKind::creature && recipe.overrideAggroDelay) {
            // m_overrideAggroDelay of the manager (game data; the AITargetCollector of the root): the member takes the nearest participant
            // as its target after m_aggroDelayOverride (0 s in the recipes of the stage: at once).
            if (recipe.aggroDelay > 0.0F) {
                const auto delay = std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(recipe.aggroDelay * 1000.0F)));
                schedule(m_world.clock().now() + delay, Due{DueKind::aggro, instance.placement, instance.generation, index, 0, entity->id});
            } else {
                provoke(instance, *entity);
            }
        }
        log::write("world: encounters: the encounter %s (generation %llu, stage %s) makes the %s %s: entity %016llx at %.2f %.2f %.2f",
                   recipe.key.c_str(), static_cast<unsigned long long>(instance.generation), stage.name.c_str(),
                   member_kind_text(member.kind), short_slice(member.slice).c_str(), static_cast<unsigned long long>(entity->id),
                   static_cast<double>(place.x), static_cast<double>(place.y), static_cast<double>(place.z));
    }
    // Stage QG5c3: a spawned objective (SliceSpawnedObjective: the spawn of Helvan) of the stage or of a stage above it counts the members of
    // its tracked slice that came; the stages that reached their count end (the innermost first). Stage QG5c4: each spawned objective of
    // those stages.
    if (made == 0) {
        return;
    }
    const std::uint32_t placementIndex = instance.placement;
    const std::uint64_t generation = instance.generation;
    std::vector<std::pair<std::uint32_t, std::size_t>> counting;
    for (std::int32_t at = static_cast<std::int32_t>(index); at >= 0; at = recipe.stages[static_cast<std::size_t>(at)].parent) {
        const gamedata::EncounterStage& stage = recipe.stages[static_cast<std::size_t>(at)];
        for (std::size_t number = 0; number <= stage.more.size(); ++number) {
            const gamedata::EncounterObjective& objective = objective_of(stage, number);
            if (objective.kind == gamedata::EncounterObjectiveKind::spawned && counts_toward(objective, spawn)) {
                counting.emplace_back(static_cast<std::uint32_t>(at), number);
            }
        }
    }
    for (const auto& [at, number] : counting) {
        EncounterInstance* current = instance_of(placementIndex, generation);
        if (current == nullptr || current->phase != EncounterPhase::running) {
            return;
        }
        // Stage QG5c5: each made member (each made part of a group) that the objective counts.
        const gamedata::EncounterObjective& objective = objective_of(recipe.stages[at], number);
        const auto added = static_cast<std::uint32_t>(std::count_if(madeParts.begin(), madeParts.end(), [&](std::int32_t part) {
            return counts_toward(objective, spawn, part);
        }));
        if (added > 0) {
            count_objective(*current, at, number, added);
        }
    }
}

bool Encounters::spawn_needed(const EncounterPlacement& placement, std::uint32_t index, const gamedata::EncounterSpawn& spawn) const {
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    for (std::int32_t at = static_cast<std::int32_t>(index); at >= 0; at = recipe.stages[static_cast<std::size_t>(at)].parent) {
        const gamedata::EncounterStage& stage = recipe.stages[static_cast<std::size_t>(at)];
        for (std::size_t number = 0; number <= stage.more.size(); ++number) {
            const gamedata::EncounterObjective& objective = objective_of(stage, number);
            const bool voided = static_cast<std::size_t>(at) < placement.voided.size() && number < placement.voided[static_cast<std::size_t>(at)].size()
                                && placement.voided[static_cast<std::size_t>(at)][number];
            if ((objective.kind == gamedata::EncounterObjectiveKind::destroyed || objective.kind == gamedata::EncounterObjectiveKind::spawned)
                && !voided && counts_toward(objective, spawn)) {
                return true;
            }
        }
    }
    return false;
}

void Encounters::provoke(EncounterInstance& instance, Entity& member) {
    const Entity* nearest = nullptr;
    for (const auto& [characterId, participant] : instance.participants) {
        const Entity* character = m_world.find_character(characterId);
        if (character != nullptr && !character->life.dead
            && (nearest == nullptr || distance3(character->position, member.position) < distance3(nearest->position, member.position))) {
            nearest = character;
        }
    }
    if (nearest != nullptr) {
        m_world.creatures().behavior().provoke(member, nearest->id);
    }
}

void Encounters::member_died(const Entity& creature) {
    if (creature.kind != EntityKind::creature || !creature.creature.encounter) {
        return;
    }
    m_events.push_back(Event{true, creature.id, creature.creature.encounter->first, creature.creature.encounter->second, {}});
}

void Encounters::object_gathered(EntityId object, const Entity& character) {
    const auto found = m_memberOf.find(object);
    if (found == m_memberOf.end()) {
        ++m_counts.oldGeneration; // An old interaction: the object of an instance that went away.
        return;
    }
    m_events.push_back(Event{false, object, found->second.first, found->second.second, character.characterId});
}

const std::string* Encounters::start_object(EntityId object) const {
    const auto found = m_objects.find(object);
    return found != m_objects.end() ? &found->second : nullptr;
}

void Encounters::take_events() {
    std::vector<Event> events;
    events.swap(m_events);
    for (const Event& event : events) {
        EncounterInstance* instance = instance_of(event.placement, event.generation);
        if (instance == nullptr || instance->members.count(event.entity) == 0) {
            ++m_counts.oldGeneration;
            log::write("world: encounters: a %s of the old generation %llu of the encounter %s changes nothing",
                       event.death ? "death" : "gather", static_cast<unsigned long long>(event.generation),
                       event.placement < m_placements.size() ? m_placements[event.placement].recipe->key.c_str() : "?");
            continue;
        }
        if (event.death) {
            ++m_counts.memberDeaths;
        } else {
            // The gather of a start object (the stage tracks its slice): the object goes, and the stage counts it (SliceDestroyedObjective).
            ++m_counts.objectsGathered;
            log::write("world: encounters: character %s used the start object %016llx of the encounter %s", event.character.c_str(),
                       static_cast<unsigned long long>(event.entity), m_placements[event.placement].recipe->key.c_str());
            m_objects.erase(event.entity);
            m_memberOf.erase(event.entity);
            m_world.interactions().remove_object(event.entity);
        }
        count_destroyed(*instance, event.entity);
    }
}

void Encounters::count_destroyed(EncounterInstance& instance, EntityId member) {
    const auto found = instance.members.find(member);
    if (found == instance.members.end() || found->second.done) {
        return;
    }
    found->second.done = true;
    found->second.destroyed = true;
    if (instance.phase != EncounterPhase::running) {
        return;
    }
    const EncounterPlacement& placement = m_placements[instance.placement];
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    const gamedata::EncounterSpawn& spawn = recipe.stages[found->second.stage].spawns[found->second.spawn];
    const std::int32_t part = found->second.part;
    const bool death = !found->second.object && !found->second.noEntity;
    // The objectives that count it (stage QG5c3): its stage and each stage above it that tracks it (a parent counts the spawns of its child
    // stages), the innermost first; and each stage that counts each death of a member of the instance (Kill Helvan Boss). Stage QG5c4: each
    // destroyed objective of those stages that tracks it.
    std::vector<std::pair<std::uint32_t, std::size_t>> counting;
    for (std::int32_t at = static_cast<std::int32_t>(found->second.stage); at >= 0; at = recipe.stages[static_cast<std::size_t>(at)].parent) {
        const gamedata::EncounterStage& stage = recipe.stages[static_cast<std::size_t>(at)];
        for (std::size_t number = 0; number <= stage.more.size(); ++number) {
            const gamedata::EncounterObjective& objective = objective_of(stage, number);
            if (objective.kind == gamedata::EncounterObjectiveKind::destroyed && counts_toward(objective, spawn, part)) {
                counting.emplace_back(static_cast<std::uint32_t>(at), number);
            }
        }
    }
    // Stage QG5c5: a running stage whose destroyed objective tracks the slice of a start object of an earlier stage (its stage ended) counts
    // it too (the spyglass of Scallywag Shore, spawned by the timed setup and tracked by "Look through Spyglass") [data: the stage tracks a
    // slice that only the earlier stage spawns; agent: the rule, only for a start object].
    // Stage QG9a: in an instance recipe a creature member of a manager stage that still runs in the background (a WaitForParentObjective
    // stage of the manager: the caster of the setup of Medea) counts for each running stage that tracks it too.
    // Stage QG9b: in an instance recipe each member counts for each running stage that tracks it (the stages below its stage and the later
    // stages: the seraph of Boss_Encounter for Wave 2 - Defeat Seraph, the werewolf lord of _Wave 0 - Wait for _Wave 2 - Fight; it covers the
    // background setup of stage QG9a).
    const bool background = !recipe.mode.empty();
    for (std::uint32_t index = 0; ((found->second.object && instance.stages[found->second.stage].ended) || background) && index < recipe.stages.size();
         ++index) {
        const EncounterStageRun& run = instance.stages[index];
        if (!run.started || run.ended) {
            continue;
        }
        const gamedata::EncounterStage& stage = recipe.stages[index];
        for (std::size_t number = 0; number <= stage.more.size(); ++number) {
            const gamedata::EncounterObjective& objective = objective_of(stage, number);
            const std::pair<std::uint32_t, std::size_t> entry{index, number};
            if (objective.kind == gamedata::EncounterObjectiveKind::destroyed && !objective.tracked.empty()
                && counts_toward(objective, spawn, part) && std::find(counting.begin(), counting.end(), entry) == counting.end()) {
                counting.push_back(entry);
            }
        }
    }
    if (death && found->second.phase >= 0) {
        schedule(m_world.clock().now(), Due{DueKind::phases, instance.placement, instance.generation, found->second.stage, 0, 0});
    }
    for (std::uint32_t index = 0; death && index < placement.anyMember.size(); ++index) {
        const std::pair<std::uint32_t, std::size_t> first{index, 0};
        if (placement.anyMember[index] && std::find(counting.begin(), counting.end(), first) == counting.end()) {
            counting.push_back(first);
        }
    }
    // Stage QG5c5: a death counts only for the stages that ran when it came: the end of a sub-wave starts the next child stage (a
    // transition that counts each death of a member) in this loop, and that stage does not count the same death (live L2: "1/3").
    std::vector<bool> running(instance.stages.size(), false);
    for (std::size_t index = 0; index < running.size(); ++index) {
        running[index] = instance.stages[index].started && !instance.stages[index].ended;
    }
    const std::uint32_t placementIndex = instance.placement;
    const std::uint64_t generation = instance.generation;
    for (const auto& [index, number] : counting) {
        EncounterInstance* current = instance_of(placementIndex, generation);
        if (current == nullptr || current->phase != EncounterPhase::running) {
            return; // An earlier stage of the chain ended the encounter.
        }
        if (index < running.size() && running[index]) {
            count_objective(*current, index, number, 1);
        }
    }
}

void Encounters::count_objective(EncounterInstance& instance, std::uint32_t index, std::size_t number, std::uint32_t added) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    EncounterStageRun& run = instance.stages[index];
    if (!run.started || run.ended || number >= run.counts.size() || run.met[number]) {
        return;
    }
    const gamedata::EncounterObjective& objective = objective_of(stage, number);
    run.counts[number] += added;
    if (number == 0) {
        (objective.kind == gamedata::EncounterObjectiveKind::spawned ? run.spawnedTracked : run.destroyed) = run.counts[0];
    }
    log::write("world: encounters: the encounter %s (stage %s, objective %zu): %u of %u %s", recipe.key.c_str(), stage.name.c_str(),
               number + 1, run.counts[number], objective.requiredCount,
               objective.kind == gamedata::EncounterObjectiveKind::spawned ? "spawned" : "destroyed");
    if (run.counts[number] >= objective.requiredCount) {
        run.met[number] = true;
    }
    // Code review of stage QG5c4, finding QG5C4R-R5: a spawned objective publishes its count too (a met spawned objective of a stage that
    // waits for an other objective must reach the HUD of each observer).
    publish_objectives(instance);
    if (std::all_of(run.met.begin(), run.met.end(), [](bool value) { return value; })) {
        objective_met(instance, index);
    }
}

void Encounters::objective_met(EncounterInstance& instance, std::uint32_t index) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    EncounterStageRun& run = instance.stages[index];
    if (!run.started || run.ended) {
        return;
    }
    run.objectiveMet = true;
    // Stage QG9b: an entity-state wait of the rule "after" waits for its child stages also when they are optional (the trigger of the
    // Isabella intro: its parent waits for the change that the dialogue after it sends) [agent].
    const bool waits = !stage.childrenOptional || stage.objective.end == gamedata::StageEnd::after;
    if (!stage.children.empty() && waits && !run.childrenDone && !children_done(instance, index)) {
        // m_childStagesOptional 0 [data: the name; agent: the rule]: the stage ends when its count came and its last child stage ended.
        // Stage QG5c5: a child stage with a WaitForParentObjective ends with its parent; the parent does not wait for it.
        log::write("world: encounters: the encounter %s (stage %s) has its count and waits for its child stages", recipe.key.c_str(),
                   stage.name.c_str());
        return;
    }
    end_stage(instance, index, true);
}

void Encounters::take_due() {
    const WorldClock::time_point now = m_world.clock().now();
    std::vector<Due> due;
    const auto end = m_due.upper_bound(now);
    for (auto entry = m_due.begin(); entry != end; ++entry) {
        due.push_back(entry->second);
    }
    m_due.erase(m_due.begin(), end);
    for (const Due& entry : due) {
        ++m_counts.dueTaken;
        if (entry.kind == DueKind::payout) {
            pay(entry.payout);
            continue;
        }
        EncounterInstance* instance = instance_of(entry.placement, entry.generation);
        if (instance == nullptr) {
            ++m_counts.oldGeneration; // An old timer: its instance ended.
            continue;
        }
        switch (entry.kind) {
        case DueKind::activate:
            activate(*instance);
            break;
        case DueKind::spawn:
            spawn(*instance, entry.stage, entry.spawn, entry.payout);
            break;
        case DueKind::stageTime: {
            const gamedata::EncounterStage& stage = m_placements[entry.placement].recipe->stages[entry.stage];
            if (instance->phase == EncounterPhase::running && instance->stages[entry.stage].started && !instance->stages[entry.stage].ended) {
                end_stage(*instance, entry.stage, stage.objective.succeedOnTimeout);
            }
            break;
        }
        case DueKind::aggro:
            if (Entity* member = m_world.find(static_cast<EntityId>(entry.payout)); member != nullptr && instance->members.count(member->id) != 0) {
                provoke(*instance, *member);
            }
            break;
        case DueKind::memberEnd:
            // Stage QG5c3: a member that ends itself (the smoke burst 4.2 s, the powder keg 13.15 s after its spawn) counts as destroyed.
            if (const auto member = instance->members.find(static_cast<EntityId>(entry.payout));
                member != instance->members.end() && !member->second.done) {
                ++m_counts.timedEnds;
                log::write("world: encounters: the member %016llx of the encounter %s ended itself",
                           static_cast<unsigned long long>(entry.payout), m_placements[entry.placement].recipe->key.c_str());
                count_destroyed(*instance, member->first);
            }
            break;
        case DueKind::eventMet:
            if (instance->phase == EncounterPhase::running && instance->stages[entry.stage].started && !instance->stages[entry.stage].ended) {
                log::write("world: encounters: the encounter %s: the event %s of the stage %s is met at its start (no sender of the data)",
                           m_placements[entry.placement].recipe->key.c_str(),
                           m_placements[entry.placement].recipe->stages[entry.stage].objective.eventId.c_str(),
                           m_placements[entry.placement].recipe->stages[entry.stage].name.c_str());
                // Code review of stage QG9b, finding QG9BR-R2: the met event is the count of the stage; the stage ends by the rule of its
                // required child stages (objective_met), as the end rule does. A direct end cut the timed wait of the QuestWolf intro and
                // never started its user wave 1. Code review of stage QG9c, finding QG9CR-R4: only the event objective is met; an other
                // objective of the stage (a destroyed objective of `more`) keeps its count, and the stage ends when each one is met (the
                // rule of count_objective).
                EncounterStageRun& run = instance->stages[entry.stage];
                if (!run.met.empty()) {
                    run.met[0] = true;
                }
                publish_objectives(*instance);
                if (std::all_of(run.met.begin(), run.met.end(), [](bool value) { return value; })) {
                    objective_met(*instance, entry.stage);
                }
            }
            break;
        case DueKind::endRule:
            if (instance->phase == EncounterPhase::running && instance->stages[entry.stage].started && !instance->stages[entry.stage].ended) {
                const gamedata::EncounterStage& stage = m_placements[entry.placement].recipe->stages[entry.stage];
                log::write("world: encounters: the encounter %s: the stage %s ends by the end rule %s (no sender of the data)",
                           m_placements[entry.placement].recipe->key.c_str(), stage.name.c_str(),
                           stage.objective.end == gamedata::StageEnd::after ? "after its spawns and its child stages"
                                                                            : "after its spawns (a stage with nothing to wait for)");
                EncounterStageRun& run = instance->stages[entry.stage];
                if (!run.met.empty()) {
                    run.met.assign(run.met.size(), true);
                }
                publish_objectives(*instance);
                objective_met(*instance, entry.stage);
            }
            break;
        case DueKind::phases:
            advance_phases(*instance);
            break;
        case DueKind::payout:
            break;
        }
    }
}

void Encounters::count_earlier_deaths(EncounterInstance& instance, std::uint32_t index) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    // Code review of stage QG9b, finding QG9BR-R1: a member of the earlier deaths comes from a parent of the stage, or from a stage whose own
    // objectives do not count it (the setup of the manager, the background setup of a boss). A member of an earlier wave that counted it
    // (the soldiers of the Phase 1.5 Burrowing of QuestApophis) is not a member of this stage [agent: the data has no owner of a member; the
    // count of a wave is the deaths of its own spawns].
    // Code review of stage QG9c, finding QG9CR-R3: also a wave that counts the member through a child setup of its own (the source stage
    // and each stage above it that is not a parent of this stage: a destroyed objective of one of them that counts the member consumed its
    // death). A parent of this stage and the setup of the manager still supply it.
    const auto parent_of_stage = [&](std::uint32_t candidate) {
        for (std::int32_t parent = stage.parent; parent >= 0; parent = recipe.stages[static_cast<std::uint32_t>(parent)].parent) {
            if (static_cast<std::uint32_t>(parent) == candidate) {
                return true;
            }
        }
        return false;
    };
    const auto supplies = [&](const EncounterMemberRun& member) {
        if (parent_of_stage(member.stage) || member.stage == index) {
            return true;
        }
        const gamedata::EncounterSpawn& spawn = recipe.stages[member.stage].spawns[member.spawn];
        for (std::int32_t owner = static_cast<std::int32_t>(member.stage); owner >= 0;
             owner = recipe.stages[static_cast<std::uint32_t>(owner)].parent) {
            if (static_cast<std::uint32_t>(owner) == index || parent_of_stage(static_cast<std::uint32_t>(owner))) {
                break; // A stage of the path of this stage: its objectives do not consume the death for this stage.
            }
            const gamedata::EncounterStage& source = recipe.stages[static_cast<std::uint32_t>(owner)];
            for (std::size_t number = 0; number <= source.more.size(); ++number) {
                const gamedata::EncounterObjective& own = objective_of(source, number);
                if (own.kind == gamedata::EncounterObjectiveKind::destroyed && counts_toward(own, spawn, member.part)) {
                    return false;
                }
            }
        }
        return true;
    };
    std::vector<std::pair<std::size_t, std::uint32_t>> found;
    for (std::size_t number = 0; number <= stage.more.size(); ++number) {
        const gamedata::EncounterObjective& objective = objective_of(stage, number);
        if (objective.kind != gamedata::EncounterObjectiveKind::destroyed || objective.tracked.empty()) {
            continue;
        }
        std::uint32_t dead = 0;
        for (const auto& [id, member] : instance.members) {
            if (member.destroyed && member.stage < recipe.stages.size() && member.spawn < recipe.stages[member.stage].spawns.size()
                && supplies(member) && counts_toward(objective, recipe.stages[member.stage].spawns[member.spawn], member.part)) {
                ++dead;
            }
        }
        if (dead > 0) {
            found.emplace_back(number, dead);
        }
    }
    const std::uint32_t placement = instance.placement;
    const std::uint64_t generation = instance.generation;
    for (const auto& [number, dead] : found) {
        EncounterInstance* current = instance_of(placement, generation);
        if (current == nullptr || current->phase != EncounterPhase::running || current->stages[index].ended) {
            return;
        }
        log::write("world: encounters: the encounter %s (stage %s) counts %u tracked member(s) that died before its start", recipe.key.c_str(),
                   stage.name.c_str(), dead);
        count_objective(*current, index, number, dead);
    }
}

void Encounters::advance_phases(EncounterInstance& instance) {
    // Stage QG9b (docs/systems/encounters.md, the boss phases): the phases of the BossPhaseComponent of a boss in the order of its list; the
    // current phase ends at the end of the user wave m_waveNumber of its encounter (WaveEndedBossPhaseConfig) or at its health percent and
    // below (HealthThresholdBossPhaseConfig; a dead boss is at 0), and the next phase starts [data: the configs and their fields]. A phase of
    // an other config never ends. A running BossPhaseObjective stage is met when the phase that it tracks ended.
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    for (auto& [id, member] : instance.members) {
        if (member.phase < 0 || member.noEntity || member.stage >= recipe.stages.size()
            || member.spawn >= recipe.stages[member.stage].spawns.size()) {
            continue;
        }
        const gamedata::EncounterSpawn& spawn = recipe.stages[member.stage].spawns[member.spawn];
        if (spawn.memberIndex >= m_data->members().size()) {
            continue;
        }
        const std::vector<gamedata::BossPhase>& phases = m_data->members()[spawn.memberIndex].phases;
        const Entity* creature = m_world.find(id);
        while (static_cast<std::size_t>(member.phase) < phases.size()) {
            const gamedata::BossPhase& phase = phases[static_cast<std::size_t>(member.phase)];
            bool ends = false;
            if (phase.kind == gamedata::BossPhase::Kind::wave) {
                ends = instance.wavesEnded.count(static_cast<std::int32_t>(phase.wave)) != 0;
            } else if (phase.kind == gamedata::BossPhase::Kind::health) {
                const bool dead = member.destroyed || (creature != nullptr && creature->kind == EntityKind::creature && creature->creature.dead);
                ends = dead || (creature != nullptr && creature->vitals.healthMax > 0.0F
                                && creature->vitals.health * 100.0F <= phase.health * creature->vitals.healthMax);
            }
            if (!ends) {
                break;
            }
            log::write("world: encounters: the encounter %s: the boss %016llx ends its phase %d (%s, %s)", recipe.key.c_str(),
                       static_cast<unsigned long long>(id), member.phase,
                       phase.kind == gamedata::BossPhase::Kind::wave ? ("the end of the wave " + std::to_string(phase.wave)).c_str()
                                                                     : ("the health " + std::to_string(phase.health) + "%").c_str(),
                       phase.key.c_str());
            ++member.phase;
        }
    }
    std::vector<std::uint32_t> met;
    for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
        const gamedata::EncounterObjective& objective = recipe.stages[index].objective;
        const EncounterStageRun& run = instance.stages[index];
        if (objective.kind != gamedata::EncounterObjectiveKind::bossPhase || !run.started || run.ended || run.met.empty() || run.met[0]) {
            continue;
        }
        for (const auto& [id, member] : instance.members) {
            if (member.phase > objective.phaseIndex && member.stage < recipe.stages.size()
                && member.spawn < recipe.stages[member.stage].spawns.size()
                && recipe.stages[member.stage].spawns[member.spawn].member == objective.phaseOf) {
                met.push_back(index);
                break;
            }
        }
    }
    const std::uint32_t placement = instance.placement;
    const std::uint64_t generation = instance.generation;
    for (const std::uint32_t index : met) {
        EncounterInstance* current = instance_of(placement, generation);
        if (current == nullptr || current->phase != EncounterPhase::running || current->stages[index].ended) {
            return;
        }
        log::write("world: encounters: the encounter %s: the boss ended the phase %d that the stage %s tracks", recipe.key.c_str(),
                   recipe.stages[index].objective.phaseIndex, recipe.stages[index].name.c_str());
        EncounterStageRun& run = current->stages[index];
        run.met[0] = true;
        publish_objectives(*current);
        if (std::all_of(run.met.begin(), run.met.end(), [](bool value) { return value; })) {
            objective_met(*current, index);
        }
    }
}

void Encounters::look_phases() {
    // Stage QG9b: the health phases (the cost: the members of the running instances with a boss phase; MP-8).
    std::vector<std::pair<std::uint32_t, std::uint64_t>> looked;
    for (const auto& [root, instance] : m_instances) {
        if (instance.phase == EncounterPhase::running
            && std::any_of(instance.members.begin(), instance.members.end(), [](const auto& entry) { return entry.second.phase >= 0; })) {
            looked.emplace_back(instance.placement, instance.generation);
        }
    }
    for (const auto& [placement, generation] : looked) {
        if (EncounterInstance* instance = instance_of(placement, generation)) {
            advance_phases(*instance);
        }
    }
}

void Encounters::end_stage(EncounterInstance& instance, std::uint32_t index, bool success) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    instance.stages[index].ended = true;
    stage_status(instance, index, success ? encounter_rules::kStageSucceeded : encounter_rules::kStageFailed);
    if (stage.userIndex >= 0 && !recipe.mode.empty() && success) {
        // Stage QG9b: the success of a user wave ends the WaveEnded boss phase of that wave (a look in the next tick) [client 0x25DB940: the
        // wave number and the success; 0x26ED922: a user-wave stage sends it; static read F of stage QG9b, F1].
        instance.wavesEnded.insert(stage.userIndex);
        schedule(m_world.clock().now(), Due{DueKind::phases, instance.placement, instance.generation, index, 0, 0});
    }
    log::write("world: encounters: the encounter %s (generation %llu): the stage %s %s", recipe.key.c_str(),
               static_cast<unsigned long long>(instance.generation), stage.name.c_str(), success ? "succeeded" : "failed");
    // Stage QG5c3: the child stages that run end with their parent (the status of the parent) [agent].
    std::vector<std::uint32_t> pending(stage.children.begin(), stage.children.end());
    while (!pending.empty()) {
        const std::uint32_t child = pending.back();
        pending.pop_back();
        EncounterStageRun& run = instance.stages[child];
        if (run.started && !run.ended) {
            run.ended = true;
            stage_status(instance, child, success ? encounter_rules::kStageSucceeded : encounter_rules::kStageFailed);
            if (recipe.stages[child].userIndex >= 0 && !recipe.mode.empty() && success) {
                instance.wavesEnded.insert(recipe.stages[child].userIndex); // Stage QG9b: the wave of a boss phase.
            }
            // Code review of stage QG5c5, finding QG5C5R-R4: the end actions of the child run one time when its parent ends it (the client
            // runs them at the end status too). No start of a sibling: the parent ends.
            if (recipe.stages[child].killSpawnsAtEnd) {
                remove_members(instance, false, true);
            }
            destroy_slices(instance, recipe.stages[child].destroyAtEnd);
        }
        pending.insert(pending.end(), recipe.stages[child].children.begin(), recipe.stages[child].children.end());
    }
    if (stage.killSpawnsAtEnd) {
        remove_members(instance, false, true);
    }
    destroy_slices(instance, stage.destroyAtEnd);
    publish_objectives(instance);
    if (stage.parent >= 0) {
        // Stage QG5c3: a child stage ended. A failure of a child that is not optional fails its parent; else the next child starts, or the
        // parent ends when it has its count too (a timed parent ends at its time).
        const auto parentIndex = static_cast<std::uint32_t>(stage.parent);
        const gamedata::EncounterStage& parent = recipe.stages[parentIndex];
        EncounterStageRun& parentRun = instance.stages[parentIndex];
        if (!parentRun.started || parentRun.ended) {
            return;
        }
        if (!success && !parent.childrenOptional) {
            end_stage(instance, parentIndex, false);
            return;
        }
        if (parentRun.nextChild < parent.children.size()) {
            const std::uint32_t next = parent.children[parentRun.nextChild++];
            const std::uint32_t placementIndex = instance.placement;
            const std::uint64_t generation = instance.generation;
            start_stage(instance, next);
            // Stage QG5c5: the next child waits for its parent (WaitForParentObjective): a parent that has its count ends now.
            if (recipe.stages[next].objective.kind == gamedata::EncounterObjectiveKind::waitParent && instance_of(placementIndex, generation) != nullptr
                && parentRun.objectiveMet && !parentRun.ended && children_done(instance, parentIndex)) {
                end_stage(instance, parentIndex, true);
            }
            return;
        }
        parentRun.childrenDone = true;
        if (parentRun.objectiveMet) {
            end_stage(instance, parentIndex, true);
        }
        return;
    }
    if (!success) {
        instance.phase = EncounterPhase::failed;
        manager_status(instance, encounter_rules::kFailed);
        end_instance(instance.root, EncounterEnd::failed);
        return;
    }
    ++instance.current;
    if (instance.current < recipe.stageOrder.size()) {
        start_stage(instance, recipe.stageOrder[instance.current]);
        return;
    }
    succeed(instance);
}

void Encounters::succeed(EncounterInstance& instance) {
    // Code review of stage QG5c2, finding QG5C2R-R1: the participants of the success are the ones of now (a grace that ended since the last
    // scan ends the participant before the snapshot; the deaths and the timers of a tick come before its scan).
    update_participants(instance);
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    instance.phase = EncounterPhase::succeeded;
    manager_status(instance, encounter_rules::kSucceeded);
    ++m_counts.succeeded;
    if (!recipe.successEvent.empty()) {
        // The credit (docs/systems/encounters.md): a snapshot of the participants and the generation; the event of the recipe goes one time
        // to each of them. The ledger keeps a recipient until its event is in the store.
        EncounterPayout payout;
        payout.placement = instance.placement;
        payout.generation = instance.generation;
        payout.event = recipe.successEvent;
        payout.result = 0xE4C0000000000000ULL ^ (static_cast<std::uint64_t>(instance.placement) << 32) ^ instance.generation;
        for (const auto& [characterId, participant] : instance.participants) {
            payout.pending.push_back(characterId);
        }
        log::write("world: encounters: the encounter %s (generation %llu) succeeded: the event %s for %zu participant(s)",
                   recipe.key.c_str(), static_cast<unsigned long long>(instance.generation), recipe.successEvent.c_str(),
                   payout.pending.size());
        m_payouts.push_back(std::move(payout));
        pay(m_payouts.size() - 1);
    } else {
        log::write("world: encounters: the encounter %s (generation %llu) succeeded (no success event of the recipe)", recipe.key.c_str(),
                   static_cast<unsigned long long>(instance.generation));
    }
    end_instance(instance.root, EncounterEnd::succeeded);
}

void Encounters::pay(std::size_t index) {
    if (index >= m_payouts.size() || m_payouts[index].pending.empty()) {
        return;
    }
    EncounterPayout& payout = m_payouts[index];
    ++payout.attempts;
    std::vector<std::string> pending;
    for (const std::string& characterId : payout.pending) {
        Entity* character = m_world.find_character(characterId);
        if (character == nullptr) {
            // A recipient that left the world gets no event (the backend has no event of a character that is not in the world).
            log::write("world: encounters: the participant %s of the encounter event %s is not in the world: no event", characterId.c_str(),
                       payout.event.c_str());
            continue;
        }
        QuestEvent event;
        event.kind = QuestEventKind::gameEvent;
        event.keys = {payout.event};
        event.result = payout.result;
        event.callerRetries = true;
        const auto matched = payout.matched.find(characterId);
        if (matched != payout.matched.end()) {
            event.matched = matched->second;
            event.restricted = true;
            event.attempt = payout.attempts - 1;
        }
        const QuestEventOutcome outcome = m_world.quests().event(*character, event);
        if (outcome.result == QuestEventResult::storeFailed) {
            if (matched == payout.matched.end()) {
                payout.matched[characterId] = outcome.matched;
            }
            pending.push_back(characterId);
            continue;
        }
        ++m_counts.credits;
        log::write("world: encounters: the event %s for character %s: %s", payout.event.c_str(), characterId.c_str(),
                   quest_event_result_text(outcome.result));
    }
    payout.pending = std::move(pending);
    if (payout.pending.empty()) {
        payout.matched.clear();
        return;
    }
    if (payout.attempts >= encounter_rules::kPayoutAttempts) {
        m_counts.creditsDropped += payout.pending.size();
        log::write("world: encounters: the store did not take the event %s for %zu participant(s) in %u attempts: no event",
                   payout.event.c_str(), payout.pending.size(), payout.attempts);
        payout.pending.clear();
        payout.matched.clear();
        return;
    }
    ++m_counts.creditRetries;
    schedule(m_world.clock().now() + encounter_rules::kPayoutRetry, Due{DueKind::payout, payout.placement, payout.generation, 0, 0, index});
}

void Encounters::destroy_slices(EncounterInstance& instance, const std::vector<gamedata::EncounterDestroy>& actions) {
    if (actions.empty()) {
        return;
    }
    // Stage QG5c5, DestroySliceAction [data: m_sliceAsset, m_aliasAsset of the action; agent: no count and no reward, as KillSpawnsAction]:
    // each member of the instance whose slice, alias, or body is the slice of the action, its alias, or a slice of its alias goes. A visual
    // that the runner did not make (the corruption hazard of the setup of a monolith) has no member: nothing goes.
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const auto named = [&](const gamedata::EncounterDestroy& action, const std::string& path) {
        return !path.empty()
               && (path == action.slice || path == action.alias
                   || std::find(action.aliasSlices.begin(), action.aliasSlices.end(), path) != action.aliasSlices.end());
    };
    std::vector<std::pair<EntityId, bool>> removed;
    for (auto& [entity, run] : instance.members) {
        if (run.done) {
            continue;
        }
        const gamedata::EncounterSpawn& spawn = recipe.stages[run.stage].spawns[run.spawn];
        const gamedata::EncounterMember* member = m_data->member(spawn.member);
        std::string body = member != nullptr ? member->body : std::string();
        if (member != nullptr && run.part >= 0 && static_cast<std::size_t>(run.part) < member->parts.size()) {
            body = member->parts[static_cast<std::size_t>(run.part)].body;
        }
        // Code review of stage QG5c5, finding QG5C5R-R2: the body that the runner selected for the entity (a part with a body alias has the
        // alias path as its body text, and its creature the slice of the alias that the spawn chose). Not the other slices of the alias.
        const Entity* live = run.noEntity ? nullptr : m_world.find(entity);
        const std::string selected = live != nullptr && live->kind == EntityKind::creature && live->creature.body != nullptr
                                         ? live->creature.body->path
                                         : std::string();
        const bool hit = std::any_of(actions.begin(), actions.end(), [&](const gamedata::EncounterDestroy& action) {
            // Code review of stage QG7, finding QG7R-R3: the selected body through each name of the action (its slice, its alias, and the
            // slices of its alias), with the rule of the other names.
            return named(action, spawn.member) || named(action, spawn.alias) || named(action, body) || (!selected.empty() && named(action, selected));
        });
        if (!hit) {
            continue;
        }
        run.done = true;
        if (run.noEntity) {
            m_proximity.erase(entity);
            continue;
        }
        if (m_memberOf.erase(entity) != 0) {
            removed.emplace_back(entity, run.object);
        }
    }
    for (const auto& [entity, object] : removed) {
        if (object) {
            m_objects.erase(entity);
            m_proximity.erase(entity);
            m_world.interactions().remove_object(entity);
        } else {
            m_world.creatures().remove_member(entity);
        }
        ++m_counts.slicesDestroyed;
        ++m_counts.membersRemoved;
    }
}

void Encounters::remove_members(EncounterInstance& instance, bool objectsToo, bool endedObjects) {
    std::vector<std::pair<EntityId, bool>> removed;
    for (auto& [entity, member] : instance.members) {
        // Stage QG5c4: a KillSpawnsAction removes the objects of the stages that ended too (the objects of the hold of the Disk of Atum at
        // its cleanup), not the objects of a stage that runs.
        if (member.object && !objectsToo && !(endedObjects && member.stage < instance.stages.size() && instance.stages[member.stage].ended)) {
            continue;
        }
        if (member.noEntity) {
            // Stage QG5c3: a member with no entity (an area trigger, a member that ends itself): it ends, with no count.
            member.done = true;
            m_proximity.erase(entity);
            continue;
        }
        if (m_memberOf.erase(entity) == 0) {
            continue; // Gone already.
        }
        removed.emplace_back(entity, member.object);
        member.done = true;
    }
    for (const auto& [entity, object] : removed) {
        if (object) {
            m_objects.erase(entity);
            m_proximity.erase(entity);
            m_world.interactions().remove_object(entity);
        } else {
            m_world.creatures().remove_member(entity);
        }
        ++m_counts.membersRemoved;
    }
}

void Encounters::end_instance(EntityId root, EncounterEnd end) {
    const auto found = m_instances.find(root);
    if (found == m_instances.end()) {
        return;
    }
    EncounterInstance instance = std::move(found->second);
    m_instances.erase(found);
    EncounterPlacement& placement = m_placements[instance.placement];
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    for (const auto& [characterId, participant] : instance.participants) {
        if (Entity* character = m_world.find_character(characterId)) {
            link(*character, root, false);
        }
    }
    // The root spawner has m_destroySpawnsOnDestroyed (game data): the members and the objects of the instance go with it.
    remove_members(instance, true);
    m_world.remove_encounter(root);
    m_byGeneration.erase({instance.placement, instance.generation});
    const WorldClock::time_point now = m_world.clock().now();
    if (instance.space != kMainSpace) {
        // Stage QG9a: an encounter of an instance: a success ends the recipe in its space (one run of the mode); a failure waits for a
        // character in the detector again (scan_spaces) [agent: a reset of the fight].
        m_spaceLive.erase({instance.placement, instance.space});
        if (end == EncounterEnd::succeeded) {
            m_spaceDone.insert({instance.placement, instance.space});
        } else if (end == EncounterEnd::failed) {
            ++m_counts.failed;
        }
        log::write("world: encounters: the encounter %s (generation %llu) of space %u ended (%s)", recipe.key.c_str(),
                   static_cast<unsigned long long>(instance.generation), static_cast<unsigned>(instance.space), encounter_end_text(end));
        if (end == EncounterEnd::succeeded) {
            m_world.instances().content_succeeded(instance.space);
        }
        return;
    }
    m_live.erase(instance.placement);
    placement.live = false;
    if (end == EncounterEnd::succeeded || end == EncounterEnd::failed) {
        // The parent spawner makes a new instance after its respawn time: a uniform draw of its respawn range (an area spawner, stage QG5c1
        // rule; the cooldowns of a point spawner).
        const float low = placement.recipe->parent.respawnMin;
        const float high = std::max(low, placement.recipe->parent.respawnMax);
        const float seconds = low + (high - low) * m_world.random_unit();
        placement.respawnAt = now + std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(seconds * 1000.0F)));
        if (end == EncounterEnd::failed) {
            ++m_counts.failed; // succeed() counts a success.
        }
        log::write("world: encounters: the encounter %s (generation %llu) ended (%s); a new instance in %.1f s", recipe.key.c_str(),
                   static_cast<unsigned long long>(instance.generation), encounter_end_text(end), static_cast<double>(seconds));
    } else {
        placement.respawnAt.reset();
        if (end == EncounterEnd::dormant) {
            ++m_counts.dormant;
        }
        log::write("world: encounters: the encounter %s (generation %llu) ended (%s)", recipe.key.c_str(),
                   static_cast<unsigned long long>(instance.generation), encounter_end_text(end));
    }
}

void Encounters::entity_removed(const Entity& entity) {
    const auto found = m_memberOf.find(entity.id);
    if (found != m_memberOf.end()) {
        // A member that a different path removed (a command): it is done, and it does not count as destroyed.
        if (EncounterInstance* instance = instance_of(found->second.first, found->second.second)) {
            if (auto member = instance->members.find(entity.id); member != instance->members.end()) {
                member->second.done = true;
            }
        }
        m_memberOf.erase(found);
        m_objects.erase(entity.id);
        m_proximity.erase(entity.id);
    }
}

void Encounters::character_left(const Entity& character) {
    for (auto& [root, instance] : m_instances) {
        if (instance.participants.erase(character.characterId) != 0) {
            ++m_counts.participantsLeft;
        }
    }
}

std::optional<EntityId> Encounters::use_start_object(Entity& character) {
    // DEVELOPMENT AID (the development routes only): the start object of a running stage near the character, as at the commit of its gather
    // (the game event of its row for the character, then the stage counts it). The rule of a gather (the prompt, the range, the time, the
    // tool) is not checked here.
    const Entity* nearest = nullptr;
    for (const auto& [object, slice] : m_objects) {
        const Entity* entity = m_world.find(object);
        if (entity != nullptr && horizontal_distance(entity->position, character.position) <= 10.0F
            && (nearest == nullptr || horizontal_distance(entity->position, character.position)
                                          < horizontal_distance(nearest->position, character.position))) {
            nearest = entity;
        }
    }
    if (nearest == nullptr) {
        return std::nullopt;
    }
    const gamedata::EncounterMember* member = m_data->member(m_objects.at(nearest->id));
    if (member != nullptr && !member->event.empty()) {
        QuestEvent event;
        event.kind = QuestEventKind::gameEvent;
        event.keys = {member->event};
        (void)m_world.quests().event(character, event);
    }
    log::write("world: encounters: DEVELOPMENT COMMAND: character %s uses the start object %016llx (no gather)", character.characterId.c_str(),
               static_cast<unsigned long long>(nearest->id));
    const EntityId id = nearest->id;
    object_gathered(id, character);
    return id;
}

bool Encounters::reset(const std::string& key) {
    const std::optional<std::uint32_t> index = placement_of(key);
    if (!index) {
        return false;
    }
    const auto live = m_live.find(*index);
    if (live == m_live.end()) {
        m_placements[*index].respawnAt.reset();
        return false;
    }
    end_instance(live->second, EncounterEnd::command);
    return true;
}

void Encounters::stage_status(EncounterInstance& instance, std::uint32_t stage, std::uint32_t status) {
    Entity* root = m_world.find(instance.root);
    if (root == nullptr || stage >= root->encounter.stages.size()) {
        return;
    }
    EncounterStageState& state = root->encounter.stages[stage];
    state.history.emplace_back(++state.sequence, status);
    if (state.history.size() > encounter_rules::kHistory) {
        state.history.erase(state.history.begin(), state.history.end() - static_cast<std::ptrdiff_t>(encounter_rules::kHistory));
    }
    m_world.touch(instance.root, ComponentKey::encounterStages);
}

void Encounters::manager_status(EncounterInstance& instance, std::uint32_t status) {
    Entity* root = m_world.find(instance.root);
    if (root == nullptr || root->encounter.managerStatus == status) {
        return;
    }
    root->encounter.managerStatus = status;
    m_world.touch(instance.root, ComponentKey::encounterManager);
}

void Encounters::publish_objectives(EncounterInstance& instance) {
    Entity* root = m_world.find(instance.root);
    if (root == nullptr) {
        return;
    }
    // The generated task tree of the client (static read B of stage QG5c2; static read D of stage QG5c4: 0x44650F0, 0x272E0E0, 0x272D660,
    // 0x3ADDEC0, 0x3AC0A22, 0x3AD6C60): a leaf and a wrapper of one child for each objective of a user-wave stage that makes a task
    // (EncounterObjective::leaf), then the outer consecutive container. A leaf is satisfied at its target (progress = target, state 2); its
    // wrapper at 1 of 1; the outer container counts the satisfied wrappers and moves the HUD row to the next one. A running objective: its
    // count (the N of "N / M" of a destroyed objective) and 0, state 1. State 2 with progress 0 does not satisfy a task (the HUD row of
    // Thorpe stayed at its first intermission in stage QG5c3).
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    std::vector<EncounterTaskState> tasks;
    std::uint32_t satisfied = 0;
    for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
        const gamedata::EncounterStage& stage = recipe.stages[index];
        const EncounterStageRun& run = instance.stages[index];
        if (!run.started) {
            continue;
        }
        for (std::size_t number = 0; number <= stage.more.size(); ++number) {
            const gamedata::EncounterObjective& objective = objective_of(stage, number);
            if (objective.leaf < 0) {
                continue;
            }
            const bool done = run.ended || (number < run.met.size() && run.met[number]);
            const bool counted = objective.kind == gamedata::EncounterObjectiveKind::destroyed
                                 || objective.kind == gamedata::EncounterObjectiveKind::spawned;
            const std::uint32_t count = number < run.counts.size() ? run.counts[number] : 0;
            const auto leaf = static_cast<std::uint8_t>(objective.leaf);
            tasks.push_back(EncounterTaskState{leaf, done ? objective.target : counted ? std::min(count, objective.target) : 0,
                                               done ? quest_state::kComplete : quest_state::kActive});
            tasks.push_back(EncounterTaskState{static_cast<std::uint8_t>(leaf + 1), done ? 1U : 0U,
                                               done ? quest_state::kComplete : quest_state::kActive});
            satisfied += done ? 1 : 0;
        }
    }
    if (recipe.taskLeaves > 0) {
        tasks.push_back(EncounterTaskState{static_cast<std::uint8_t>(2 * recipe.taskLeaves + 1), satisfied,
                                           satisfied >= recipe.taskLeaves ? quest_state::kComplete : quest_state::kActive});
    }
    std::sort(tasks.begin(), tasks.end(), [](const EncounterTaskState& a, const EncounterTaskState& b) { return a.index < b.index; });
    root->encounter.tasks = std::move(tasks);
    m_world.touch(instance.root, ComponentKey::encounterObjectives);
}

std::vector<EncounterView> Encounters::view(const state::WorldPosition& place, float radius) const {
    std::vector<EncounterView> out;
    for (const auto& [root, instance] : m_instances) {
        const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
        const float distance = horizontal_distance(place, recipe.position);
        if (distance > radius) {
            continue;
        }
        EncounterView view;
        view.key = recipe.key;
        view.slice = recipe.slice;
        view.root = root;
        view.generation = instance.generation;
        view.phase = instance.phase;
        view.distance = distance;
        if (instance.current < recipe.stageOrder.size()) {
            const std::uint32_t stage = recipe.stageOrder[instance.current];
            view.stage = recipe.stages[stage].name;
            view.destroyed = instance.stages[stage].destroyed;
            view.required = recipe.stages[stage].objective.requiredCount;
        }
        for (const auto& [characterId, participant] : instance.participants) {
            view.participants.push_back(characterId);
        }
        for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
            const EncounterStageRun& run = instance.stages[index];
            if (!run.started || run.ended) {
                continue;
            }
            std::string text = recipe.stages[index].name + ":";
            for (std::size_t number = 0; number < run.counts.size(); ++number) {
                text += (number == 0 ? " " : ", ") + std::to_string(run.counts[number]) + "/"
                        + std::to_string(objective_of(recipe.stages[index], number).requiredCount);
            }
            view.running.push_back(std::move(text));
        }
        for (const auto& [entity, member] : instance.members) {
            if (!member.done) {
                view.members.push_back(entity);
            }
        }
        out.push_back(std::move(view));
    }
    std::sort(out.begin(), out.end(), [](const EncounterView& a, const EncounterView& b) { return a.distance < b.distance; });
    return out;
}

namespace {

bool same_mode(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

bool Encounters::runs_mode(std::string_view mode) {
    if (!m_loaded && data() != nullptr) {
        load();
    }
    bool any = false;
    for (const EncounterPlacement& placement : m_placements) {
        const gamedata::EncounterRecipe& recipe = *placement.recipe;
        if (recipe.mode.empty() || recipe.daily || !same_mode(recipe.mode, mode)) {
            continue;
        }
        if (!placement.refused.empty()) {
            return false;
        }
        any = true;
    }
    return any;
}

std::size_t Encounters::start_space(SpaceId space, std::string_view mode) {
    if (!m_loaded && data() != nullptr) {
        load();
    }
    if (space == kMainSpace || !runs_mode(mode)) {
        return 0;
    }
    std::vector<std::uint32_t>& list = m_spaces[space];
    list.clear();
    for (std::uint32_t index = 0; index < m_placements.size(); ++index) {
        const gamedata::EncounterRecipe& recipe = *m_placements[index].recipe;
        if (!recipe.mode.empty() && !recipe.daily && same_mode(recipe.mode, mode) && m_placements[index].refused.empty()) {
            list.push_back(index);
            log::write("world: encounters: the encounter %s (%s) of the mode %s waits in space %u for a character in its detector",
                       recipe.key.c_str(), short_slice(recipe.slice).c_str(), recipe.mode.c_str(), static_cast<unsigned>(space));
        }
    }
    scan_soon();
    return list.size();
}

void Encounters::end_space(SpaceId space) {
    std::vector<EntityId> roots;
    for (const auto& [root, instance] : m_instances) {
        if (instance.space == space && space != kMainSpace) {
            roots.push_back(root);
        }
    }
    for (const EntityId root : roots) {
        end_instance(root, EncounterEnd::command);
    }
    const auto found = m_spaces.find(space);
    if (found != m_spaces.end()) {
        for (const std::uint32_t index : found->second) {
            m_spaceDone.erase({index, space});
        }
        m_spaces.erase(found);
    }
}

bool Encounters::space_done(SpaceId space) const {
    const auto found = m_spaces.find(space);
    if (found == m_spaces.end() || found->second.empty()) {
        return false;
    }
    // Stage QG9b [agent]: the recipes of a mode with more than one recipe start only for an active task (EncounterPlacement::gated): the
    // content succeeded when one of them succeeded and none runs (the seraph of the quest of the character; a later entry is a new run, and
    // its recipes start by the quest state of that time).
    if (m_placements[found->second.front()].gated) {
        return std::any_of(found->second.begin(), found->second.end(), [&](std::uint32_t index) { return m_spaceDone.contains({index, space}); })
               && std::none_of(found->second.begin(), found->second.end(),
                               [&](std::uint32_t index) { return m_spaceLive.contains({index, space}); });
    }
    return std::all_of(found->second.begin(), found->second.end(), [&](std::uint32_t index) { return m_spaceDone.contains({index, space}); });
}

void Encounters::hold_space(SpaceId space, bool held) {
    if (held) {
        m_heldSpaces.insert(space);
    } else if (m_heldSpaces.erase(space) != 0) {
        scan_soon();
    }
}

bool Encounters::mode_recipe_task(const Entity& character, SpaceId space) {
    const auto found = m_spaces.find(space);
    if (found == m_spaces.end()) {
        return false;
    }
    return std::any_of(found->second.begin(), found->second.end(), [&](std::uint32_t index) {
        const EncounterPlacement& placement = m_placements[index];
        return !placement.taskKeys.empty() && m_world.quests().mode_task_active(character, placement.recipe->mode, placement.taskKeys);
    });
}

void Encounters::scan_spaces() {
    if (m_spaces.empty()) {
        return;
    }
    // The few recipes of the spaces of the instances, and the characters of each space near each recipe (the position index: MP-8).
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    const gamedata::CreatureCapsule* player = creatures != nullptr && creatures->player_capsule() ? &*creatures->player_capsule() : nullptr;
    std::vector<std::pair<std::uint32_t, SpaceId>> wanted;
    for (const auto& [space, list] : m_spaces) {
        if (m_heldSpaces.contains(space)) {
            continue; // Stage QG9d: the recipes wait for the quest objects of the instance.
        }
        for (const std::uint32_t index : list) {
            if (m_spaceLive.contains({index, space}) || m_spaceDone.contains({index, space})) {
                continue;
            }
            const EncounterPlacement& placement = m_placements[index];
            const gamedata::EncounterRecipe& recipe = *placement.recipe;
            bool inside = false;
            m_world.for_each_near(space, recipe.position, recipe.detectorReach + kBodyReach, [&](Entity& entity) {
                ++m_counts.entitiesLooked;
                inside = inside
                         || (entity.kind == EntityKind::player && !entity.life.dead && !entity.positionEstimated
                             && in_detector(recipe, recipe.position, capsule_of(entity, player))
                             // Stage QG9b: a recipe of a mode with more than one recipe needs an active task of the character that names it.
                             && (!placement.gated || m_world.quests().mode_task_active(entity, recipe.mode, placement.taskKeys)));
            });
            if (inside) {
                wanted.emplace_back(index, space);
            }
        }
    }
    for (const auto& [index, space] : wanted) {
        make_instance(index, space);
    }
}

bool Encounters::cutscene_requested(Entity& character, std::uint32_t crc) {
    if (crc == 0 || character.space == kMainSpace) {
        return false;
    }
    for (const auto& [root, instance] : m_instances) {
        if (instance.space != character.space || instance.phase != EncounterPhase::running) {
            continue;
        }
        const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
        for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
            const gamedata::EncounterStage& stage = recipe.stages[index];
            if (stage.objective.kind == gamedata::EncounterObjectiveKind::cutscene && stage.objective.cutsceneCrc == crc
                && instance.stages[index].started && !instance.stages[index].ended) {
                character.encounterCutscene = crc;
                return true;
            }
        }
    }
    return false;
}

bool Encounters::cutscene_ended(Entity& character) {
    const std::uint32_t crc = character.encounterCutscene;
    character.encounterCutscene = 0;
    if (crc == 0 || character.space == kMainSpace) {
        return false;
    }
    for (auto& [root, instance] : m_instances) {
        if (instance.space != character.space || instance.phase != EncounterPhase::running
            || instance.participants.count(character.characterId) == 0) {
            continue;
        }
        const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
        for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
            const gamedata::EncounterStage& stage = recipe.stages[index];
            if (stage.objective.kind == gamedata::EncounterObjectiveKind::cutscene && stage.objective.cutsceneCrc == crc
                && instance.stages[index].started && !instance.stages[index].ended) {
                log::write("world: encounters: the encounter %s: the cutscene %s of the stage %s ended at the client of character %s",
                           recipe.key.c_str(), stage.objective.cutscene.c_str(), stage.name.c_str(), character.characterId.c_str());
                end_stage(instance, index, true);
                return true;
            }
        }
    }
    return false;
}

} // namespace soulwarden::backend::world
