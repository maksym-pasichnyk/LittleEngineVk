#pragma once
#include <ktl/fixed_pimpl.hpp>
#include <levk/core/bitmap.hpp>
#include <levk/core/not_null.hpp>
#include <levk/core/span.hpp>
#include <levk/graphics/font/glyph.hpp>
#include <memory>

namespace le::graphics {
class Device;

class FontFace {
  public:
	enum struct Height : u32 { eDefault = 64U };

	struct Slot {
		Bitmap pixmap;
		glm::ivec2 topLeft{};
		glm::ivec2 advance{};
		Codepoint codepoint;

		bool hasBitmap() const noexcept { return pixmap.extent.x > 0 && pixmap.extent.y > 0; }
	};

	FontFace(not_null<Device*> device);
	FontFace(FontFace&&) noexcept;
	FontFace& operator=(FontFace&&) noexcept;
	~FontFace() noexcept;

	bool load(Span<std::byte const> ttf, Height height) noexcept;
	explicit operator bool() const noexcept;

	Slot const& slot(Codepoint cp) noexcept;
	Height height() const noexcept;

	std::size_t slotCount() const noexcept;
	void clearSlots() noexcept;

  private:
	struct Impl;
	ktl::fixed_pimpl<Impl, 128> m_impl;
	not_null<Device*> m_device;
};
} // namespace le::graphics
