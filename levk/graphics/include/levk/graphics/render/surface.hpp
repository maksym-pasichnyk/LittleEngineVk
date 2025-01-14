#pragma once
#include <ktl/enum_flags/enum_flags.hpp>
#include <ktl/fixed_vector.hpp>
#include <levk/core/not_null.hpp>
#include <levk/core/span.hpp>
#include <levk/graphics/common.hpp>
#include <levk/graphics/render/framebuffer.hpp>
#include <levk/graphics/render/vsync.hpp>
#include <optional>

namespace le::graphics {
class VRAM;

class Surface {
  public:
	struct Format {
		vk::SurfaceFormatKHR colour;
		vk::Format depth;
		VSync vsync{};
	};

	struct Acquire {
		RenderTarget image;
		std::uint32_t index{};
	};

	struct Sync {
		vk::Semaphore wait;
		vk::Semaphore ssignal;
		vk::Fence fsignal;
	};

	using VSyncs = ktl::enum_flags<VSync, u8>;

	Surface(not_null<VRAM*> vram, Extent2D fbSize = {}, std::optional<VSync> vsync = std::nullopt);
	Surface(Surface&&) = default;
	Surface& operator=(Surface&&) = default;
	~Surface();

	static constexpr bool srgb(vk::Format format) noexcept;
	static constexpr bool rgba(vk::Format format) noexcept;
	static constexpr bool bgra(vk::Format format) noexcept;
	static constexpr bool valid(glm::ivec2 framebufferSize) noexcept { return framebufferSize.x > 0 && framebufferSize.y > 0; }
	static constexpr bool outOfDate(vk::Result result) noexcept;

	VRAM& vram() const noexcept { return *m_vram; }
	VSyncs vsyncs() const noexcept { return m_vsyncs; }
	Format const& format() const noexcept { return m_storage.format; }
	Extent2D extent() const noexcept { return cast(m_storage.info.extent); }
	u32 imageCount() const noexcept { return m_storage.info.imageCount; }
	u32 minImageCount() const noexcept { return m_storage.info.minImageCount; }
	BlitFlags blitFlags() const noexcept { return m_storage.blitFlags; }
	vk::ImageUsageFlags usage() const noexcept { return m_createInfo.imageUsage; }

	bool makeSwapchain(Extent2D fbSize = {}, std::optional<VSync> vsync = std::nullopt);

	std::optional<Acquire> acquireNextImage(Extent2D fbSize, vk::Semaphore signal);
	vk::Result submit(Span<vk::CommandBuffer const> cbs, Sync const& sync) const;
	vk::Result present(Extent2D fbSize, Acquire image, vk::Semaphore wait);
	RenderTarget const& lastDrawn() const noexcept { return m_storage.lastDrawn; }

  private:
	struct Info {
		vk::Extent2D extent{};
		u32 imageCount{};
		u32 minImageCount{};
	};

	struct Swapchain {
		vk::UniqueSwapchainKHR swapchain;
		ktl::fixed_vector<vk::UniqueImageView, 8> imageViews;
	};

	struct Storage {
		Swapchain swapchain;
		ktl::fixed_vector<RenderTarget, 8> images;
		Format format;
		Info info;
		BlitFlags blitFlags;
		RenderTarget lastDrawn;
	};

	Info makeInfo(Extent2D extent) const;
	std::optional<Storage> makeStorage(Storage const& retired, VSync vsync);

	vk::UniqueSurfaceKHR m_surface;
	Storage m_storage;
	Swapchain m_retired;
	vk::SwapchainCreateInfoKHR m_createInfo;
	VSyncs m_vsyncs;
	not_null<VRAM*> m_vram;
};

// impl

constexpr bool Surface::srgb(vk::Format format) noexcept {
	switch (format) {
	case vk::Format::eR8G8B8Srgb:
	case vk::Format::eB8G8R8Srgb:
	case vk::Format::eR8G8B8A8Srgb:
	case vk::Format::eB8G8R8A8Srgb:
	case vk::Format::eA8B8G8R8SrgbPack32: return true;
	default: return false;
	}
}

constexpr bool Surface::rgba(vk::Format format) noexcept {
	switch (format) {
	case vk::Format::eR8G8B8A8Uint:
	case vk::Format::eR8G8B8A8Unorm:
	case vk::Format::eR8G8B8A8Sint:
	case vk::Format::eR8G8B8A8Snorm:
	case vk::Format::eR8G8B8A8Uscaled:
	case vk::Format::eR8G8B8A8Srgb: return true;
	default: return false;
	}
}

constexpr bool Surface::bgra(vk::Format format) noexcept {
	switch (format) {
	case vk::Format::eB8G8R8A8Uint:
	case vk::Format::eB8G8R8A8Unorm:
	case vk::Format::eB8G8R8A8Sint:
	case vk::Format::eB8G8R8A8Snorm:
	case vk::Format::eB8G8R8A8Uscaled:
	case vk::Format::eB8G8R8A8Srgb: return true;
	default: return false;
	}
}

constexpr bool Surface::outOfDate(vk::Result result) noexcept {
	switch (result) {
	case vk::Result::eErrorOutOfDateKHR: return true;
	case vk::Result::eSuboptimalKHR: return true;
	default: break;
	}
	return false;
}
} // namespace le::graphics
