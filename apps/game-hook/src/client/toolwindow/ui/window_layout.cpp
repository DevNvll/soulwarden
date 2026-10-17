#include "client/toolwindow/ui/window_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "client/toolwindow/model/admin_client.h"
#include "client/toolwindow/tool_window.h"

namespace soulwarden::client::toolwindow::ui {
namespace {

using nlohmann::json;

/** Seconds from the last change of the search text to the search request. */
constexpr double kSearchDelay = 0.25;
/** The end of the id of a weapon mastery in the game tables ("SwordAbilityTable"). */
constexpr const char* kWeaponSuffix = "AbilityTable";
constexpr ImVec4 kErrorColor{1.0F, 0.45F, 0.40F, 1.0F};
constexpr ImVec4 kDoneColor{0.55F, 0.90F, 0.55F, 1.0F};
constexpr ImVec4 kDimColor{0.65F, 0.68F, 0.72F, 1.0F};

// The copy of the replies of the backend, and the values of the controls. Submission thread only.
model::Snapshot g_data;
std::uint64_t g_version = 0;

char g_searchText[96] = "";
bool g_searchChanged = true;
double g_searchChangedAt = 0.0;
std::string g_selectedItem;
unsigned g_selectedMaxStack = 1;
int g_quantity = 1;
/** The gear score (0: the value of the item table) and the perks of a give (stage 1.3b): PerkID texts with "," between them. */
int g_gearScore = 0;
char g_perks[512] = {};

// The controls of the tab "Creatures".
char g_kindSearchText[96] = "";
bool g_kindSearchChanged = true;
double g_kindSearchChangedAt = 0.0;
std::string g_selectedKind;
int g_creatureLevel = 1;
int g_creatureDistance = 3;
int g_creatureHealthPercent = 50;
// The travel tab.
char g_placeFilter[64] = "";
std::string g_selectedPlace;
float g_travelPosition[3]{};
bool g_travelPositionSet = false;
/** The form of the world time (stage 2.2): the hour and the minute that the button "Set" sends. */
int g_worldHour = 12;
int g_worldMinute = 0;
/** The time of the last request for the travel places. Below 0: no request yet. */
double g_placesAskedAt = -1.0;
/** The build presets (stage B0): the time of the last request of the list, the selected preset and variant, and the confirm step. */
double g_presetsAskedAt = -1.0;
std::string g_selectedPreset;
std::string g_selectedVariant;
/** The character of an open confirm line: a change of the selected character closes it. */
std::string g_presetConfirm;
/** The number of the last result (Snapshot::resultNumber) that refilled the form. A second apply with the same result text is a new
 * result: the text alone cannot show it (stage B0 review, B0-R6). */
std::uint64_t g_presetFilled = 0;

/** The character that the values of the controls are for. */
std::string g_formCharacter;
int g_level = 1;
int g_experience = 1000;
double g_coin = 0.0;
int g_attributes[model::kAttributeCount]{};
std::map<std::string, int> g_weaponLevels;
std::map<std::string, int> g_skillLevels;

/** @return The name of a weapon for the window: the id of its mastery with no table suffix. */
std::string weapon_name(const std::string& id) {
    const std::size_t suffix = std::strlen(kWeaponSuffix);
    return id.size() > suffix && id.compare(id.size() - suffix, suffix, kWeaponSuffix) == 0 ? id.substr(0, id.size() - suffix) : id;
}

std::string capital(const char* text) {
    std::string name = text;
    if (!name.empty() && name[0] >= 'a' && name[0] <= 'z') {
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
    }
    return name;
}

/** Sets the controls to the values of the selected character. */
void fill_form() {
    g_formCharacter = g_data.selected;
    g_level = static_cast<int>(g_data.level);
    g_coin = static_cast<double>(g_data.coin) / 100.0;
    for (unsigned index = 0; index < model::kAttributeCount; ++index) {
        g_attributes[index] = static_cast<int>(g_data.attributes[index].value);
    }
    g_weaponLevels.clear();
    g_skillLevels.clear();
}

void send(const char* name, const json& arguments, const std::string& label) {
    // The command is for the character whose values this frame shows: the rows and the
    // controls of the window are the rows and the controls of that character.
    model::command(g_data.selected, name, arguments.dump(), label);
}

/** The first line: the character, and the state of the connection to the backend. */
void draw_header() {
    const model::Character* selected = nullptr;
    for (const model::Character& character : g_data.characters) {
        if (character.id == g_data.selected) {
            selected = &character;
        }
    }
    const auto label_of = [](const model::Character& character) {
        return character.name + " (level " + std::to_string(character.level) + (character.inWorld ? ", in the world)" : ", not in the world)");
    };
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Character");
    ImGui::SameLine();
    if (g_data.characters.size() > 1) {
        // The account has more than one character: each command is for the selected one.
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0F);
        if (ImGui::BeginCombo("##character", selected != nullptr ? label_of(*selected).c_str() : "(none)")) {
            for (const model::Character& character : g_data.characters) {
                if (ImGui::Selectable(label_of(character).c_str(), character.id == g_data.selected)) {
                    model::select_character(character.id);
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextUnformatted(selected != nullptr ? label_of(*selected).c_str() : "(none)");
    }
    if (!g_data.problem.empty()) {
        ImGui::TextColored(kErrorColor, "%s", g_data.problem.c_str());
    }
}

void draw_items() {
    const float line = ImGui::GetFontSize();
    if (!g_data.catalog) {
        ImGui::TextColored(kErrorColor, "The backend has no item catalog: it gives no item.");
        ImGui::TextWrapped("Make the catalog with tools/gamedata/items.sh, then start the game again.");
    }
    ImGui::SeparatorText("Give an item");
    ImGui::SetNextItemWidth(line * 24.0F);
    if (ImGui::InputTextWithHint("##search", "search: id, type, or class of an item", g_searchText, sizeof(g_searchText))) {
        g_searchChanged = true;
        g_searchChangedAt = ImGui::GetTime();
    }
    // The request goes out a short time after the last key, not for each character.
    if (g_searchChanged && ImGui::GetTime() - g_searchChangedAt >= kSearchDelay) {
        g_searchChanged = false;
        model::search(g_searchText);
    }
    ImGui::SameLine();
    if (g_data.searchDone) {
        if (g_data.foundTotal > g_data.found.size()) {
            ImGui::TextColored(kDimColor, "%zu of %u show", g_data.found.size(), g_data.foundTotal);
        } else {
            ImGui::TextColored(kDimColor, "%u found", g_data.foundTotal);
        }
    }
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
                                       | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("found", 4, kTable, ImVec2(0.0F, line * 10.5F))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 5.0F);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 2.0F);
        ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Stack", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();
        for (const model::CatalogItem& item : g_data.found) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(item.id.c_str(), item.id == g_selectedItem, ImGuiSelectableFlags_SpanAllColumns)) {
                g_selectedItem = item.id;
                g_selectedMaxStack = item.maxStack;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.type.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", item.tier);
            ImGui::TableNextColumn();
            ImGui::Text("%u", item.maxStack);
        }
        ImGui::EndTable();
    }
    if (g_selectedItem.empty()) {
        ImGui::TextColored(kDimColor, "Selected: none. Select an item of the list.");
    } else {
        ImGui::Text("Selected: %s (a stack has %u)", g_selectedItem.c_str(), g_selectedMaxStack);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Quantity");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(line * 8.0F);
    ImGui::InputInt("##quantity", &g_quantity, 1, 10);
    g_quantity = std::clamp(g_quantity, 1, 100000);
    ImGui::SameLine();
    ImGui::TextUnformatted("Gear score");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(line * 8.0F);
    ImGui::InputInt("##gearScore", &g_gearScore, 5, 50);
    g_gearScore = std::clamp(g_gearScore, 0, 1000);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Perks");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##perks", "PerkID texts with \",\" between them (an empty part: no perk; a gem is a perk)", g_perks,
                             sizeof(g_perks));
    ImGui::BeginDisabled(g_selectedItem.empty() || g_data.busy);
    if (ImGui::Button("Give")) {
        nlohmann::json arguments{{"item", g_selectedItem}, {"quantity", g_quantity}};
        if (g_gearScore > 0) {
            arguments["gearScore"] = g_gearScore;
        }
        if (g_perks[0] != '\0') {
            arguments["perks"] = std::string(g_perks);
        }
        send("give_item", arguments, "Give " + std::to_string(g_quantity) + " " + g_selectedItem);
    }
    ImGui::EndDisabled();

    char title[64];
    snprintf(title, sizeof(title), "Bag: %zu items in %u slots", g_data.bag.size(), g_data.bagSlots);
    ImGui::SeparatorText(title);
    if (ImGui::BeginTable("bag", 4, kTable, ImVec2(0.0F, std::max(line * 6.0F, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 5.0F);
        ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthStretch, 1.5F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.7F);
        ImGui::TableHeadersRow();
        for (const model::BagItem& item : g_data.bag) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(item.slot));
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%u", item.slot);
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", item.item.c_str(), item.locked ? " (locked)" : "");
            ImGui::TableNextColumn();
            ImGui::Text("%u", item.quantity);
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(g_data.busy);
            if (ImGui::SmallButton("Remove")) {
                send("remove_item", {{"slot", item.slot}},
                     "Remove " + std::to_string(item.quantity) + " " + item.item + " of slot " + std::to_string(item.slot));
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void draw_creatures() {
    const float line = ImGui::GetFontSize();
    if (!g_data.creatureData) {
        ImGui::TextColored(kErrorColor, "The backend has no creature data: the world has no creature.");
        ImGui::TextWrapped("Make the data with tools/gamedata/creatures.sh, then start the game again.");
    }
    ImGui::SeparatorText("Spawn a creature");
    ImGui::SetNextItemWidth(line * 24.0F);
    if (ImGui::InputTextWithHint("##kind-search", "search: id, name, or family of a kind", g_kindSearchText,
                                 sizeof(g_kindSearchText))) {
        g_kindSearchChanged = true;
        g_kindSearchChangedAt = ImGui::GetTime();
    }
    // The request goes out a short time after the last key, not for each character.
    if (g_kindSearchChanged && ImGui::GetTime() - g_kindSearchChangedAt >= kSearchDelay) {
        g_kindSearchChanged = false;
        model::search_kinds(g_kindSearchText);
    }
    ImGui::SameLine();
    if (g_data.kindSearchDone) {
        if (g_data.kindTotal > g_data.kinds.size()) {
            ImGui::TextColored(kDimColor, "%zu of %u show", g_data.kinds.size(), g_data.kindTotal);
        } else {
            ImGui::TextColored(kDimColor, "%u found", g_data.kindTotal);
        }
    }
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
                                       | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("kinds", 4, kTable, ImVec2(0.0F, line * 8.5F))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 4.0F);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.5F);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthStretch, 1.3F);
        ImGui::TableHeadersRow();
        for (const model::CreatureKind& kind : g_data.kinds) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(kind.id.c_str(), kind.id == g_selectedKind, ImGuiSelectableFlags_SpanAllColumns)) {
                g_selectedKind = kind.id;
                g_creatureLevel = static_cast<int>(kind.level);
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kind.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", kind.level);
            ImGui::TableNextColumn();
            ImGui::Text("%u", kind.health);
        }
        ImGui::EndTable();
    }
    if (g_selectedKind.empty()) {
        ImGui::TextColored(kDimColor, "Selected: none. Select a kind of the list.");
    } else {
        ImGui::Text("Selected: %s", g_selectedKind.c_str());
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Level");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(line * 6.5F);
    ImGui::InputInt("##creature-level", &g_creatureLevel, 1, 5);
    g_creatureLevel = std::clamp(g_creatureLevel, 1, 1000);
    ImGui::SameLine();
    ImGui::TextUnformatted("Meters to the north");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(line * 6.5F);
    ImGui::InputInt("##creature-distance", &g_creatureDistance, 1, 5);
    g_creatureDistance = std::clamp(g_creatureDistance, -500, 500);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_selectedKind.empty() || g_data.busy || !g_data.inWorld);
    if (ImGui::Button("Spawn")) {
        send("spawn_creature", {{"kind", g_selectedKind}, {"level", g_creatureLevel}, {"dx", 0}, {"dy", g_creatureDistance}, {"dz", 0}},
             "Spawn " + g_selectedKind + " (level " + std::to_string(g_creatureLevel) + ")");
    }
    ImGui::EndDisabled();
    if (!g_data.inWorld) {
        ImGui::TextColored(kDimColor, "The character is not in the world: a new creature needs its place.");
    }

    char title[96];
    snprintf(title, sizeof(title), "Creatures near the character: %zu (%u in the world)", g_data.creatures.size(), g_data.creatureTotal);
    ImGui::SeparatorText(title);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Health for \"Set\" (percent)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(line * 6.5F);
    ImGui::InputInt("##creature-health", &g_creatureHealthPercent, 5, 25);
    g_creatureHealthPercent = std::clamp(g_creatureHealthPercent, 0, 100);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_data.busy || g_data.spawnPointsWaiting == 0);
    char reset[64];
    snprintf(reset, sizeof(reset), "Reset %u spawn point(s)", g_data.spawnPointsWaiting);
    if (ImGui::Button(reset)) {
        send("reset_creatures", json::object(), "Reset the spawn points that wait");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginTable("creatures", 5, kTable, ImVec2(0.0F, std::max(line * 6.0F, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Creature", ImGuiTableColumnFlags_WidthStretch, 3.2F);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 0.8F);
        ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthStretch, 2.1F);
        ImGui::TableSetupColumn("Meters", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 3.7F);
        ImGui::TableHeadersRow();
        for (const model::Creature& creature : g_data.creatures) {
            ImGui::TableNextRow();
            ImGui::PushID(creature.id.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s%s", creature.name.empty() ? creature.kind.c_str() : creature.name.c_str(),
                        creature.spawnPoint ? "" : " (command)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s, id %s", creature.kind.c_str(), creature.id.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%u", creature.level);
            ImGui::TableNextColumn();
            if (creature.dead) {
                ImGui::TextColored(kErrorColor, "dead");
            } else {
                ImGui::Text("%u / %u", creature.health, creature.healthMax);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", static_cast<double>(creature.distance));
            ImGui::TableNextColumn();
            const std::string name = creature.name.empty() ? creature.kind : creature.name;
            ImGui::BeginDisabled(g_data.busy || creature.dead);
            if (ImGui::SmallButton("Set")) {
                send("set_creature_health", {{"creature", creature.id}, {"percent", g_creatureHealthPercent}},
                     "Set the health of " + name + " to " + std::to_string(g_creatureHealthPercent) + " percent");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Kill")) {
                send("kill_creature", {{"creature", creature.id}}, "Kill " + name);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(g_data.busy);
            if (ImGui::SmallButton("Remove")) {
                send("remove_creature", {{"creature", creature.id}}, "Remove " + name);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

/** @return The text with the letters A to Z in lower case. */
std::string lower_text(std::string text) {
    for (char& letter : text) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter - 'A' + 'a');
        }
    }
    return text;
}

void draw_travel() {
    const float line = ImGui::GetFontSize();
    // The list of the places does not change in one start: the window asks for it one time,
    // and again each 5 s while the backend gave no list.
    if (!g_data.placesDone && (g_placesAskedAt < 0.0 || ImGui::GetTime() - g_placesAskedAt >= 5.0)) {
        g_placesAskedAt = ImGui::GetTime();
        model::load_places();
    }
    if (g_data.hasPosition) {
        ImGui::Text("The character is at %.1f %.1f %.1f (meters: east, north, up).", static_cast<double>(g_data.position[0]),
                    static_cast<double>(g_data.position[1]), static_cast<double>(g_data.position[2]));
        if (!g_travelPositionSet) {
            g_travelPositionSet = true;
            std::copy(std::begin(g_data.position), std::end(g_data.position), std::begin(g_travelPosition));
        }
    } else {
        ImGui::TextColored(kDimColor, "The character is not in the world: a travel sets the place of its next world entry.");
    }

    ImGui::SeparatorText("Travel to a place");
    if (g_data.placesDone && !g_data.placesLoaded) {
        ImGui::TextColored(kErrorColor, "The backend has no travel places.");
        ImGui::TextWrapped("Make the list with tools/gamedata/places.sh, then start the game again.");
    }
    ImGui::SetNextItemWidth(line * 24.0F);
    ImGui::InputTextWithHint("##place-filter", "filter: name, territory, shrine, inn", g_placeFilter, sizeof(g_placeFilter));
    const std::string filter = lower_text(g_placeFilter);
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
                                       | ImGuiTableFlags_SizingStretchProp;
    const model::TravelPlace* selected = nullptr;
    unsigned shown = 0;
    // The space below the table: the selected place with its button, and the position form.
    const float below = ImGui::GetFrameHeightWithSpacing() * 5.2F;
    if (ImGui::BeginTable("places", 4, kTable, ImVec2(0.0F, std::max(line * 8.0F, ImGui::GetContentRegionAvail().y - below)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Place", ImGuiTableColumnFlags_WidthStretch, 4.2F);
        ImGui::TableSetupColumn("Territory", ImGuiTableColumnFlags_WidthStretch, 2.8F);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1.1F);
        ImGui::TableSetupColumn("Meters", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableHeadersRow();
        for (const model::TravelPlace& place : g_data.places) {
            if (place.id == g_selectedPlace) {
                selected = &place;
            }
            if (!filter.empty() && lower_text(place.name + " " + place.territory + " " + place.kind).find(filter) == std::string::npos) {
                continue;
            }
            ++shown;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(place.id.c_str());
            if (ImGui::Selectable(place.name.c_str(), place.id == g_selectedPlace, ImGuiSelectableFlags_SpanAllColumns)) {
                g_selectedPlace = place.id;
                selected = &place;
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(place.territory.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(place.kind.c_str());
            ImGui::TableNextColumn();
            if (g_data.hasPosition) {
                ImGui::Text("%.0f", static_cast<double>(std::hypot(place.position[0] - g_data.position[0],
                                                                    place.position[1] - g_data.position[1])));
            }
        }
        ImGui::EndTable();
    }
    if (selected == nullptr) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kDimColor, "Selected: none. Select a place of the list (%u of %zu show).", shown, g_data.places.size());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Selected: %s, %s", selected->name.c_str(), selected->territory.c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selected == nullptr || g_data.busy);
    if (ImGui::Button("Travel")) {
        send("travel", {{"place", selected->id}}, "Travel to " + selected->name + " (" + selected->territory + ")");
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Travel to a position (meters: east, north, up)");
    ImGui::SetNextItemWidth(line * 20.0F);
    ImGui::InputFloat3("##travel-position", g_travelPosition, "%.1f");
    ImGui::SameLine();
    ImGui::BeginDisabled(g_data.busy);
    if (ImGui::Button("Travel##position")) {
        char label[96];
        snprintf(label, sizeof(label), "Travel to %.1f %.1f %.1f", static_cast<double>(g_travelPosition[0]),
                 static_cast<double>(g_travelPosition[1]), static_cast<double>(g_travelPosition[2]));
        send("travel", {{"x", g_travelPosition[0]}, {"y", g_travelPosition[1]}, {"z", g_travelPosition[2]}}, label);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_data.hasPosition);
    if (ImGui::Button("Here")) {
        std::copy(std::begin(g_data.position), std::end(g_data.position), std::begin(g_travelPosition));
    }
    ImGui::EndDisabled();
    ImGui::PushStyleColor(ImGuiCol_Text, kDimColor);
    ImGui::TextWrapped("The height is a height on the ground, or above it: the character falls to the ground.");
    ImGui::PopStyleColor();
}

/**
 * The tab "World" (stage 2.2): the world time of the backend, and the form that sets it. The time is a value of the whole world: the
 * command names the selected character as the sender (rule MP-9), and each player gets the new time.
 */
void draw_world() {
    ImGui::SeparatorText("World time");
    if (!g_data.hasWorldTime) {
        ImGui::TextColored(kDimColor, "The backend has no world time (a backend from before stage 2.2).");
        return;
    }
    ImGui::Text("The time of the world is %s (%s). Next phase: %s in %u s.", g_data.worldTime.c_str(), g_data.phase.c_str(),
                g_data.nextPhase.c_str(), g_data.secondsToNextPhase);
    ImGui::PushStyleColor(ImGuiCol_Text, kDimColor);
    ImGui::TextWrapped("One day of the game is %u minutes of real time: %u minutes from 06:00 to 19:00, and %u minutes of night. "
                       "The sky of each player follows this time.",
                       (g_data.dayLength + g_data.nightLength) / 60, g_data.dayLength / 60, g_data.nightLength / 60);
    ImGui::PopStyleColor();

    ImGui::SeparatorText("Set the time of the world");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
    ImGui::InputInt("hour##world-hour", &g_worldHour);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
    ImGui::InputInt("minute##world-minute", &g_worldMinute);
    const bool correct = g_worldHour >= 0 && g_worldHour <= 23 && g_worldMinute >= 0 && g_worldMinute <= 59;
    ImGui::SameLine();
    ImGui::BeginDisabled(!correct || g_data.busy);
    if (ImGui::Button("Set##world-time")) {
        char time[8];
        snprintf(time, sizeof(time), "%02d:%02d", g_worldHour, g_worldMinute);
        send("set_world_time", {{"time", time}}, std::string("Set the time of the world to ") + time);
    }
    ImGui::EndDisabled();
    if (!correct) {
        ImGui::TextColored(kErrorColor, "The hour is 0 to 23, and the minute is 0 to 59.");
    }
    ImGui::BeginDisabled(g_data.busy);
    for (const char* time : {"06:00", "12:00", "19:00", "00:00"}) {
        if (ImGui::Button(time)) {
            send("set_world_time", {{"time", time}}, std::string("Set the time of the world to ") + time);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::EndDisabled();
    ImGui::PushStyleColor(ImGuiCol_Text, kDimColor);
    ImGui::TextWrapped("The time changes for each player of the world, not only for this character. Some places have their own "
                       "light (a cave, a dungeon, a storm): their sky can differ from the time of the world.");
    ImGui::PopStyleColor();
}

/** One row of the character tab: a name, the value now, a control for the new value, and a button. */
bool begin_value_row(const char* name, const char* now) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(now);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    return true;
}

/**
 * The group "Build presets" of the tab "Character" (stage B0): the presets of the backend, the choice of the variant (the tree), the
 * button "Apply", one confirm line, and the result line. The command keeps the character of the snapshot that the window drew.
 */
void draw_presets() {
    // The list does not change in one start: the window asks for it one time, and again each 5 s while the backend gave no list.
    if (!g_data.presetsDone && (g_presetsAskedAt < 0.0 || ImGui::GetTime() - g_presetsAskedAt >= 5.0)) {
        g_presetsAskedAt = ImGui::GetTime();
        model::load_presets();
    }
    ImGui::SeparatorText("Build presets");
    if (!g_data.presetsDone) {
        ImGui::TextColored(kDimColor, "No list of the backend yet.");
        return;
    }
    if (g_data.presets.empty()) {
        ImGui::TextColored(kDimColor, "The backend has no build preset.");
        return;
    }
    const model::Preset* preset = nullptr;
    for (const model::Preset& entry : g_data.presets) {
        if (entry.name == g_selectedPreset) {
            preset = &entry;
        }
    }
    if (preset == nullptr) {
        preset = &g_data.presets.front();
        g_selectedPreset = preset->name;
        g_selectedVariant = preset->variants.empty() ? std::string{} : preset->variants.front().name;
    }
    const float line = ImGui::GetFontSize();
    ImGui::BeginDisabled(g_data.busy);
    ImGui::SetNextItemWidth(line * 24.0F);
    if (ImGui::BeginCombo("##preset", (preset->title.empty() ? preset->name : preset->title).c_str())) {
        for (const model::Preset& entry : g_data.presets) {
            if (ImGui::Selectable((entry.title.empty() ? entry.name : entry.title).c_str(), entry.name == g_selectedPreset)) {
                g_selectedPreset = entry.name;
                g_selectedVariant = entry.variants.empty() ? std::string{} : entry.variants.front().name;
                g_presetConfirm.clear();
            }
        }
        ImGui::EndCombo();
    }
    bool first = true;
    for (const model::PresetVariant& variant : preset->variants) {
        if (!first) {
            ImGui::SameLine();
        }
        first = false;
        if (ImGui::RadioButton(variant.label.c_str(), variant.name == g_selectedVariant)) {
            g_selectedVariant = variant.name;
            g_presetConfirm.clear();
        }
    }
    if (!preset->enabled) {
        ImGui::TextColored(kErrorColor, "The backend disabled this preset: %s", preset->cause.c_str());
    }
    std::string name;
    for (const model::Character& character : g_data.characters) {
        if (character.id == g_data.selected) {
            name = character.name;
        }
    }
    if (g_presetConfirm != g_data.selected) {
        g_presetConfirm.clear();
    }
    ImGui::BeginDisabled(!preset->enabled);
    if (g_presetConfirm.empty()) {
        if (ImGui::Button("Apply##preset")) {
            g_presetConfirm = g_data.selected;
        }
    } else {
        // The confirm line: the command names the character of this frame.
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(kErrorColor, "This replaces the equipment, the level, the attributes, and the trees of %s.", name.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Confirm##preset")) {
            std::string label = "Apply the build preset " + preset->name;
            for (const model::PresetVariant& variant : preset->variants) {
                if (variant.name == g_selectedVariant) {
                    label += " (" + variant.label + ")";
                }
            }
            send("apply_preset", {{"preset", preset->name}, {"variant", g_selectedVariant}}, label);
            g_presetConfirm.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##preset")) {
            g_presetConfirm.clear();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    // The result line of the last preset command (the last line of the window has it too). A preset that the backend applied changes
    // the level and the attributes: the fields of the form get the new values one time.
    if (g_data.result.rfind("Apply the build preset", 0) == 0 && !g_data.resultIsError && g_data.resultNumber != g_presetFilled &&
        !g_data.busy) {
        g_presetFilled = g_data.resultNumber;
        fill_form();
    }
    if (g_data.result.rfind("Apply the build preset", 0) == 0) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(g_data.resultIsError ? kErrorColor : kDoneColor, "%s", g_data.result.c_str());
        ImGui::PopTextWrapPos();
    }
}

void draw_character() {
    char now[96];
    if (!g_data.tables) {
        ImGui::TextColored(kErrorColor, "The backend has no progression tables: it refuses a level and an attribute.");
        ImGui::TextWrapped("Make the tables with tools/gamedata/progression.sh, then start the game again.");
    }
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    ImGui::BeginDisabled(g_data.busy);
    if (ImGui::BeginTable("values", 4, kTable)) {
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 2.2F);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthStretch, 4.2F);
        ImGui::TableSetupColumn("New", ImGuiTableColumnFlags_WidthStretch, 2.8F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.8F);
        ImGui::TableHeadersRow();

        snprintf(now, sizeof(now), "%u", g_data.level);
        begin_value_row("Level", now);
        ImGui::InputInt("##level", &g_level, 1, 10);
        g_level = std::clamp(g_level, 1, 1000);
        ImGui::TableNextColumn();
        if (ImGui::Button("Set##level")) {
            send("set_level", {{"level", g_level}}, "Set the level " + std::to_string(g_level));
        }

        if (g_data.nextLevelExperience != 0) {
            snprintf(now, sizeof(now), "%u (next level: %u)", g_data.experience, g_data.nextLevelExperience);
        } else {
            snprintf(now, sizeof(now), "%u", g_data.experience);
        }
        begin_value_row("Experience", now);
        ImGui::InputInt("##experience", &g_experience, 100, 1000);
        g_experience = std::clamp(g_experience, 1, 10000000);
        ImGui::TableNextColumn();
        if (ImGui::Button("Give")) {
            send("add_experience", {{"amount", g_experience}}, "Give " + std::to_string(g_experience) + " experience");
        }

        snprintf(now, sizeof(now), "%.2f", static_cast<double>(g_data.coin) / 100.0);
        begin_value_row("Coin", now);
        ImGui::InputDouble("##coin", &g_coin, 0.0, 0.0, "%.2f");
        g_coin = std::clamp(g_coin, 0.0, 1000000.0);
        ImGui::TableNextColumn();
        if (ImGui::Button("Set##coin")) {
            char text[48];
            snprintf(text, sizeof(text), "Set the coin %.2f", g_coin);
            send("set_coin", {{"amount", static_cast<std::uint64_t>(std::llround(g_coin * 100.0))}}, text);
        }

        snprintf(now, sizeof(now), "%u / %u%s", g_data.health, g_data.healthMax, g_data.dead ? " (dead)" : "");
        begin_value_row("Health", now);
        ImGui::TableNextColumn();
        // The full health for the character. A dead character lives again at its place.
        if (ImGui::Button(g_data.dead ? "Revive" : "Heal")) {
            send("heal", json::object(), g_data.dead ? "Revive the character" : "Give the full health");
        }
        ImGui::EndTable();
    }
    // The creatures of the world attack a character that they see. With this mark they ignore
    // the character of this row (a value of the backend for this character, until its stop).
    bool ignore = g_data.creaturesIgnore;
    if (ImGui::Checkbox("Creatures ignore this character", &ignore)) {
        send("set_creatures_ignore", {{"ignore", ignore}}, ignore ? "Creatures ignore the character" : "Creatures attack the character");
    }
    // The loot switch (stage 3.5a): each kill of the character drops loot and coin (the outer
    // chances of the game data pass). A value of the backend for this character, until its stop.
    bool loot = g_data.forceLoot;
    if (ImGui::Checkbox("Each kill drops loot and coin", &loot)) {
        send("set_force_loot", {{"force", loot}}, loot ? "Each kill drops loot and coin" : "The kills have the chances of the game data");
    }
    if (!g_data.creatureBehavior) {
        ImGui::TextColored(kDimColor, "The behavior of the creatures is off for this start: they stand.");
    }

    char title[64];
    snprintf(title, sizeof(title), "Attributes: %u free points", g_data.freeAttributePoints);
    ImGui::SeparatorText(title);
    if (ImGui::BeginTable("attributes", 4, kTable)) {
        ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_WidthStretch, 2.2F);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthStretch, 4.2F);
        ImGui::TableSetupColumn("New", ImGuiTableColumnFlags_WidthStretch, 2.8F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.8F);
        ImGui::TableHeadersRow();
        // The sequence of the character window of the game.
        for (const unsigned index : {2U, 3U, 4U, 1U, 0U}) {
            const std::string name = capital(model::kAttributeNames[index]);
            ImGui::PushID(static_cast<int>(index));
            snprintf(now, sizeof(now), "%u (%u points)", g_data.attributes[index].value, g_data.attributes[index].spent);
            begin_value_row(name.c_str(), now);
            ImGui::InputInt("##value", &g_attributes[index], 1, 10);
            g_attributes[index] = std::clamp(g_attributes[index], 0, 100000);
            ImGui::TableNextColumn();
            if (ImGui::Button("Set")) {
                send("set_attribute", {{"attribute", model::kAttributeNames[index]}, {"value", g_attributes[index]}},
                     "Set " + name + " " + std::to_string(g_attributes[index]));
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    draw_presets();
}

/** @return The value of the control of one skill row: the level now until the user changes it. */
int& pending_level(std::map<std::string, int>& levels, const std::string& id, unsigned now) {
    const auto found = levels.find(id);
    return found != levels.end() ? found->second : levels.emplace(id, static_cast<int>(now)).first->second;
}

void draw_skills() {
    const float line = ImGui::GetFontSize();
    if (!g_data.tables) {
        ImGui::TextColored(kErrorColor, "The backend has no progression tables: it has no weapon mastery and no trade skill.");
        ImGui::TextWrapped("Make the tables with tools/gamedata/progression.sh, then start the game again.");
        return;
    }
    constexpr ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY
                                       | ImGuiTableFlags_SizingStretchProp;
    const float height = std::max(line * 6.0F, (ImGui::GetContentRegionAvail().y - line * 4.0F) * 0.5F);
    ImGui::BeginDisabled(g_data.busy);
    ImGui::SeparatorText("Weapon mastery: each level above 1 gives one point (free, used)");
    if (ImGui::BeginTable("weapons", 6, kTable, ImVec2(0.0F, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Weapon", ImGuiTableColumnFlags_WidthStretch, 2.6F);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Free", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("New level", ImGuiTableColumnFlags_WidthStretch, 2.7F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.1F);
        ImGui::TableHeadersRow();
        for (const model::Weapon& weapon : g_data.weapons) {
            const std::string name = weapon_name(weapon.id);
            int& level = pending_level(g_weaponLevels, weapon.id, weapon.level);
            ImGui::TableNextRow();
            ImGui::PushID(weapon.id.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", weapon.level);
            ImGui::TableNextColumn();
            ImGui::Text("%u", weapon.freePoints);
            ImGui::TableNextColumn();
            ImGui::Text("%u", weapon.spentPoints);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##level", &level, 1, 5);
            level = std::clamp(level, 1, 1000);
            ImGui::TableNextColumn();
            if (ImGui::Button("Set")) {
                send("set_weapon_mastery", {{"weapon", weapon.id}, {"level", level}},
                     "Set the mastery of " + name + " to level " + std::to_string(level));
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Trade skills");
    if (ImGui::BeginTable("tradeskills", 4, kTable, ImVec2(0.0F, std::max(line * 6.0F, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Skill", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 2.4F);
        ImGui::TableSetupColumn("New level", ImGuiTableColumnFlags_WidthStretch, 2.6F);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableHeadersRow();
        for (const model::TradeSkill& skill : g_data.tradeSkills) {
            int& level = pending_level(g_skillLevels, skill.id, skill.level);
            ImGui::TableNextRow();
            ImGui::PushID(skill.id.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(skill.id.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u of %u", skill.level, skill.maxLevel);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##level", &level, 1, 10);
            level = std::clamp(level, 0, 100000);
            ImGui::TableNextColumn();
            if (ImGui::Button("Set")) {
                send("set_trade_skill", {{"skill", skill.id}, {"level", level}},
                     "Set " + skill.id + " to level " + std::to_string(level));
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
}

} // namespace

void draw() noexcept {
    (void)model::read(g_data, g_version);
    if (g_data.selected != g_formCharacter) {
        fill_form();
    }
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    // At the left side: the middle of the picture stays free (the harness makes the game window
    // the foreground window with a click there).
    ImGui::SetNextWindowPos(ImVec2(12.0F * scale, 40.0F * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(580.0F * scale, display.x * 0.47F), std::min(600.0F * scale, display.y - 52.0F * scale)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.0F * scale, 300.0F * scale), display);
    bool open = true;
    char title[96];
    snprintf(title, sizeof(title), "Soulwarden tools (key 0x%02X shows and hides this window)###soulwarden-tools", settings().toggleKey);
    if (ImGui::Begin(settings().toggleKey == VK_F8 ? "Soulwarden tools (F8 shows and hides this window)###soulwarden-tools" : title,
                     &open,
                     ImGuiWindowFlags_NoCollapse)) {
        if (!g_data.configured) {
            ImGui::TextColored(kErrorColor, "%s", g_data.problem.c_str());
            ImGui::TextWrapped("Start the game with the launcher of Soulwarden (linux-compat/run-soulwarden.sh, or the entry of the "
                               "Steam library): it gives the game and the backend the token.");
        } else {
            draw_header();
            if (g_data.selected.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s",
                                   g_data.connected ? "The account of this start has no character yet. Make a character in "
                                                      "the main menu of the game."
                                                    : "No data of the backend yet.");
            } else {
                // The last line of the window has the result of the last command.
                const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
                if (ImGui::BeginChild("tabs", ImVec2(0.0F, -footer))) {
                    if (ImGui::BeginTabBar("pages")) {
                        if (ImGui::BeginTabItem("Items")) {
                            draw_items();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Character")) {
                            draw_character();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Skills")) {
                            draw_skills();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Creatures")) {
                            draw_creatures();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Travel")) {
                            draw_travel();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("World")) {
                            draw_world();
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }
                ImGui::EndChild();
                ImGui::Separator();
                if (g_data.busy) {
                    ImGui::TextColored(kDimColor, "The command is with the backend...");
                } else if (!g_data.result.empty()) {
                    ImGui::PushTextWrapPos(0.0F);
                    ImGui::TextColored(g_data.resultIsError ? kErrorColor : kDoneColor, "%s", g_data.result.c_str());
                    ImGui::PopTextWrapPos();
                } else {
                    ImGui::TextColored(kDimColor, "The result of a command shows here.");
                }
            }
        }
    }
    ImGui::End();
    if (!open) {
        (void)set_visible(false);
    }
}

} // namespace soulwarden::client::toolwindow::ui
