#pragma once
#include <levk/graphics/command_buffer.hpp>
#include <levk/graphics/render/descriptor_set.hpp>
#include <levk/graphics/render/draw_list.hpp>

namespace le::graphics {
struct DescriptorFallback {
	not_null<Texture const*> texture;
	not_null<Texture const*> cubemap;
};

class DescriptorHelper {
  public:
	class Fallback;
	class Updater;
	class Binder;
	class Map;

  protected:
	std::size_t m_nextIndex[max_bindings_v] = {};
};

using DescriptorUpdater = DescriptorHelper::Updater;
using DescriptorBinder = DescriptorHelper::Binder;
using DescriptorMap = DescriptorHelper::Map;

class DescriptorHelper::Fallback : public DescriptorHelper {
  public:
	Fallback(DescriptorFallback fallback) noexcept : m_fallback(fallback) {}

	DescriptorFallback fallback() const noexcept { return m_fallback; }
	void fallback(DescriptorFallback fallback) noexcept { m_fallback = fallback; }

  protected:
	DescriptorFallback m_fallback;
	std::size_t m_nextIndex[max_bindings_v] = {};
};

class DescriptorHelper::Updater : public DescriptorHelper::Fallback {
  public:
	explicit Updater(DescriptorFallback fallback, Opt<DescriptorSet> descriptorSet = {}) : Fallback(fallback), m_descriptorSet(descriptorSet) {}

	bool valid() const noexcept { return m_descriptorSet; }
	Opt<DescriptorSet> descriptorSet() const noexcept { return m_descriptorSet; }

	template <typename T>
	bool update(u32 binding, T const& t, vk::DescriptorType type = vk::DescriptorType::eUniformBuffer) const;
	bool update(u32 binding, Opt<Texture const> tex) const;
	bool update(u32 binding, ShaderBuffer const& buffer) const;

  private:
	bool check(u32 binding, vk::DescriptorType const* type = {}, Texture::Type const* texType = {}) const noexcept;
	Texture const& safeTex(Texture const* tex, u32 bind) const;

	mutable ktl::fixed_vector<u32, max_bindings_v> m_binds;
	Opt<DescriptorSet> m_descriptorSet{};
};

class DescriptorHelper::Binder : public DescriptorHelper {
  public:
	Binder(vk::PipelineLayout layout, not_null<ShaderInput*> input, CommandBuffer cb) noexcept : m_cb(cb), m_input(input), m_layout(layout) {}

	void bind(DrawBindings const& indices) const;

	CommandBuffer const& commandBuffer() const noexcept { return m_cb; }
	vk::PipelineLayout pipelineLayout() const noexcept { return m_layout; }
	ShaderInput const& shaderInput() const noexcept { return *m_input; }

  private:
	CommandBuffer m_cb;
	not_null<ShaderInput*> m_input;
	vk::PipelineLayout m_layout;
};

class DescriptorHelper::Map : public DescriptorHelper::Fallback {
  public:
	Map(DescriptorFallback fallback, not_null<ShaderInput*> input) noexcept : Fallback(fallback), m_input(input) {}

	bool contains(u32 setNumber);
	Updater nextSet(DrawBindings const& bindings, u32 setNumber);

	ShaderInput const& shaderInput() const noexcept { return *m_input; }

  private:
	not_null<ShaderInput*> m_input;
};

// impl

template <typename T>
bool DescriptorHelper::Updater::update(u32 bind, T const& t, vk::DescriptorType type) const {
	if (check(bind, &type)) {
		m_descriptorSet->writeUpdate(t, bind);
		return true;
	}
	return false;
}
} // namespace le::graphics
