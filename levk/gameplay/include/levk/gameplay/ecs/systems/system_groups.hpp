#pragma once
#include <dens/system_group.hpp>
#include <levk/gameplay/ecs/systems/component_system.hpp>

namespace le {
using ComponentSystemGroup = dens::system_group<SystemData>;

class PhysicsSystemGroup : public ComponentSystemGroup {};
class TickSystemGroup : public ComponentSystemGroup {};
} // namespace le
