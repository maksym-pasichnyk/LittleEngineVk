#include <map>
#include <stdexcept>
#include <core/log.hpp>
#include <core/maths.hpp>
#include <core/utils.hpp>
#include <graphics/common.hpp>
#include <graphics/context/vram.hpp>
#include <graphics/render_context.hpp>

namespace le::graphics {
RenderContext::Render::Render(RenderContext& context, Frame&& frame) : m_frame(std::move(frame)), m_context(context) {
}

RenderContext::Render::Render(Render&& rhs) : m_frame(std::exchange(rhs.m_frame, Frame())), m_context(rhs.m_context) {
}

RenderContext::Render& RenderContext::Render::operator=(Render&& rhs) {
	if (&rhs != this) {
		destroy();
		m_frame = std::exchange(rhs.m_frame, Frame());
		m_context = rhs.m_context;
	}
	return *this;
}

RenderContext::Render::~Render() {
	destroy();
}

void RenderContext::Render::destroy() {
	if (!Device::default_v(m_frame.primary.m_cb)) {
		if (!m_context.get().endFrame()) {
			g_log.log(lvl::warning, 1, "[{}] RenderContext failed to end frame", g_name);
		}
	}
}

VertexInputInfo RenderContext::vertexInput(VertexInputCreateInfo const& info) {
	VertexInputInfo ret;
	u32 bindDelta = 0, locationDelta = 0;
	for (auto& type : info.types) {
		vk::VertexInputBindingDescription binding;
		binding.binding = u32(info.bindStart + bindDelta);
		binding.stride = (u32)type.size;
		binding.inputRate = type.inputRate;
		ret.bindings.push_back(binding);
		for (auto const& member : type.members) {
			vk::VertexInputAttributeDescription attribute;
			attribute.binding = u32(info.bindStart + bindDelta);
			attribute.format = member.format;
			attribute.offset = (u32)member.offset;
			attribute.location = u32(info.locationStart + locationDelta++);
			ret.attributes.push_back(attribute);
		}
		++bindDelta;
	}
	return ret;
}

VertexInputInfo RenderContext::vertexInput(QuickVertexInput const& info) {
	VertexInputInfo ret;
	vk::VertexInputBindingDescription binding;
	binding.binding = info.binding;
	binding.stride = (u32)info.size;
	binding.inputRate = vk::VertexInputRate::eVertex;
	ret.bindings.push_back(binding);
	u32 location = 0;
	for (auto const& [format, offset] : info.attributes) {
		vk::VertexInputAttributeDescription attribute;
		attribute.binding = info.binding;
		attribute.format = format;
		attribute.offset = (u32)offset;
		attribute.location = location++;
		ret.attributes.push_back(attribute);
	}
	return ret;
}

RenderContext::RenderContext(Swapchain& swapchain, u32 rotateCount, u32 secondaryCmdCount)
	: m_sync(swapchain.m_device, rotateCount, secondaryCmdCount), m_swapchain(swapchain), m_vram(swapchain.m_vram), m_device(swapchain.m_device) {
	m_storage.status = Status::eReady;
}

RenderContext::RenderContext(RenderContext&& rhs)
	: m_sync(std::move(rhs.m_sync)), m_storage(std::move(rhs.m_storage)), m_swapchain(rhs.m_swapchain), m_vram(rhs.m_vram), m_device(rhs.m_device) {
	rhs.m_storage.status = Status::eIdle;
}

RenderContext& RenderContext::operator=(RenderContext&& rhs) {
	if (&rhs != this) {
		m_sync = std::move(rhs.m_sync);
		m_storage = std::move(rhs.m_storage);
		m_swapchain = rhs.m_swapchain;
		m_vram = rhs.m_vram;
		m_device = rhs.m_device;
		rhs.m_storage.status = Status::eIdle;
	}
	return *this;
}

RenderContext::~RenderContext() {
	destroy();
}

bool RenderContext::waitForFrame() {
	if (!m_vram.get().m_transfer.polling()) {
		m_vram.get().m_transfer.update();
	}
	if (m_storage.status == Status::eReady) {
		return true;
	}
	if (m_storage.status != Status::eWaiting) {
		g_log.log(lvl::warning, 1, "[{}] Invalid RenderContext status", g_name);
		return false;
	}
	auto& sync = m_sync.get();
	m_device.get().waitFor(sync.sync.drawing);
	m_device.get().decrementDeferred();
	m_storage.status = Status::eReady;
	return true;
}

std::optional<RenderContext::Frame> RenderContext::beginFrame(CommandBuffer::PassInfo const& info) {
	if (m_storage.status != Status::eReady) {
		g_log.log(lvl::warning, 1, "[{}] Invalid RenderContext status", g_name);
		return std::nullopt;
	}
	if (m_swapchain.get().flags().any(Swapchain::Flag::ePaused | Swapchain::Flag::eOutOfDate)) {
		return std::nullopt;
	}
	FrameSync& sync = m_sync.get();
	auto target = m_swapchain.get().acquireNextImage(sync.sync);
	if (!target) {
		return std::nullopt;
	}
	m_storage.status = Status::eDrawing;
	m_device.get().destroy(sync.framebuffer);
	sync.framebuffer = m_device.get().createFramebuffer(m_swapchain.get().renderPass(), target->attachments(), target->extent);
	if (!sync.primary.commandBuffer.begin(m_swapchain.get().renderPass(), sync.framebuffer, target->extent, info)) {
		ENSURE(false, "Failed to begin recording command buffer");
		m_device.get().destroy(sync.framebuffer, sync.sync.drawReady);
		sync.sync.drawReady = m_device.get().createSemaphore(); // sync.drawReady will be signalled by acquireNextImage and cannot be reused
		return std::nullopt;
	}
	return Frame{*target, sync.primary.commandBuffer};
}

std::optional<RenderContext::Render> RenderContext::render(Colour clear, vk::ClearDepthStencilValue depth) {
	vk::ClearColorValue const c = std::array{clear.r.toF32(), clear.g.toF32(), clear.b.toF32(), clear.a.toF32()};
	CommandBuffer::PassInfo const pass{{c, depth}};
	if (auto frame = beginFrame(pass)) {
		return Render(*this, std::move(*frame));
	}
	return std::nullopt;
}

bool RenderContext::endFrame() {
	if (m_storage.status != Status::eDrawing) {
		g_log.log(lvl::warning, 1, "[{}] Invalid RenderContext status", g_name);
		return false;
	}
	FrameSync& sync = m_sync.get();
	sync.primary.commandBuffer.end();
	vk::SubmitInfo submitInfo;
	vk::PipelineStageFlags const waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &sync.sync.drawReady;
	submitInfo.pWaitDstStageMask = &waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &sync.primary.commandBuffer.m_cb;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &sync.sync.presentReady;
	m_device.get().device().resetFences(sync.sync.drawing);
	m_device.get().queues().submit(QType::eGraphics, submitInfo, sync.sync.drawing, false);
	m_storage.status = Status::eWaiting;
	auto present = m_swapchain.get().present(sync.sync);
	if (!present) {
		return false;
	}
	m_sync.next();
	return true;
}

bool RenderContext::reconstructed(glm::ivec2 framebufferSize) {
	auto const flags = m_swapchain.get().flags();
	if (flags.any(Swapchain::Flag::eOutOfDate | Swapchain::Flag::ePaused)) {
		if (m_swapchain.get().reconstruct(framebufferSize)) {
			m_storage.status = Status::eWaiting;
			return true;
		}
	}
	return false;
}

View<Pipeline> RenderContext::makePipeline(std::string_view id, Pipeline::CreateInfo createInfo) {
	createInfo.dynamicState.renderPass = m_swapchain.get().renderPass();
	Pipeline p0(m_vram, std::move(createInfo), id);
	auto [iter, bResult] = m_storage.pipes.emplace(id, std::move(p0));
	if (!bResult || iter == m_storage.pipes.end()) {
		throw std::runtime_error("Map insertion failure");
	}
	g_log.log(lvl::info, 1, "[{}] Pipeline [{}] constructed", g_name, id);
	return iter->second;
}

bool RenderContext::destroyPipeline(Hash id) {
	if (auto search = m_storage.pipes.find(id); search != m_storage.pipes.end()) {
		m_storage.pipes.erase(id);
		return true;
	}
	return false;
}

bool RenderContext::hasPipeline(Hash id) const noexcept {
	return m_storage.pipes.find(id) != m_storage.pipes.end();
}

View<Pipeline> RenderContext::pipeline(Hash id) {
	if (auto search = m_storage.pipes.find(id); search != m_storage.pipes.end()) {
		return search->second;
	}
	return {};
}

glm::mat4 RenderContext::preRotate() const noexcept {
	glm::mat4 ret(1.0f);
	f32 rad = 0.0f;
	auto const transform = m_swapchain.get().display().transform;
	if (transform == vk::SurfaceTransformFlagBitsKHR::eIdentity) {
		return ret;
	} else if (transform == vk::SurfaceTransformFlagBitsKHR::eRotate90) {
		rad = glm::radians(90.0f);
	} else if (transform == vk::SurfaceTransformFlagBitsKHR::eRotate180) {
		rad = glm::radians(180.0f);
	} else if (transform == vk::SurfaceTransformFlagBitsKHR::eRotate180) {
		rad = glm::radians(270.0f);
	}
	return glm::rotate(ret, rad, g_nFront);
}

vk::Viewport RenderContext::viewport(glm::ivec2 extent, glm::vec2 const& depth, ScreenRect const& nRect) const noexcept {
	if (!Swapchain::valid(extent)) {
		extent = this->extent();
	}
	vk::Viewport ret;
	glm::vec2 const size = nRect.size();
	ret.minDepth = depth.x;
	ret.maxDepth = depth.y;
	ret.width = size.x * (f32)extent.x;
	ret.height = -(size.y * (f32)extent.y); // flip viewport about X axis
	ret.x = nRect.lt.x * (f32)extent.x;
	ret.y = nRect.lt.y * (f32)extent.y - ret.height;
	return ret;
}

vk::Rect2D RenderContext::scissor(glm::ivec2 extent, ScreenRect const& nRect) const noexcept {
	if (!Swapchain::valid(extent)) {
		extent = this->extent();
	}
	vk::Rect2D scissor;
	glm::vec2 const size = nRect.size();
	scissor.offset.x = (s32)(nRect.lt.x * (f32)extent.x);
	scissor.offset.y = (s32)(nRect.lt.y * (f32)extent.y);
	scissor.extent.width = (u32)(size.x * (f32)extent.x);
	scissor.extent.height = (u32)(size.y * (f32)extent.y);
	return scissor;
}

void RenderContext::destroy() {
	m_storage.pipes.clear();
}
} // namespace le::graphics
