#include <core/services.hpp>
#include <engine/assets/asset_store.hpp>
#include <engine/gui/tree.hpp>
#include <engine/input/space.hpp>
#include <engine/render/viewport.hpp>
#include <graphics/font/font.hpp>
#include <algorithm>

namespace le::gui {
DrawScissor scissor(input::Space const& space, glm::vec2 centre, glm::vec2 halfSize, bool normalised) noexcept {
	return {space.project(centre - halfSize, normalised) * space.render.scale + 0.5f, space.project(centre + halfSize, normalised) * space.render.scale + 0.5f};
}

not_null<graphics::VRAM*> TreeRoot::vram() const { return Services::get<graphics::VRAM>(); }

graphics::Font* TreeRoot::findFont(Hash uri) const {
	if (auto store = Services::find<AssetStore>()) {
		if (auto font = store->find<Font>(uri)) { return &*font; }
	}
	return {};
}

void TreeNode::update(input::Space const& space) {
	m_rect.adjust(m_parent->m_rect);
	m_scissor = scissor(space);
	onUpdate(space);
	for (auto& node : m_ts) { node->update(space); }
}
} // namespace le::gui
