#pragma once

namespace soulwarden::client::diagnostics {

/**
 * Installs entry trace points for research. Each point logs the register arguments and a
 * bounded dump of the memory that each argument points to, then runs the original function.
 *
 * The list comes from the SOULWARDEN_TRACE environment variable: image-relative addresses in
 * hexadecimal, separated by commas. An address can have a ":N" suffix that sets the dump
 * size in bytes, and then an "s" that adds the return addresses on the stack ("6444B10:0s").
 * SOULWARDEN_PROBE has the same form. A probe can be at any instruction start. It logs all general
 * registers and dumps the stack and the frame (rbp). A probe logs its first hits, then one
 * hit in 120.
 * The facility is active only when the process has SOULWARDEN_HARNESS=1.
 */
void install_trace_points() noexcept;

} // namespace soulwarden::client::diagnostics
