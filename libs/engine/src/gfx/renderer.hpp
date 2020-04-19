#pragma once
#include <deque>
#include <glm/glm.hpp>
#include "core/delegate.hpp"
#include "engine/window/common.hpp"
#include "presenter.hpp"
#include "draw/pipeline.hpp"
#include "draw/resource_descriptors.hpp"

namespace le
{
class WindowImpl;
}

namespace le::gfx
{
class Pipeline;

class Renderer final
{
public:
	struct Info final
	{
		PresenterInfo presenterInfo;
		WindowID windowID;
		u8 frameCount = 2;
	};

	struct FrameDriver final
	{
		rd::Set set;
		vk::CommandBuffer commandBuffer;
	};

	using Write = std::function<void(rd::Set&)>;
	using Draw = std::function<std::vector<Pipeline*>(FrameDriver const&)>;

private:
	struct FrameSync final
	{
		rd::Set set;
		vk::Semaphore renderReady;
		vk::Semaphore presentReady;
		vk::Fence drawing;
		vk::Framebuffer framebuffer;
		vk::CommandBuffer commandBuffer;
		vk::CommandPool commandPool;
		bool bNascent = true;
	};

public:
	static std::string const s_tName;
	std::string m_name;

private:
	Presenter m_presenter;
	vk::DescriptorPool m_descriptorPool;
	std::deque<Pipeline> m_pipelines;
	std::vector<FrameSync> m_frames;

	size_t m_index = 0;
	WindowID m_window;
	u8 m_frameCount = 0;

public:
	Renderer(Info const& info);
	~Renderer();

public:
	void create(u8 frameCount = 2);
	void destroy();
	void reset();

	Pipeline* createPipeline(Pipeline::Info info);

	void update();
	bool render(Write write, Draw draw, ClearValues const& clear);

public:
	vk::Viewport transformViewport(ScreenRect const& nRect = {}, glm::vec2 const& depth = {0.0f, 1.0f}) const;
	vk::Rect2D transformScissor(ScreenRect const& nRect = {}) const;

private:
	void onFramebufferResize();
	FrameSync& frameSync();
	void next();

	friend class le::WindowImpl;
};
} // namespace le::gfx
