#pragma once
#include <core/span.hpp>
#include <engine/render/prop.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

namespace le {
struct Rect2D {
	glm::uvec2 extent{};
	glm::ivec2 offset{};
	bool set{};
};

struct Drawable {
	glm::mat4 model = glm::mat4(1.0f);
	Rect2D scissor;
	Span<Prop const> props;
};

struct NoDraw {};
} // namespace le