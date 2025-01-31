#pragma once
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <glm/vec2.hpp>
#include <levk/core/bitmap.hpp>
#include <levk/core/span.hpp>
#include <levk/core/std_types.hpp>
#include <levk/core/utils/unique.hpp>

namespace le::graphics {
struct FTLib {
	FT_Library lib{};

	static FTLib make() noexcept;

	explicit operator bool() const noexcept { return lib != nullptr; }
	constexpr bool operator==(FTLib const&) const = default;
};

struct FTFace {
	using ID = u32;

	FT_Face face{};

	static FTFace make(FTLib const& lib, Span<std::byte const> bytes) noexcept;
	static FTFace make(FTLib const& lib, char const* path) noexcept;

	explicit operator bool() const noexcept { return face != nullptr; }
	constexpr bool operator==(FTFace const&) const = default;

	bool setCharSize(glm::uvec2 size = {0U, 16U * 64U}, glm::uvec2 res = {300U, 300U}) const noexcept;
	bool setPixelSize(glm::uvec2 size = {0U, 16U}) const noexcept;

	ID glyphIndex(u32 codepoint) const noexcept;
	bool loadGlyph(ID index, FT_Render_Mode mode = FT_RENDER_MODE_NORMAL) const;
	Extent2D glyphExtent() const;
	std::vector<u8> buildGlyphImage() const;
};

struct FTDeleter {
	void operator()(FTLib const& lib) const noexcept;
	void operator()(FTFace const& face) const noexcept;
};

template <typename T, typename D = FTDeleter>
using FTUnique = Unique<T, D>;
} // namespace le::graphics
