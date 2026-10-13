#pragma once

// The position report of the client. The input report of the client (message type 1589) has a
// field for the exact position of its character. The client fills and sends the field only
// when bit 15 of the flags of the input entry is set, and it sets that bit from the value
// "within a plot" of its housing component (protocol notes 4.7). This hook makes the client
// set the bit in each input entry: the backend then gets the exact position of the character
// with each input report, and the housing value of the client does not change (the camera,
// the compass, and the HUD stay as they are).
//
// The hook changes 6 bytes of one function of the client. It compares the code at the address
// with the code of the supported build first. With different code it changes nothing: the
// hook log has one line, the game continues, and the backend estimates the position from the
// camera, as it does for a client with no hook.

namespace soulwarden::client::hooks::movement {

/**
 * Makes the client send the position of its character in each input report. With
 * SOULWARDEN_EXACT_POSITION=0 it changes nothing. Call it one time, before the trace points of the
 * diagnostics: a trace point at the same address changes the code that this function compares.
 * @return True when the client code was changed.
 */
bool install_position_report() noexcept;

} // namespace soulwarden::client::hooks::movement
