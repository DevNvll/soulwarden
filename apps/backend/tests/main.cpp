// Test program of the backend. It runs the game server with made-up clients: no socket, no
// game process, and a database in memory.
//
// Usage: soulwarden-backend-tests --golden-dir DIR [--work-dir DIR] [--write-golden] [--write-trace] [--list] [CASE ...]
// Status 0: each case passed. tools/test-soulwarden-backend.sh builds and runs this program.

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "core/logging/log.h"
#include "support/cases.h"
#include "support/check.h"

namespace soulwarden::backend::tests {

std::vector<std::string>& log_lines() {
    static std::vector<std::string> lines;
    return lines;
}

std::size_t log_count(const std::string& text) {
    std::size_t count = 0;
    for (const std::string& line : log_lines()) {
        count += line.find(text) != std::string::npos ? 1 : 0;
    }
    return count;
}

} // namespace soulwarden::backend::tests

int main(int argc, char** argv) {
    using namespace soulwarden::backend;
    tests::Options options;
    std::vector<std::string> selected;
    bool list = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--golden-dir" && index + 1 < argc) {
            options.goldenDirectory = argv[++index];
        } else if (argument == "--work-dir" && index + 1 < argc) {
            options.workDirectory = argv[++index];
        } else if (argument == "--write-golden") {
            options.writeGolden = true;
        } else if (argument == "--write-trace") {
            options.writeTrace = true;
        } else if (argument == "--list") {
            list = true;
        } else if (argument.rfind("--", 0) == 0) {
            std::fprintf(stderr, "usage: soulwarden-backend-tests --golden-dir DIR [--work-dir DIR] [--write-golden] [--write-trace] [--list] [CASE ...]\n");
            return 2;
        } else {
            selected.push_back(argument);
        }
    }
    const std::vector<tests::Case> cases = tests::all_cases();
    if (list) {
        for (const tests::Case& item : cases) {
            std::printf("%-4s %s\n", item.id, item.title);
        }
        return 0;
    }
    if (options.workDirectory.empty()) {
        options.workDirectory = std::filesystem::temp_directory_path() / "soulwarden-backend-tests";
    }
    std::filesystem::remove_all(options.workDirectory);
    std::filesystem::create_directories(options.workDirectory);
    // The log lines of the backend go to a list that the cases read, not to the terminal.
    log::set_sink([](const std::string& line) { tests::log_lines().push_back(line); });

    int passed = 0;
    int failed = 0;
    std::vector<std::string> results;
    for (const tests::Case& item : cases) {
        bool wanted = selected.empty();
        for (const std::string& name : selected) {
            wanted = wanted || name == item.id;
        }
        if (!wanted) {
            continue;
        }
        std::printf("%s  %s\n", item.id, item.title);
        tests::log_lines().clear();
        const tests::Counters before = tests::counters();
        try {
            item.run(options);
        } catch (const std::exception& error) {
            tests::check(false, std::string("the case stopped with an exception: ") + error.what());
        }
        const int checks = tests::counters().checks - before.checks;
        const bool ok = tests::counters().failures == before.failures && checks > 0;
        ok ? ++passed : ++failed;
        char line[256];
        std::snprintf(line, sizeof(line), "%-4s %s  %s (%d checks)", item.id, ok ? "PASS" : "FAIL", item.title, checks);
        results.emplace_back(line);
        std::printf("\n");
    }
    log::set_sink({});
    std::printf("Cases:\n");
    for (const std::string& line : results) {
        std::printf("  %s\n", line.c_str());
    }
    std::printf("Result: %d cases, %d passed, %d failed, %d checks\n", passed + failed, passed, failed, tests::counters().checks);
    return failed == 0 && passed > 0 ? 0 : 1;
}
