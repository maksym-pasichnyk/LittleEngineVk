#pragma once
#include <variant>
#include <core/colour.hpp>
#include <glm/vec2.hpp>
#include <graphics/bitmap.hpp>
#include <graphics/context/vram.hpp>
#include <graphics/utils/deferred.hpp>
#include <kt/fixed_vector/fixed_vector.hpp>

namespace le::graphics {
class Sampler {
  public:
	using MinMag = TPair<vk::Filter>;
	static vk::SamplerCreateInfo info(MinMag minMag, vk::SamplerMipmapMode mip = vk::SamplerMipmapMode::eLinear);

	Sampler(not_null<Device*> device, vk::SamplerCreateInfo const& info);
	Sampler(not_null<Device*> device, MinMag minMag, vk::SamplerMipmapMode mip = vk::SamplerMipmapMode::eLinear);

	vk::Sampler sampler() const noexcept { return *m_sampler; }

  private:
	Deferred<vk::Sampler> m_sampler;
	not_null<Device*> m_device;
};

class Texture {
  public:
	enum class Type { e2D, eCube };
	enum class Payload { eColour, eData };

	struct Data {
		vk::ImageView imageView;
		vk::Sampler sampler;
		vk::Format format{};
		Extent2D size{};
		Payload payload{};
		Type type{};
	};

	using Img = BmpBytes;
	using Cube = CubeBytes;
	struct CreateInfo;

	inline static constexpr auto srgb = vk::Format::eR8G8B8A8Srgb;
	inline static constexpr auto linear = vk::Format::eR8G8B8A8Unorm;

	Texture(not_null<VRAM*> vram);

	static Img img(Span<std::byte const> bytes);
	static Cubemap unitCubemap(Colour colour);

	bool construct(CreateInfo const& info);

	bool valid() const noexcept;
	bool busy() const;
	bool ready() const;
	void wait() const;

	Data const& data() const noexcept;
	Image const& image() const;

	not_null<VRAM*> m_vram;

  private:
	struct Storage {
		std::optional<Image> image;
		Deferred<vk::ImageView> view;
		Data data;
		VRAM::Future transfer;
	};

	bool construct(CreateInfo const& info, Storage& out_storage);

	Storage m_storage;
};

struct Texture::CreateInfo {
	using Data = std::variant<Img, Cube, Bitmap, Cubemap>;

	Data data;
	vk::Sampler sampler;
	std::optional<vk::Format> forceFormat;
	Payload payload = Payload::eColour;
};
} // namespace le::graphics
