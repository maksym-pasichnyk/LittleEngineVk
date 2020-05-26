#pragma once
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include "core/std_types.hpp"
#include "engine/window/common.hpp"

namespace le::gfx
{
class PipelineImpl final
{
public:
	struct Info final
	{
		vk::RenderPass renderPass;
		vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
		vk::CullModeFlagBits cullMode = vk::CullModeFlagBits::eNone;
		vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
		vk::ColorComponentFlags colourWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
												  | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		vk::DescriptorSetLayout samplerLayout;
		std::string name;
		std::string shaderID;
		std::set<vk::DynamicState> dynamicStates;
		std::vector<vk::PushConstantRange> pushConstantRanges;
		f32 staticLineWidth = 1.0f;
		WindowID window;
		class Shader* pShader = nullptr;
		bool bBlend = true;
		bool bDepthTest = true;
		bool bDepthWrite = true;
	};

public:
	static std::string const s_tName;

public:
	vk::Pipeline m_pipeline;
	vk::PipelineLayout m_layout;

private:
	Info m_info;
	class Pipeline* m_pPipeline;
	bool m_bOutOfDate = false;

public:
	PipelineImpl(Pipeline* pPipeline);
	PipelineImpl(PipelineImpl&&);
	PipelineImpl& operator=(PipelineImpl&&);
	~PipelineImpl();

public:
	bool create(Info info);
	bool update(vk::DescriptorSetLayout samplerLayout);
	void destroy();

	void update();

private:
	bool create(vk::Pipeline& out_pipeline, vk::PipelineLayout& out_layout);
	void destroy(vk::Pipeline& out_pipeline, vk::PipelineLayout& out_layout);
};
} // namespace le::gfx
