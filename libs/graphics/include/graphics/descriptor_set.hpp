#pragma once
#include <unordered_map>
#include <core/ref.hpp>
#include <core/span.hpp>
#include <graphics/utils/ring_buffer.hpp>
#include <vulkan/vulkan.hpp>

namespace le::graphics {
class Device;
class Texture;
class Buffer;
class Image;

struct BindingInfo {
	vk::DescriptorSetLayoutBinding binding;
	std::string name;
	bool bUnassigned = false;
};

class DescriptorSet {
  public:
	// Combined Image Sampler
	struct CIS {
		vk::ImageView image;
		vk::Sampler sampler;
	};
	struct CreateInfo;

	DescriptorSet(Device& device, CreateInfo const& info);
	DescriptorSet(DescriptorSet&&) noexcept;
	DescriptorSet& operator=(DescriptorSet&&) noexcept;
	~DescriptorSet();

	void index(std::size_t index);
	void next();
	vk::DescriptorSet get() const;

	void updateBuffers(u32 binding, View<Ref<Buffer const>> buffers, std::size_t size, vk::DescriptorType type = vk::DescriptorType::eUniformBuffer);
	bool updateCIS(u32 binding, std::vector<CIS> cis);
	bool updateTextures(u32 binding, View<Texture> textures);

	u32 setNumber() const noexcept;

	Ref<Device> m_device;

  private:
	template <typename T>
	void update(u32 binding, vk::DescriptorType type, View<T> writes);
	void update(vk::WriteDescriptorSet set);
	void destroy();

	struct Binding {
		std::string name;
		vk::DescriptorType type;
		std::vector<Ref<Buffer const>> buffers;
		std::vector<CIS> cis;
		u32 count = 1;
	};
	struct Set {
		vk::DescriptorSet set;
		vk::DescriptorPool pool;
		std::unordered_map<u32, Binding> bindings;
	};
	struct Storage {
		vk::DescriptorSetLayout layout;
		RingBuffer<Set> setBuffer;
		std::unordered_map<u32, BindingInfo> bindingInfos;
		u32 rotateCount = 1;
		u32 setNumber = 0;
	} m_storage;

	std::pair<Set&, Binding&> setBind(u32 bind, vk::DescriptorType type, u32 count);
};

struct DescriptorSet::CreateInfo {
	vk::DescriptorSetLayout layout;
	View<BindingInfo> bindingInfos;
	std::size_t rotateCount = 1;
	u32 setNumber = 0;
};

class SetFactory {
  public:
	struct CreateInfo;

	SetFactory(Device& device, CreateInfo const& info);

	DescriptorSet& front();
	DescriptorSet& at(std::size_t idx);
	Span<DescriptorSet> populate(std::size_t count);
	void swap();

  private:
	struct Storage {
		vk::DescriptorSetLayout layout;
		std::vector<BindingInfo> bindInfos;
		std::vector<DescriptorSet> descriptorSets;
		std::size_t rotateCount = 0;
		u32 setNumber = 0;
	} m_storage;
	Ref<Device> m_device;
};

struct SetFactory::CreateInfo {
	vk::DescriptorSetLayout layout;
	std::vector<BindingInfo> bindInfos;
	std::size_t rotateCount = 2;
	u32 setNumber = 0;
};

// impl

inline u32 DescriptorSet::setNumber() const noexcept {
	return m_storage.setNumber;
}

template <typename T>
void DescriptorSet::update(u32 binding, vk::DescriptorType type, View<T> writes) {
	vk::WriteDescriptorSet write;
	write.dstSet = get();
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorType = type;
	write.descriptorCount = (u32)writes.size();
	if constexpr (std::is_same_v<T, vk::DescriptorImageInfo>) {
		write.pImageInfo = writes.data();
	} else if constexpr (std::is_same_v<T, vk::DescriptorBufferInfo>) {
		write.pBufferInfo = writes.data();
	} else {
		static_assert(false_v<T>, "Invalid type");
	}
	update(write);
}
} // namespace le::graphics
