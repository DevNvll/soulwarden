#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// The support manifest of the quests (stage 1.8e, criterion I21 of research quest-engine.md; docs/systems/quests.md): one row for each
// objective of the quest data with its grant sources, its gates, its tasks and their suppliers, its achievement actions, its world content,
// its rewards, and the exact reasons when the backend cannot run it; the mission templates apart; and the counts of the quests that a
// character of a history can play from start to end.
//
// The manifest is made from the contracts of the code, not from a list by hand: the evaluators of the quest system (the task types that
// Quests runs, the availability `Quests::available`, the conditional, the reward join `Quests::reward_join`), the person system (the
// presence and the state selection), and the game data that the world loaded (the trigger volumes, the points of interest, the persons, the
// quest objects and the destructible quest objects, the creatures, the recipes, the loot tables, the item catalog). A world with other
// data gives other counts.
//
// The history of a profile (`SupportProfile`) is a closure in rounds: each round looks at each objective in data order, and an objective
// that a source grants now, whose gates pass, and whose tree has a completion path with the achievement bits of now is played: its
// achievement actions, its completion achievements, and its standing go into the history. The rounds end when a round plays nothing. The
// rules of the closure are in docs/systems/quests.md ("The support manifest").
//
// World thread only (it reads the systems of the world). A reader: it changes nothing.

namespace soulwarden::backend::world {

class World;

/** One history of the counts (a new character of an archetype, or an existing character with a backstory template of decision D2). */
struct SupportProfile {
    /** "new:ARCHETYPE" or "template:ID" or "level60:none". */
    std::string id;
    /** The group of the counts: "new character", or the start objective of the template (decision D2 option C), or "no template". */
    std::string group;
    /** The achievement bits at the start (the archetype bit of the creation, the achievements of a template). */
    std::vector<std::string> achievements;
    /** The objectives that the start grants (the territory start of a new character, the start objectives of a template). */
    std::vector<std::string> starts;
    /** The territory of the start grants (the origin territory of their instances; 0: none). */
    std::uint16_t startTerritory{0};
    /** The level of the character for RequiredLevel (a new character: the level cap that kills give; a template history: 60). */
    std::uint32_t level{1};
    /** Stage 1.8f (finding QR8E-R5): the items at the start (lower case): the start items of the creation of a new character. */
    std::vector<std::string> items;
    /**
     * Stage QG7: the faction that the history joins (1 Syndicate, 2 Marauder, 3 Covenant): the TaskChooseFaction of a recruitment of that
     * faction completes, the one of an other faction waits (the decline branch can end its recruitment). 0: the history joins the faction of
     * the first recruitment that it completes (the data order of the rounds).
     */
    std::uint8_t faction{0};
};

class QuestSupport {
public:
    explicit QuestSupport(World& world);
    ~QuestSupport();
    QuestSupport(const QuestSupport&) = delete;
    QuestSupport& operator=(const QuestSupport&) = delete;

    /**
     * The profiles of the counts from the data of the world: a new character for each archetype row of the backstory data (the creation
     * gives its AchievementUnlockOverride and the start objective of Windsward), a level 60 character with no template (decision D2 option
     * A), and a level 60 character with each backstory template (decision D2 option C: its achievements and its start objectives).
     */
    [[nodiscard]] std::vector<SupportProfile> default_profiles() const;

    /** @return The manifest (members "format", "counts", "profiles", "objectives", "missions"). No quest data: a document with no rows. */
    [[nodiscard]] nlohmann::json manifest(const std::vector<SupportProfile>& profiles);

private:
    struct Index;
    struct Row;
    struct History;
    struct BoardMission;

    void build();
    /**
     * @return The facts of one row. Stage QG6a: `mission` (not null) evaluates a mission template with the effective objective of a mission
     * that a board of the data gives (its tasks, its success event, the provider board as the destination of its return task).
     */
    [[nodiscard]] Row evaluate(std::uint32_t objective, const BoardMission* mission = nullptr) const;
    void play(const SupportProfile& profile, const std::vector<Row>& facts, History& history) const;

    World& m_world;
    std::unique_ptr<Index> m_index;
};

} // namespace soulwarden::backend::world
