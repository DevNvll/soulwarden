#include "server/rep/research.h"

#include <cstdlib>

namespace soulwarden::backend::server::rep {

bool read_research_option(const std::string& text, ResearchSwitch& research) {
    const std::size_t equals = text.find('=');
    if (equals == std::string::npos || equals + 1 >= text.size()) {
        return false;
    }
    const std::string key = text.substr(0, equals);
    const std::string value = text.substr(equals + 1);
    char* end = nullptr;
    const long number = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    if (key == "registration-result") {
        research.registrationResult = static_cast<std::int32_t>(number);
    } else if (key == "reject" && number >= 0) {
        research.rejectReason = static_cast<std::uint32_t>(number);
    } else if (key == "kick" && number >= 0) {
        research.kickReason = static_cast<std::uint32_t>(number);
    } else if (key == "kick-delay" && number >= 0) {
        research.kickDelay = std::chrono::seconds(number);
    } else if (key == "refuse-equipment" && (number == 0 || number == 1)) {
        research.refuseEquipment = number == 1;
    } else {
        return false;
    }
    return true;
}

} // namespace soulwarden::backend::server::rep
