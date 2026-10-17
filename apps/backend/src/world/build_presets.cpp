#include "world/build_presets.h"

#include <algorithm>
#include <set>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The mastery rank of each tree of the bow build: the level 20 of the screen, 19 points for the 19 nodes of a tree (decision D5). */
constexpr std::uint32_t kBowBuildRank = 19;
/** The count of each consumable of the bag and of each quick slot: a project default of the build sheet (section 5.2). */
constexpr std::uint32_t kBagCount = 10;
constexpr std::uint32_t kQuickCount = 50;

PresetItem slot_item(std::uint32_t slot, const char* itemId, std::uint32_t gearScore, PerkIds perks = {}, std::uint32_t quantity = 1) {
    PresetItem item;
    item.itemId = itemId;
    item.quantity = quantity;
    item.gearScore = gearScore;
    item.perks = std::move(perks);
    item.slot = slot;
    return item;
}

PresetItem bag_item(const char* itemId, std::uint32_t quantity) {
    PresetItem item;
    item.itemId = itemId;
    item.quantity = quantity;
    return item;
}

/**
 * The bow build of the user (stage B0): the video "THE ONE SHOT BOW BUILD IS BACK IN SEASON 10" and its build sheet
 * (~/Games/soulwarden/work/research/bow-build.md, sections 2, 3, and 5.2; each id from the game data). Decisions of the orchestrator:
 * D1 both trees (PvE first), D2 place 5 of the earring Enchanted Ward II, D3 the ring at 725 (the limit of the data: the ring has no
 * MaxCraftUpgradeGS, and the client parser keeps no upgrade limit for an empty cell), D4 the heart rune Bile Bomb, D5 level 70 and the
 * two masteries at the level 20, D6 the old equipment into the bag.
 */
BuildPreset bow_boltcaster() {
    BuildPreset preset;
    preset.name = "bow-boltcaster";
    preset.title = "Boltcaster bow and rapier (Season 10)";
    preset.level = 70;
    // STR 50, DEX 456, INT 5, FOC 5, CON 19: 510 points, the points of level 70 (DEX 500 with the food Banana Pudding).
    preset.attributes[attribute::kConstitution] = 19;
    preset.attributes[attribute::kFocus] = 5;
    preset.attributes[attribute::kStrength] = 50;
    preset.attributes[attribute::kDexterity] = 456;
    preset.attributes[attribute::kIntelligence] = 5;
    const PerkIds gorgonHead{"PerkID_StatSwap_HP", "PerkID_Armor_DmgLightning", "PerkID_RuneglassGem_ElementalSub_Lightning",
                             "PerkID_Common_CDR", "PerkID_Armor_DefBasic"};
    preset.items = {
        // Boltcaster: Electrified, Bolt, Shirking Shock II, Lightning Harnessing II, Alacrity II.
        slot_item(paperdoll::kMainHand1, "Artifact_Set1_2hBow", 800,
                  {"PerkID_Artifact_Set1_Bow", "PerkID_Artifact_Set1_BowPerk", "PerkID_Fixed_Weapon_DodgeSuccess_LightningDMG",
                   "PerkID_Slottable_Common_DmgLightning", "PerkID_Slottable_Common_DurHaste"}),
        // Lucky Narwhal Tusk: Acutely Empowered, Penetrating the Empowered, Augmented Electrified (gem), Vicious, Omnidirectional Evade.
        slot_item(paperdoll::kMainHand1 + 1, "Rapier_DoubloonShopNamedT5", 725,
                  {"PerkID_StatSwap_BaseDamage", "PerkID_Weapon_PvPZone_1", "PerkID_RuneglassGem_LightningAdd_Lightning",
                   "PerkID_Weapon_DmgCrit", "PerkID_Ability_Rapier_Evade"}),
        // The Gorgon armor: Wellness, Lightning Harnessing, Electrified Elemental Ward (gem), the perk of the piece, Enchanted Ward.
        slot_item(paperdoll::kHead, "LightHead_HercyneRaid_CraftedSchematic", 725, gorgonHead),
        slot_item(paperdoll::kChest, "LightChest_HercyneRaid_CraftedSchematic", 725,
                  {"PerkID_StatSwap_HP", "PerkID_Armor_DmgLightning", "PerkID_RuneglassGem_ElementalSub_Lightning",
                   "PerkID_Chest_HealthStam_Synergy", "PerkID_Armor_DefBasic"}),
        slot_item(paperdoll::kHands, "MediumHands_HercyneRaid_CraftedSchematic", 725, gorgonHead),
        slot_item(paperdoll::kLegs, "LightLegs_HercyneRaid_CraftedSchematic", 725,
                  {"PerkID_StatSwap_HP", "PerkID_Armor_DmgLightning", "PerkID_RuneglassGem_ElementalSub_Lightning",
                   "PerkID_Legs_DodgeSuccess_Stamina", "PerkID_Armor_DefBasic"}),
        // Winged Leather Shoes: Speedy, Electrified Elemental Ward (gem), Hardcore Parkour II, Enchanted Ward II, Lightning Harnessing II.
        slot_item(paperdoll::kFeet, "Artifact_Set1_MediumFeet", 800,
                  {"PerkID_Artifact_Set1_MediumFeet", "PerkID_RuneglassGem_ElementalSub_Lightning", "PerkID_Fixed_Feet_HasteOnVault",
                   "PerkID_Slottable_Common_DefBasic", "PerkID_Slottable_Common_DmgLightning"}),
        // Gorgon's Amulet: Wellness, Alchemist Reprieve, Elemental Ward IV (gem), Divine, Thrust Protection.
        slot_item(paperdoll::kAmulet, "AmuletT5_HercyneRaid_CraftedSchematic", 725,
                  {"PerkID_StatSwap_HP", "PerkID_Amulet_PotionHeal", "PerkID_Gem_ElementalWard4", "PerkID_Amulet_Healing",
                   "PerkID_Amulet_DefThurst"}),
        // Gorgonite Ring (725, decision D3): Acutely Empowered, Lightning Damage, Elemental Ward IV (gem), Mortal Empowerment, Gambled Energy.
        slot_item(paperdoll::kRing, "RingT5_HercyneRaid_CraftedShard", 725,
                  {"PerkID_StatSwap_BaseDamage", "PerkID_Ring_DmgLightning", "PerkID_Gem_ElementalWard4",
                   "PerkID_PvP_Ring_OnKill_EmpowerStack", "PerkID_Ring_MaxStamina"}),
        // Endless Thirst: The Thirst, Elemental Ward IV (gem), Empowering Toast II, Fortifying Toast II, Enchanted Ward II (decision D2).
        slot_item(paperdoll::kToken, "Artifact_Set1_Earring1", 800,
                  {"PerkID_Artifact_Set1_Earring1", "PerkID_Gem_ElementalWard4", "PerkID_Fixed_Earring_OnPotionDmg",
                   "PerkID_Fixed_Earring_OnPotionFort", "PerkID_Slottable_Common_DefBasic"}),
        // Cunning Heartrune of Bile Bomb (decision D4): the fixed perks of the definition (Crippling Bile, Exhausting Bile).
        slot_item(paperdoll::kHeartRune, "RuneBileBombT4A", 700),
        // The quick slots 14 to 16: Infused Health Potion, Regeneration Serum, Infused Mana Potion. Quick slot 17 keeps its item (U3).
        slot_item(paperdoll::kQuickFirst, "PotionHealthT5", 0, {}, kQuickCount),
        slot_item(paperdoll::kQuickFirst + 1, "SerumRegenT5", 0, {}, kQuickCount),
        slot_item(paperdoll::kQuickFirst + 2, "PotionManaT5", 0, {}, kQuickCount),
        // The bag: Powerful Honing Stone, Banana Pudding (DEX +44), Desert Sunrise.
        bag_item("HoningStoneT5", kBagCount),
        bag_item("FoodDEXT52", kBagCount),
        bag_item("FoodDOTT5", kBagCount),
    };
    const TreeBuild rapier{"RapierAbilityTable",
                           kBowBuildRank,
                           {"PassiveRapier02", "PassiveRapier05", "Ability_Rapier_Flourish", "Rapier_Flourish_Mod1", "Rapier_Flourish_Mod2",
                            "Ability_Rapier_Aerial_Lunge_Intro", "Rapier_BonusDmgLowStamina", "Rapier_BonusStamRegenOnKill",
                            "Rapier_Aerial_Lunge_Mod1", "Ability_Rapier_Evasion", "Rapier_Aerial_Lunge_Mod2", "Rapier_BonusCDOnCrit",
                            "Rapier_Evasion_Mod1", "Rapier_Aerial_Lunge_Mod3", "Rapier_Evasion_Mod2", "Rapier_BonusSpeedOnKill",
                            "Rapier_Evasion_Mod3", "Rapier_Evasion_Mod4", "Rapier_BonusDMGAfterAbility"},
                           {"Ability_Rapier_Evasion", "Ability_Rapier_Flourish", "Ability_Rapier_Aerial_Lunge_Intro"}};
    const std::array<std::string, kBarSlots> bowBar{"Ability_Bow_PenetratingShot", "Ability_Bow_SplinterShot", "Ability_Bow_RapidShot"};
    const TreeBuild bowPve{"BowAbilityTable",
                           kBowBuildRank,
                           {"Passive_Bow_Skirmish_RendHead", "Passive_Bow_Skirmish_CDRVsLow", "Passive_Bow_Skirmish_DodgeHaste",
                            "Passive_Bow_Skirmish_HasteOnSwap", "Passive_Bow_Skirmish_DmgVsDebuff", "Passive_Bow_Hunter_DmgVsDist",
                            "Ability_Bow_PenetratingShot", "Passive_Bow_Hunter_HeavyDmg", "Passive_Bow_Hunter_DmgVsLow",
                            "Ability_Bow_SplinterShot", "Ability_Bow_RapidShot", "Passive_Bow_Hunter_HeavyDmgVsFull",
                            "Upgrade_Bow_SplinterShot_CDRPerHIt", "Upgrade_Bow_RapidAccuracy_CDROnLast", "Passive_Bow_Hunter_DmgOnFirst",
                            "Upgrade_Bow_RapidAccuracy_DmgOnLast", "Passive_Bow_Hunter_Crit", "Upgrade_Bow_SplinterShot_FiveA",
                            "Ultimate_Bow_Hunter"},
                           bowBar};
    const TreeBuild bowPvp{"BowAbilityTable",
                           kBowBuildRank,
                           {"Passive_Bow_Skirmish_HasteWhenFoesNear", "Passive_Bow_Skirmish_RendHead", "Ability_Bow_RainofArrows",
                            "Passive_Bow_Skirmish_SlowFull", "Passive_Bow_Skirmish_CDRVsLow", "Passive_Bow_Skirmish_DodgeHaste",
                            "Passive_Bow_Skirmish_StaminaVsDebuff", "Passive_Bow_Skirmish_HasteOnSwap", "Passive_Bow_Skirmish_DmgVsDebuff",
                            "Passive_Bow_Skirmish_DebuffDur", "Ultimate_Bow_Skirmish", "Passive_Bow_Hunter_DmgVsDist",
                            "Ability_Bow_PenetratingShot", "Passive_Bow_Hunter_HeavyDmg", "Passive_Bow_Hunter_DmgVsLow",
                            "Ability_Bow_SplinterShot", "Ability_Bow_RapidShot", "Passive_Bow_Hunter_HeavyDmgVsFull",
                            "Passive_Bow_Hunter_DmgOnFirst"},
                           bowBar};
    preset.variants = {{"pve", "PvE tree", {bowPve, rapier}}, {"pvp", "PvP tree", {bowPvp, rapier}}};
    return preset;
}

const char* attribute_name(unsigned type) {
    switch (type) {
    case attribute::kConstitution:
        return "constitution";
    case attribute::kFocus:
        return "focus";
    case attribute::kStrength:
        return "strength";
    case attribute::kDexterity:
        return "dexterity";
    default:
        return "intelligence";
    }
}

} // namespace

std::vector<BuildPreset> project_presets() {
    return {bow_boltcaster()};
}

std::vector<BuildItem> build_items(const BuildPreset& preset) {
    std::vector<BuildItem> items;
    for (const PresetItem& entry : preset.items) {
        BuildItem item;
        item.itemId = entry.itemId;
        item.quantity = entry.quantity;
        if (entry.gearScore != 0) {
            item.gearScore = entry.gearScore;
        }
        item.perks = entry.perks;
        item.slot = entry.slot;
        items.push_back(std::move(item));
    }
    return items;
}

const char* preset_error_text(PresetError error) {
    switch (error) {
    case PresetError::none:
        return "no error";
    case PresetError::unknownPreset:
        return "the backend has no build preset with this name";
    case PresetError::unknownVariant:
        return "the build preset has no variant with this name";
    case PresetError::disabled:
        return "the build preset is disabled: the game data does not have a part of it";
    case PresetError::unknownCharacter:
        return "the store has no character with this id";
    case PresetError::items:
        return "the inventory system refused the items of the preset";
    case PresetError::progress:
        return "the progress system refused the level or the attributes of the preset";
    case PresetError::skills:
        return "the skill system refused a tree of the preset";
    case PresetError::storeFailed:
        return "the database did not accept the change";
    }
    return "unknown error";
}

BuildPresets::BuildPresets(World& world, std::vector<BuildPreset> presets) : m_world(world) {
    set(std::move(presets));
}

void BuildPresets::set(std::vector<BuildPreset> presets) {
    m_presets.clear();
    for (BuildPreset& preset : presets) {
        m_presets.push_back(PresetStatus{std::move(preset), false, "not checked"});
    }
    m_checked = false;
}

std::string BuildPresets::cause_of(const BuildPreset& preset) const {
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    if (catalog == nullptr || !catalog->loaded()) {
        return "the backend has no item catalog (tools/gamedata/items.sh)";
    }
    if (m_world.perk_data() == nullptr) {
        return "the backend has no perk data (tools/gamedata/perks.sh)";
    }
    if (preset.variants.empty()) {
        return "the preset has no variant";
    }
    std::set<std::uint32_t> slots;
    for (const PresetItem& item : preset.items) {
        if (item.slot && !slots.insert(*item.slot).second) {
            return "two items of the preset are for slot " + std::to_string(*item.slot);
        }
    }
    // The rules of the systems on a character with nothing: the same checks as the apply.
    std::string detail;
    if (m_world.inventory().check_build(build_items(preset), detail) != ItemError::none) {
        return detail;
    }
    if (const ProgressError error = m_world.progress().check_sheet(preset.level, preset.attributes); error != ProgressError::none) {
        return std::string("the level or the attributes: ") + progress_error_text(error);
    }
    for (const PresetVariant& variant : preset.variants) {
        if (const ProgressError error = m_world.skills().check_trees(variant.trees); error != ProgressError::none) {
            return "the trees of the variant " + variant.name + ": " + progress_error_text(error);
        }
    }
    return {};
}

void BuildPresets::check() {
    std::string enabled;
    for (PresetStatus& status : m_presets) {
        status.cause = cause_of(status.preset);
        status.enabled = status.cause.empty();
        if (status.enabled) {
            enabled += (enabled.empty() ? "" : ", ") + status.preset.name;
        } else {
            log::write("presets: the build preset %s is disabled: %s", status.preset.name.c_str(), status.cause.c_str());
        }
    }
    log::write("presets: %s", enabled.empty() ? "no build preset is enabled" : ("enabled build preset(s): " + enabled).c_str());
    m_checked = true;
}

const std::vector<PresetStatus>& BuildPresets::presets() {
    if (!m_checked) {
        check();
    }
    return m_presets;
}

PresetResult BuildPresets::apply(const std::string& characterId, std::string_view presetName, std::string_view variantName) {
    PresetResult result;
    const std::vector<PresetStatus>& list = presets();
    const auto found = std::find_if(list.begin(), list.end(), [&](const PresetStatus& status) { return status.preset.name == presetName; });
    if (found == list.end()) {
        result.error = PresetError::unknownPreset;
        return result;
    }
    const BuildPreset& preset = found->preset;
    if (!found->enabled) {
        result.error = PresetError::disabled;
        result.cause = found->cause;
        return result;
    }
    const auto variant = variantName.empty() ? preset.variants.begin()
                                             : std::find_if(preset.variants.begin(), preset.variants.end(),
                                                            [&](const PresetVariant& entry) { return entry.name == variantName; });
    if (variant == preset.variants.end()) {
        result.error = PresetError::unknownVariant;
        return result;
    }
    const Entity* entity = m_world.find_character(characterId);
    // The log lines name the session that plays the character.
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    const std::string what = "build preset " + preset.name + " (" + variant->name + ")";
    const auto refuse = [&](PresetError error, const std::string& cause) {
        result.error = error;
        result.cause = cause;
        log::write("presets: %s of character %s refused: %s", what.c_str(), characterId.c_str(), cause.c_str());
        return result;
    };

    // 1. The plan of each system, with no write: the checks of the single commands.
    Inventory::Plan items = m_world.inventory().plan_build(characterId, build_items(preset));
    if (!items.ok()) {
        return refuse(items.error == ItemError::unknownCharacter ? PresetError::unknownCharacter : PresetError::items,
                      item_error_text(items.error));
    }
    Progress::Plan sheet = m_world.progress().plan_sheet(characterId, preset.level, preset.attributes);
    if (!sheet.ok()) {
        return refuse(PresetError::progress, progress_error_text(sheet.error));
    }
    Skills::Plan trees = m_world.skills().plan_trees(characterId, variant->trees);
    if (!trees.ok()) {
        return refuse(PresetError::skills, progress_error_text(trees.error));
    }
    // The old items that leave their slots (an item of the preset that is in its slot already stays: no move).
    unsigned moved = 0;
    for (const PresetItem& item : preset.items) {
        const ItemInstance* old = item.slot ? items.before.equipment.item(*item.slot) : nullptr;
        const ItemInstance* now = item.slot ? items.items.equipment.item(*item.slot) : nullptr;
        moved += old != nullptr && (now == nullptr || now->instance != old->instance) ? 1 : 0;
    }
    // The achievement of the objective of each artifact of the equipment (the item catalog): the client shows the perks 2 to 5 of an
    // artifact only with it (live check U1 of stage B0; the game data gives it to its test characters: AchievementUnlockOverride).
    std::vector<std::string> achievements;
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    for (const PresetItem& item : preset.items) {
        const gamedata::ArtifactObjective* objective = item.slot && catalog != nullptr ? catalog->artifact_objective(item.itemId) : nullptr;
        if (objective != nullptr) {
            achievements.push_back(objective->achievementId);
        }
    }

    // 2. All rows in one transaction.
    bool stored = false;
    {
        state::Store::Batch batch(m_world.store());
        stored = batch.open() && m_world.inventory().write(items) && m_world.progress().write(sheet) && m_world.skills().write(trees)
                 && (achievements.empty() || m_world.ownership().write_achievements(characterId, achievements)) && batch.commit();
    }
    if (!stored) {
        return refuse(PresetError::storeFailed, preset_error_text(PresetError::storeFailed));
    }

    // 3. The entity and the marks: the level and the attributes first (the finish of the items refreshes the gear with them).
    m_world.progress().finish(sheet, what.c_str());
    m_world.inventory().finish(items, what.c_str());
    m_world.skills().finish(trees, what.c_str());
    if (Entity* now = m_world.find_character(characterId)) {
        m_world.ownership().achievements_written(*now, achievements);
        m_world.gear().refresh(*now);
        result.inWorld = true;
    }

    // The reply: the changes.
    std::string values;
    for (unsigned type : {attribute::kStrength, attribute::kDexterity, attribute::kIntelligence, attribute::kFocus, attribute::kConstitution}) {
        values += std::string(values.empty() ? "" : ", ") + attribute_name(type) + " " + std::to_string(preset.attributes[type]);
    }
    result.changes.push_back("level " + std::to_string(preset.level) + "; " + values);
    for (const PresetItem& item : preset.items) {
        if (item.slot) {
            result.changes.push_back("slot " + std::to_string(*item.slot) + ": " + std::to_string(item.quantity) + " x " + item.itemId
                                     + (item.gearScore != 0 ? ", gear score " + std::to_string(item.gearScore) : std::string{}));
        } else {
            result.changes.push_back("bag: " + std::to_string(item.quantity) + " x " + item.itemId);
        }
    }
    result.changes.push_back(std::to_string(moved) + " old item(s) of the paperdoll went into the bag");
    for (const TreeBuild& tree : variant->trees) {
        std::string bar;
        for (const std::string& ability : tree.bar) {
            bar += (bar.empty() ? "" : ", ") + (ability.empty() ? std::string("-") : ability);
        }
        result.changes.push_back(tree.table + ": level " + std::to_string(tree.rank + 1) + ", " + std::to_string(tree.nodes.size())
                                 + " nodes, bar " + bar);
    }
    for (const std::string& id : achievements) {
        result.changes.push_back("achievement " + id);
    }
    log::write("presets: %s for character %s: done, %u old item(s) into the bag (%s)",
               what.c_str(),
               characterId.c_str(),
               moved,
               result.inWorld ? "the character is in the world" : "the rows only");
    return result;
}

} // namespace soulwarden::backend::world
