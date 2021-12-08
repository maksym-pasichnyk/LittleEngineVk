#pragma once
#include <engine/render/descriptor_helper.hpp>
#include <engine/render/draw_list.hpp>
#include <engine/render/pipeline.hpp>
#include <graphics/render/pipeline_factory.hpp>
#include <graphics/render/renderer.hpp>

namespace dens {
class registry;
}

namespace le {
class ListRenderer {
  public:
	using PipelineFactory = graphics::PipelineFactory;
	using Pipeline = graphics::Pipeline;
	using RenderPass = graphics::RenderPass;
	using LayerMap = std::unordered_map<RenderPipeline, std::vector<Drawable>, RenderPipeline::Hasher>;

	static constexpr vk::Rect2D cast(DrawScissor rect) noexcept { return {{rect.offset.x, rect.offset.y}, {rect.extent.x, rect.extent.y}}; }
	static graphics::PipelineSpec pipelineSpec(RenderPipeline const& rp);
	static void add(LayerMap& out_map, RenderPipeline const& rp, glm::mat4 const& model, MeshView const& mesh, DrawScissor scissor = {});

	void render(RenderPass& out_rp, LayerMap map);

  protected:
	virtual void fill(LayerMap& out_map, dens::registry const& registry);
	virtual void draw(DescriptorBinder bind, DrawList const& list, graphics::CommandBuffer const& cb) const;

	virtual void writeSets(DescriptorMap map, DrawList const& list) = 0;
};
} // namespace le