#pragma once

// The layout of the tool window: one window with a character selector and three tabs (items,
// character, skills). Submission thread only, between ImGui::NewFrame and ImGui::Render.

namespace soulwarden::client::toolwindow::ui {

/** Draws the tool window for this frame. */
void draw() noexcept;

} // namespace soulwarden::client::toolwindow::ui
