#pragma once
#include <levk/graphics/buffer.hpp>
#include <levk/graphics/image.hpp>

namespace le::graphics {
struct Scratch {
	std::optional<Buffer> buffer;
	std::optional<Image> image;
};
} // namespace le::graphics
