#pragma once

namespace soulwarden::client::automation {

/**
 * Starts the automation harness when the process has SOULWARDEN_HARNESS=1.
 * The harness reads commands from `<data>/harness/command.txt` and writes
 * `<data>/harness/result.txt`. It sends input to this game process only.
 */
void start() noexcept;

} // namespace soulwarden::client::automation
