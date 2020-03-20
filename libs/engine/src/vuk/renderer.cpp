#include <glm/gtc/matrix_transform.hpp>
#include "core/log.hpp"
#include "core/utils.hpp"
#include "presenter.hpp"
#include "info.hpp"
#include "renderer.hpp"
#include "utils.hpp"

namespace le::vuk
{
std::string const Renderer::s_tName = utils::tName<Renderer>();

Renderer::Shared const Renderer::s_shared = {UBOData{sizeof(ubo::View)}};

Renderer::Renderer(Presenter* pPresenter, u8 frameCount) : m_pPresenter(pPresenter), m_window(pPresenter->m_window)
{
	create(frameCount);
}

Renderer::~Renderer()
{
	destroy();
}

void Renderer::create(u8 frameCount)
{
	if (m_frames.empty() && frameCount > 0)
	{
		m_frameCount = frameCount;
		if (m_callbackTokens.empty())
		{
			m_callbackTokens = {m_pPresenter->registerDestroyed([this]() { destroy(); }),
								m_pPresenter->registerSwapchainRecreated([this]() { reset(); })};
		}
		// Descriptor Pool
		vk::DescriptorPoolSize descPoolSize;
		descPoolSize.type = vk::DescriptorType::eUniformBuffer;
		descPoolSize.descriptorCount = frameCount;
		vk::DescriptorPoolCreateInfo createInfo;
		createInfo.poolSizeCount = 1;
		createInfo.pPoolSizes = &descPoolSize;
		createInfo.maxSets = frameCount;
		m_descriptorPool = g_info.device.createDescriptorPool(createInfo);
		// Descriptor Sets
		vk::DescriptorSetAllocateInfo allocInfo;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = frameCount;
		std::vector const layouts((size_t)frameCount, g_info.matricesLayout);
		allocInfo.pSetLayouts = layouts.data();
		BufferData data;
		data.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
		data.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		auto sets = g_info.device.allocateDescriptorSets(allocInfo);
		for (u8 idx = 0; idx < frameCount; ++idx)
		{
			Renderer::FrameSync frame;
			frame.renderReady = g_info.device.createSemaphore({});
			frame.presentReady = g_info.device.createSemaphore({});
			vk::FenceCreateInfo createInfo;
			// createInfo.flags = vk::FenceCreateFlagBits::eSignaled;
			frame.drawing = g_info.device.createFence(createInfo);
			frame.ubos.view.descriptorSet = sets.at((size_t)idx);
			data.size = s_shared.uboView.size;
			frame.ubos.view.buffer = createBuffer(data);
			frame.ubos.view.offset = 0;
			vk::DescriptorBufferInfo bufferInfo;
			bufferInfo.buffer = frame.ubos.view.buffer.resource;
			bufferInfo.offset = 0;
			bufferInfo.range = s_shared.uboView.size;
			vk::WriteDescriptorSet descWrite;
			descWrite.dstSet = frame.ubos.view.descriptorSet;
			descWrite.dstBinding = 0;
			descWrite.dstArrayElement = 0;
			descWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
			descWrite.descriptorCount = 1;
			descWrite.pBufferInfo = &bufferInfo;
			g_info.device.updateDescriptorSets(descWrite, {});
			// Commands
			vk::CommandPoolCreateInfo commandPoolCreateInfo;
			commandPoolCreateInfo.queueFamilyIndex = g_info.queueFamilyIndices.graphics;
			commandPoolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
			frame.commandPool = g_info.device.createCommandPool(commandPoolCreateInfo);
			vk::CommandBufferAllocateInfo allocInfo;
			allocInfo.commandPool = frame.commandPool;
			allocInfo.level = vk::CommandBufferLevel::ePrimary;
			allocInfo.commandBufferCount = 1;
			frame.commandBuffer = g_info.device.allocateCommandBuffers(allocInfo).front();
			m_frames.push_back(std::move(frame));
		}
		LOG_D("[{}:{}] created", s_tName, m_window);
	}
	return;
}

void Renderer::destroy()
{
	if (!m_frames.empty())
	{
		for (auto& frame : m_frames)
		{
			vkDestroy(frame.commandPool, frame.drawing, frame.renderReady, frame.presentReady, frame.ubos.view.buffer);
		}
		vkDestroy(m_descriptorPool);
		m_descriptorPool = vk::DescriptorPool();
		m_frames.clear();
		m_index = 0;
		LOG_D("[{}:{}] destroyed", s_tName, m_window);
	}
	return;
}

void Renderer::reset()
{
	if (m_frameCount > 0)
	{
		create(m_frameCount);
		g_info.device.waitIdle();
		for (auto& frame : m_frames)
		{
			frame.bNascent = true;
			g_info.device.resetFences(frame.drawing);
			g_info.device.resetCommandPool(frame.commandPool, vk::CommandPoolResetFlagBits::eReleaseResources);
		}
		LOG_D("[{}:{}] reset", s_tName, m_window);
	}
	return;
}

bool Renderer::render(Write write, Draw draw, ClearValues const& clear)
{
	auto& sync = frameSync();
	if (!sync.bNascent)
	{
		vuk::wait(sync.drawing);
	}
	FrameDriver::UBOs ubos{sync.ubos.view};
	// Acquire
	auto [bResult, acquire] = m_pPresenter->acquireNextImage(sync.renderReady, sync.drawing);
	if (!bResult)
	{
		return false;
	}
	sync.commandBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
	std::array const clearColour = {clear.colour.r.toF32(), clear.colour.g.toF32(), clear.colour.b.toF32(), clear.colour.a.toF32()};
	vk::ClearColorValue const colour = clearColour;
	vk::ClearDepthStencilValue const depth = {clear.depthStencil.x, (u32)clear.depthStencil.y};
	std::array<vk::ClearValue, 2> const clearValues = {colour, depth};
	vk::RenderPassBeginInfo renderPassInfo;
	renderPassInfo.renderPass = acquire.renderPass;
	renderPassInfo.framebuffer = acquire.framebuffer;
	renderPassInfo.renderArea.extent = acquire.swapchainExtent;
	renderPassInfo.clearValueCount = (u32)clearValues.size();
	renderPassInfo.pClearValues = clearValues.data();
	if (write)
	{
		write(ubos);
	}
	// Begin
	auto const& commandBuffer = sync.commandBuffer;
	vk::CommandBufferBeginInfo beginInfo;
	commandBuffer.begin(beginInfo);
	commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
	FrameDriver driver{ubos, commandBuffer};
	draw(driver);
	commandBuffer.endRenderPass();
	commandBuffer.end();
	// Submit
	vk::SubmitInfo submitInfo;
	vk::PipelineStageFlags const waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &sync.renderReady;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &sync.presentReady;
	g_info.device.resetFences(sync.drawing);
	g_info.queues.graphics.submit(submitInfo, sync.drawing);
	if (m_pPresenter->present(sync.presentReady))
	{
		next();
		return true;
	}
	return false;
}

vk::Viewport Renderer::transformViewport(ScreenRect const& nRect, glm::vec2 const& depth) const
{
	vk::Viewport viewport;
	auto const& extent = m_pPresenter->m_swapchain.extent;
	glm::vec2 const size = {nRect.right - nRect.left, nRect.bottom - nRect.top};
	viewport.minDepth = depth.x;
	viewport.maxDepth = depth.y;
	viewport.width = size.x * extent.width;
	viewport.height = size.y * extent.height;
	viewport.x = nRect.left * extent.width;
	viewport.y = nRect.top * extent.height;
	return viewport;
}

vk::Rect2D Renderer::transformScissor(ScreenRect const& nRect) const
{
	vk::Rect2D scissor;
	glm::vec2 const size = {nRect.right - nRect.left, nRect.bottom - nRect.top};
	scissor.offset.x = (s32)(nRect.left * m_pPresenter->m_swapchain.extent.width);
	scissor.offset.y = (s32)(nRect.top * m_pPresenter->m_swapchain.extent.height);
	scissor.extent.width = (u32)(size.x * m_pPresenter->m_swapchain.extent.width);
	scissor.extent.height = (u32)(size.y * m_pPresenter->m_swapchain.extent.height);
	return scissor;
}

Renderer::FrameSync& Renderer::frameSync()
{
	ASSERT(m_index < m_frames.size(), "Invalid index!");
	return m_frames.at(m_index);
}

void Renderer::next()
{
	m_frames.at(m_index).bNascent = false;
	m_index = (m_index + 1) % m_frames.size();
	return;
}
} // namespace le::vuk
