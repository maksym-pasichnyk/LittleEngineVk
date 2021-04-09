#pragma once
#include <core/traits.hpp>
#include <core/utils/future.hpp>
#include <graphics/context/transfer.hpp>

namespace le::graphics {
class Device;

class VRAM final : public Memory {
  public:
	using notify_t = Transfer::notify_t;
	using Future = ::le::utils::Future<notify_t>;

	VRAM(Device& device, Transfer::CreateInfo const& transferInfo = {});
	~VRAM();

	Buffer createBO(vk::DeviceSize size, vk::BufferUsageFlags usage, bool bHostVisible);
	template <typename T>
	Buffer createBO(T const& t, vk::BufferUsageFlags usage);

	[[nodiscard]] Future copy(Buffer const& src, Buffer& out_dst, vk::DeviceSize size = 0);
	[[nodiscard]] Future stage(Buffer& out_deviceBuffer, void const* pData, vk::DeviceSize size = 0);
	[[nodiscard]] Future copy(View<View<std::byte>> pixelsArr, Image& out_dst, LayoutPair layouts);

	template <typename Cont = std::initializer_list<Ref<Future const>>>
	void wait(Cont&& futures) const;
	void waitIdle();

	Ref<Device> m_device;

  private:
	Transfer m_transfer;
	struct {
		vk::PipelineStageFlags stages;
		vk::AccessFlags access;
	} m_post;

	friend class RenderContext;
};

// impl

template <typename T>
Buffer VRAM::createBO(T const& t, vk::BufferUsageFlags usage) {
	Buffer ret = createBO(sizeof(T), usage, true);
	ret.writeT(t);
	return ret;
}

template <typename Cont>
void VRAM::wait(Cont&& futures) const {
	for (Future const& f : futures) {
		f.wait();
	}
}
} // namespace le::graphics
