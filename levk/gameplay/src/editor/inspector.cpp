#include <editor/sudo.hpp>
#include <levk/core/services.hpp>
#include <levk/core/utils/algo.hpp>
#include <levk/engine/assets/asset_provider.hpp>
#include <levk/engine/engine.hpp>
#include <levk/engine/render/no_draw.hpp>
#include <levk/engine/render/pipeline.hpp>
#include <levk/gameplay/cameras/freecam.hpp>
#include <levk/gameplay/editor/inspector.hpp>
#include <levk/gameplay/gui/view.hpp>
#include <levk/gameplay/gui/widget.hpp>

namespace le::editor {
#if defined(LEVK_USE_IMGUI)
namespace {
struct TransformWidget {
	void operator()(Transform& transform) const {
		TWidget<Transform> tr("Pos", "Orn", "Scl", transform);
		Styler s{Style::eSeparator};
	}
};

struct GuiRect {
	void operator()(gui::Rect& rect) const {
		auto t = Text("Rect");
		TWidget<glm::vec2> org("Origin", rect.origin, false);
		TWidget<glm::vec2> size("Size", rect.size, false);
		t = Text("Anchor");
		TWidget<glm::vec2> ann("Norm", rect.anchor.norm, false, 0.001f);
		TWidget<glm::vec2> ano("Offset", rect.anchor.offset, false);
		Styler s{Style::eSeparator};
	}
};

struct GuiViewWidget {
	void operator()(gui::View& view) const {
		Text t("View");
		bool block = view.m_block == gui::View::Block::eBlock;
		TWidget<bool> blk("Block", block);
		view.m_block = block ? gui::View::Block::eBlock : gui::View::Block::eNone;
		Styler s{Style::eSeparator};
	}

	void operator()(gui::Widget& widget) const {
		Text t("Widget");
		TWidget<bool> blk("Interact", widget.m_interact);
		Styler s{Style::eSeparator};
	}
};

struct GuiNode {
	void operator()(gui::TreeNode& node) const {
		Text t("Node");
		TWidget<bool> active("Active", node.m_active);
		TWidget<f32> zi("Z-Index", node.m_zIndex);
		TWidget<glm::quat> orn("Orient", node.m_orientation);
		Styler s{Style::eSeparator};
	}
};

bool shouldDraw(dens::entity e, dens::registry const& r) { return !r.attached<NoDraw>(e); }

void shouldDraw(dens::entity e, dens::registry& r, bool set) {
	if (set) {
		r.detach<NoDraw>(e);
	} else {
		r.attach<NoDraw>(e);
	}
}
} // namespace
#endif

bool Inspector::detach(std::string const& id) {
	auto doDetach = [id](auto& map) {
		if (auto it = map.find(id); it != map.end()) {
			map.erase(it);
			return true;
		}
		return false;
	};
	return doDetach(s_gadgets) || doDetach(s_guiGadgets);
}

void Inspector::update([[maybe_unused]] SceneRef const& scene) {
#if defined(LEVK_USE_IMGUI)
	if (scene.valid()) {
		auto inspect = Sudo::inspect(scene);
		auto store = Services::get<AssetStore>();
		if (inspect->entity != dens::entity()) {
			auto& reg = *Sudo::registry(scene);
			if (!inspect->tree) {
				Text(reg.name(inspect->entity));
				Text(CStr<16>("id: {}", inspect->entity.id));
				if (reg.attached<RenderPipeline>(inspect->entity) || reg.attached<RenderPipeProvider>(inspect->entity)) {
					TWidgetWrap<bool> draw;
					if (draw(shouldDraw(inspect->entity, reg), "Draw", draw.out)) { shouldDraw(inspect->entity, reg, draw.out); }
				}
				Styler s{Style::eSeparator};
				if (auto transform = reg.find<Transform>(inspect->entity)) { TransformWidget{}(*transform); }
				attach(inspect->entity, reg, *store);
			} else {
				auto const name = CStr<128>("{} -> [GUI node]", reg.name(inspect->entity));
				Text txt(name);
				GuiRect{}(inspect->tree->m_rect);
				if (auto view = dynamic_cast<gui::View*>(inspect->tree)) {
					GuiViewWidget{}(*view);
				} else if (auto node = dynamic_cast<gui::TreeNode*>(inspect->tree)) {
					GuiNode{}(*node);
					if (auto widget = dynamic_cast<gui::Widget*>(inspect->tree)) { GuiViewWidget{}(*widget); }
				}
				for (auto const& [id, gadget] : s_guiGadgets) { gadget->inspect(id, inspect->entity, reg, *store, inspect->tree); }
			}
		}
	}
#endif
}

void Inspector::clear() {
	s_gadgets.clear();
	s_guiGadgets.clear();
}

void Inspector::attach(dens::entity entity, dens::registry& reg, AssetStore const& store) {
	std::vector<GadgetMap::const_iterator> attachable;
	attachable.reserve(s_gadgets.size());
	for (auto it = s_gadgets.cbegin(); it != s_gadgets.cend(); ++it) {
		auto const& [id, gadget] = *it;
		if (!gadget->inspect(id, entity, reg, store, {}) && gadget->attachable()) { attachable.push_back(it); }
	}
	if (!attachable.empty()) {
		Styler(glm::vec2{0.0f, 30.0f});
		if (Button("Attach")) { Popup::open("attach_component"); }
		if (auto attach = Popup("attach_component")) {
			static CStr<128> s_filter;
			editor::TWidget<char*>("Search##component_filter", s_filter.c_str(), s_filter.capacity());
			auto filter = s_filter.get();
			for (auto const& kvp : attachable) {
				auto const& [id, gadget] = *kvp;
				Pane sub("component_list", {0.0f, 80.0f});
				if ((filter.empty() || id.find(filter) != std::string_view::npos) && Selectable(id.data())) {
					gadget->attach(entity, reg);
					attach.close();
				}
			}
		}
	}
}
} // namespace le::editor
