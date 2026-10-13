#pragma once

#include <string>

namespace soulwarden::backend::state {

/** The single local world. The id is a fixed local value. */
struct LocalWorld {
    std::string worldId{"a2071063-7300-4a2e-9a01-000000000001"};
    std::string name{"Soulwarden"};
    std::string worldSet{"soulwarden-local-set"};
    int maxAccountCharacters{4};
    int maxConnectionCount{2000};
};

[[nodiscard]] inline const LocalWorld& local_world() {
    static const LocalWorld world;
    return world;
}

} // namespace soulwarden::backend::state
