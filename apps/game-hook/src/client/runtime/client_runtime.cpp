#include "client/runtime/client_runtime.h"

#include <atomic>

#include "client/automation/harness.h"
#include "client/diagnostics/trace_points.h"
#include "client/hooks/movement/position_report.h"
#include "client/hooks/network/network_hooks.h"
#include "client/hooks/trust/trust_anchor.h"
#include "client/toolwindow/tool_window.h"
#include "core/logging/log.h"

namespace soulwarden::client::runtime {
namespace {

std::atomic<bool> g_activated{false};

} // namespace

void attach() noexcept {
    const bool installed = hooks::network::install();
    core::log::write("hook: network hooks %s", installed ? "installed" : "FAILED");
}

void activate() noexcept {
    if (g_activated.exchange(true)) {
        return;
    }
    core::log::write("hook: client activated");
    (void)hooks::trust::install();
    automation::start();
    // Before the trace points: the tool window compares the client code with the code of the
    // supported build, and a trace point at the same address changes that code.
    toolwindow::start();
    // Before the trace points for the same cause: the position report compares the code of
    // the client at its address before it changes it.
    (void)hooks::movement::install_position_report();
    diagnostics::install_trace_points();
}

} // namespace soulwarden::client::runtime
