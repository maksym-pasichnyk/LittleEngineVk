#include <levk/core/log_channel.hpp>
#include <levk/core/utils/algo.hpp>
#include <levk/graphics/common.hpp>
#include <levk/graphics/device/device.hpp>
#include <levk/graphics/device/transfer.hpp>

namespace le::graphics {
namespace {
constexpr vk::DeviceSize ceilPOT(vk::DeviceSize size) noexcept {
	vk::DeviceSize ret = 2;
	while (ret < size) { ret <<= 1; }
	return ret;
}
} // namespace

Transfer::Transfer(not_null<Memory*> memory, CreateInfo const& info) : m_memory(memory) {
	vk::CommandPoolCreateInfo poolInfo;
	poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	poolInfo.queueFamilyIndex = memory->m_device->queues().transfer().family();
	m_data.pool = memory->m_device->device().createCommandPool(poolInfo);
	m_sync.staging = ktl::kthread([this, r = info.reserve]() {
		{
			std::scoped_lock lock(m_sync.mutex);
			for (auto const& range : r) {
				for (auto i = range.count; i > 0; --i) { m_data.buffers.push_back(makeStagingBuffer(range.size)); }
			}
		}
		logI(LC_LibUser, "[{}] Transfer thread started", g_name);
		while (auto f = m_queue.pop()) { (*f)(); }
		logI(LC_LibUser, "[{}] Transfer thread completed", g_name);
	});
	if (info.autoPollRate && *info.autoPollRate > 0ms) {
		m_sync.poll = ktl::kthread([this, rate = *info.autoPollRate](ktl::kthread::stop_t stop) {
			logI(LC_LibUser, "[{}] Transfer poll thread started", g_name);
			while (!stop.stop_requested()) {
				update();
				ktl::kthread::sleep_for(rate);
			}
			logI(LC_LibUser, "[{}] Transfer poll thread completed", g_name);
		});
	}
	logI(LC_LibUser, "[{}] Transfer constructed", g_name);
}

Transfer::~Transfer() {
	stopTransfer();
	stopPolling();
	m_sync.staging = {};
	Memory& m = *m_memory;
	Device& d = *m.m_device;
	d.waitIdle();
	d.destroy(m_data.pool);
	for (auto& fence : m_data.fences) { d.destroy(fence); }
	for (auto& batch : m_batches.submitted) { d.destroy(batch.done); }
	m_data = {};
	m_batches = {};
	d.waitIdle(); // force flush deferred
	logI(LC_LibUser, "[{}] Transfer destroyed", g_name);
}

Buffer Transfer::makeStagingBuffer(vk::DeviceSize size) const {
	Buffer::CreateInfo info;
	info.size = ceilPOT(size);
	info.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
	info.usage = vk::BufferUsageFlagBits::eTransferSrc;
	info.qcaps = QType::eGraphics;
	info.vmaUsage = VMA_MEMORY_USAGE_CPU_ONLY;
	return Buffer(m_memory, info);
}

std::size_t Transfer::update() {
	auto removeDone = [this](Batch& batch) -> bool {
		if (m_memory->m_device->signalled(batch.done)) {
			if (batch.framePad == 0) {
				for (auto& [stage, promise] : batch.entries) {
					promise.set_value();
					scavenge(std::move(stage), batch.done);
				}
				return true;
			}
			--batch.framePad;
		}
		return false;
	};
	std::scoped_lock lock(m_sync.mutex);
	std::erase_if(m_batches.submitted, removeDone);
	if (!m_batches.active.entries.empty()) {
		std::vector<vk::CommandBuffer> commands;
		commands.reserve(m_batches.active.entries.size());
		m_batches.active.done = nextFence();
		for (auto& [stage, _] : m_batches.active.entries) { commands.push_back(stage.command); }
		vk::SubmitInfo const submitInfo(0U, nullptr, nullptr, (u32)commands.size(), commands.data());
		m_memory->m_device->queues().transfer().submit(submitInfo, m_batches.active.done);
		m_batches.submitted.push_back(std::move(m_batches.active));
	}
	m_batches.active = {};
	return m_batches.submitted.size();
}

Transfer::Stage Transfer::newStage(vk::DeviceSize bufferSize) { return Stage{nextBuffer(bufferSize), nextCommand()}; }

void Transfer::addStage(Stage&& stage, Promise&& promise) {
	stage.command.end();
	std::scoped_lock lock(m_sync.mutex);
	m_batches.active.entries.emplace_back(std::move(stage), std::move(promise));
}

std::optional<Buffer> Transfer::nextBuffer(vk::DeviceSize size) {
	if (size == 0) { return std::nullopt; }
	std::scoped_lock lock(m_sync.mutex);
	for (auto iter = m_data.buffers.begin(); iter != m_data.buffers.end(); ++iter) {
		if (iter->writeSize() >= size) {
			Buffer ret = std::move(*iter);
			m_data.buffers.erase(iter);
			return ret;
		}
	}
	return makeStagingBuffer(size);
}

vk::CommandBuffer beginCb(vk::CommandBuffer cb) {
	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	cb.begin(beginInfo);
	return cb;
}

vk::CommandBuffer Transfer::nextCommand() {
	std::scoped_lock lock(m_sync.mutex);
	if (!m_data.commands.empty()) {
		auto ret = m_data.commands.back();
		m_data.commands.pop_back();
		return beginCb(ret);
	}
	vk::CommandBufferAllocateInfo commandBufferInfo;
	commandBufferInfo.commandBufferCount = 1;
	commandBufferInfo.commandPool = m_data.pool;
	return beginCb(m_memory->m_device->device().allocateCommandBuffers(commandBufferInfo).front());
}

void Transfer::scavenge(Stage&& stage, vk::Fence fence) {
	m_data.commands.push_back(std::move(stage.command));
	if (stage.buffer) { m_data.buffers.push_back(std::move(*stage.buffer)); }
	if (std::find(m_data.fences.begin(), m_data.fences.end(), fence) == m_data.fences.end()) {
		m_memory->m_device->resetFence(fence, false);
		m_data.fences.push_back(fence);
	}
}

vk::Fence Transfer::nextFence() {
	if (!m_data.fences.empty()) {
		auto ret = m_data.fences.back();
		m_data.fences.pop_back();
		return ret;
	}
	return m_memory->m_device->makeFence(false);
}

void Transfer::stopPolling() {
	m_sync.poll.request_stop();
	m_sync.poll.join();
}

void Transfer::stopTransfer() {
	m_queue.clear(false);
	m_sync.staging.join();
}
} // namespace le::graphics
