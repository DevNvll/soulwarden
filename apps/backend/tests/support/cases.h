#pragma once

#include <filesystem>
#include <string>
#include <vector>

// The list of the test cases and the options of a test run.

namespace soulwarden::backend::tests {

struct Options {
    /** Folder of the stored files of the repository (apps/backend/tests/golden). */
    std::filesystem::path goldenDirectory;
    /** True: case T2 writes its stored file again and does not compare. */
    bool writeGolden{false};
    /** Folder for the files that a case writes (capture folders). It is not in the repository. */
    std::filesystem::path workDirectory;
    /**
     * True: case BTD1 writes the stored trace of the decisions of the behavior again (golden/behavior_trace.txt, stage BT1) and compares
     * with it. Do this only for a change of the behavior that is intended (a later stage of the trees), with its source.
     */
    bool writeTrace{false};
};

struct Case {
    const char* id;
    const char* title;
    void (*run)(const Options& options);
};

/** Each case of the test program, in the sequence of a run. */
std::vector<Case> all_cases();

/** The log lines of the backend from the start of the current case. */
std::vector<std::string>& log_lines();

/** @return The number of log lines of the current case that have the text. */
std::size_t log_count(const std::string& text);

} // namespace soulwarden::backend::tests
