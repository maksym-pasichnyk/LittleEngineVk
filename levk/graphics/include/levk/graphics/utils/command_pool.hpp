#pragma once
#include <levk/graphics/command_buffer.hpp>

namespace le::graphics {
class FencePool {
  public:
	FencePool(not_null<Device*> device, std::size_t count = 0);

	vk::Fence next();

  private:
	vk::UniqueFence makeFence() const;

	std::vector<vk::UniqueFence> m_idle;
	std::vector<vk::UniqueFence> m_busy;
	not_null<Device*> m_device;
};

class CommandPool {
  public:
	static constexpr auto pool_flags_v = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient;

	CommandPool(not_null<Device*> device, QType qtype = QType::eGraphics, std::size_t batch = 4);

	CommandBuffer acquire();
	vk::Result release(CommandBuffer&& cmd, bool block);

  private:
	struct Cmd {
		vk::CommandBuffer cb;
		vk::Fence fence;
	};

	FencePool m_fencePool;
	std::vector<Cmd> m_cbs;
	vk::UniqueCommandPool m_pool;
	not_null<Device*> m_device;
	QType m_qtype{};
	u32 m_batch;
};
} // namespace le::graphics
