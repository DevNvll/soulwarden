#include "gamedata/level_data.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** @return The bytes of a file, or of the regular files of a folder (not of its sub folders). 0: none. */
std::uint64_t bytes_of(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_regular_file(path, error)) {
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        return error ? 0 : static_cast<std::uint64_t>(size);
    }
    std::uint64_t total = 0;
    if (std::filesystem::is_directory(path, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
            std::error_code sizeError;
            if (entry.is_regular_file(sizeError)) {
                const std::uintmax_t size = entry.file_size(sizeError);
                total += sizeError ? 0 : static_cast<std::uint64_t>(size);
            }
        }
    }
    return total;
}

bool path_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

} // namespace

bool level_name_valid(std::string_view level) {
    if (level.empty() || level.size() > 64) {
        return false;
    }
    return std::all_of(level.begin(), level.end(),
                       [](char letter) { return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_'; });
}

void LevelData::load(const std::string& directory) {
    const std::filesystem::path folder(directory);
    const std::filesystem::path manifestPath = folder / kLevelManifestFile;
    std::ifstream stream(manifestPath);
    if (!stream) {
        m_problem = "the set has no " + std::string(kLevelManifestFile) + " (make it with tools/gamedata/level.sh)";
        return;
    }
    json manifest;
    try {
        manifest = json::parse(stream);
    } catch (const json::exception& error) {
        m_problem = std::string(kLevelManifestFile) + " is not JSON: " + error.what();
        return;
    }
    if (!manifest.is_object() || !manifest.contains("format") || !manifest["format"].is_number_unsigned()
        || manifest["format"].get<std::uint64_t>() != kLevelFormat) {
        m_problem = std::string(kLevelManifestFile) + " has a different format (an old file: make the set again with tools/gamedata/level.sh)";
        return;
    }
    if (!manifest.contains("level") || !manifest["level"].is_string() || manifest["level"].get<std::string>() != m_level
        || !manifest.contains("terrain") || !manifest["terrain"].is_string()) {
        m_problem = std::string(kLevelManifestFile) + " does not name the level " + m_level + " and its terrain";
        return;
    }
    m_terrain = manifest["terrain"].get<std::string>();
    m_accepted = true;

    const std::filesystem::path creatures = folder / kCreatureDataFile;
    if (!path_exists(creatures)) {
        m_creaturePart.problem = "no file (the level has no spawn point of its own)";
    } else if (m_creatures.load(creatures.string())) {
        m_creaturePart.loaded = true;
        m_creaturePart.bytes = bytes_of(creatures);
    } else {
        m_creaturePart.problem = m_creatures.error();
    }

    const std::filesystem::path navigation = folder / kNavigationFolder;
    if (!path_exists(navigation)) {
        m_navigationPart.problem = "no folder";
    } else if (m_navigation.set_directory(navigation.string())) {
        m_navigationPart.loaded = true;
        m_navigationPart.bytes = bytes_of(navigation);
    } else {
        m_navigationPart.problem = m_navigation.problem().empty() ? "no region file with a correct header" : m_navigation.problem();
    }

    const std::filesystem::path heights = folder / kHeightMapsFolder;
    if (!path_exists(heights)) {
        m_heightPart.problem = "no folder";
    } else if (m_heights.set_directory(heights.string())) {
        m_heightPart.loaded = true;
        m_heightPart.bytes = bytes_of(heights);
    } else {
        m_heightPart.problem = "the folder has no height file";
    }

    const std::filesystem::path interactions = folder / kInteractionDataFile;
    if (!path_exists(interactions)) {
        m_interactionPart.problem = "no file";
    } else if (m_interactions.load(interactions.string())) {
        m_interactionPart.loaded = true;
        m_interactionPart.bytes = bytes_of(interactions);
    } else {
        m_interactionPart.problem = m_interactions.error();
    }
}

std::unique_ptr<LevelData> LevelFolder::open(const std::string& level) const {
    if (m_directory.empty() || !level_name_valid(level)) {
        return nullptr;
    }
    const std::filesystem::path folder = std::filesystem::path(m_directory) / level;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        return nullptr;
    }
    auto data = std::make_unique<LevelData>(level);
    data->load(folder.string());
    return data;
}

std::vector<std::string> LevelFolder::levels() const {
    std::vector<std::string> names;
    std::error_code error;
    if (m_directory.empty() || !std::filesystem::is_directory(m_directory, error)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(m_directory, error)) {
        std::error_code kindError;
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(kindError) && level_name_valid(name)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace soulwarden::backend::gamedata
