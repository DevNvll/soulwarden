#include "core/capture/http_capture.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace soulwarden::backend::capture {
namespace {

std::string g_directory;
std::atomic<unsigned> g_sequence{0};

std::string sanitize(const std::string& text) {
    std::string output;
    for (const char character : text) {
        output += std::isalnum(static_cast<unsigned char>(character)) != 0 ? character : '_';
        if (output.size() >= 80) {
            break;
        }
    }
    return output;
}

bool is_text(const std::string& body) {
    for (const unsigned char character : body) {
        if (character < 9 || (character > 13 && character < 32)) {
            return false;
        }
    }
    return true;
}

void write_body(std::ofstream& output, const std::string& body) {
    if (is_text(body)) {
        output << body << "\n";
        return;
    }
    char cell[4];
    for (std::size_t index = 0; index < body.size(); ++index) {
        std::snprintf(cell, sizeof(cell), "%02x", static_cast<unsigned char>(body[index]));
        output << cell << ((index + 1) % 32 == 0 ? "\n" : " ");
    }
    output << "\n";
}

/** @return True for a request header whose value must not be in a capture file. */
bool secret_header(const std::string& name) {
    static constexpr std::string_view kSecret = "x-soulwarden-admin";
    return name.size() == kSecret.size()
           && std::equal(name.begin(), name.end(), kSecret.begin(), [](char left, char right) {
                  return std::tolower(static_cast<unsigned char>(left)) == right;
              });
}

} // namespace

std::string recorded_target(const service::HttpRequest& request) {
    static constexpr std::string_view kAdminPath = "/admin";
    const std::string_view path = request.path;
    const bool admin = path.substr(0, kAdminPath.size()) == kAdminPath
                       && (path.size() == kAdminPath.size() || path[kAdminPath.size()] == '/');
    if (!admin || request.target.find('?') == std::string::npos) {
        return request.target;
    }
    return request.path + "?(not recorded)";
}

void enable(const std::string& directory) {
    std::filesystem::create_directories(directory);
    g_directory = directory;
}

void record(const service::HttpRequest& request, const service::HttpResponse& response, const std::string& routeName) {
    if (g_directory.empty()) {
        return;
    }
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%05u", g_sequence.fetch_add(1));
    const std::string name = std::string(prefix) + "-" + request.method + "-" + sanitize(request.host) + "-"
                             + sanitize(request.path) + ".txt";
    std::ofstream output(std::filesystem::path(g_directory) / name, std::ios::binary);
    output << "ROUTE " << routeName << "\n"
           << request.method << " " << recorded_target(request) << "\nHOST " << request.host << "\n";
    for (const auto& [key, value] : request.headers) {
        // The token of the admin interface is a secret of one start: no file has it.
        output << key << ": " << (secret_header(key) ? "(not recorded)" : value) << "\n";
    }
    output << "\nREQUEST BODY " << request.body.size() << " bytes\n";
    write_body(output, request.body);
    output << "\nREPLY " << response.status << " " << response.contentType << " " << response.body.size() << " bytes\n";
    for (const auto& [key, value] : response.headers) {
        output << key << ": " << value << "\n";
    }
    write_body(output, response.body);
}

} // namespace soulwarden::backend::capture
