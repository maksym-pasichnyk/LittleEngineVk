#include <levk/core/utils/expect.hpp>
#include <levk/graphics/render/context.hpp>
#include <levk/graphics/render/pipeline_factory.hpp>
#include <levk/graphics/utils/utils.hpp>

#include <levk/core/services.hpp>

namespace le::graphics {
ShaderInput::ShaderInput(not_null<VRAM*> vram, PoolData data) : m_vram(vram) {
	Buffering const buffering = data.buffering == Buffering::eNone ? Buffering::eDouble : data.buffering;
	for (std::size_t set = 0; set < data.sets.size(); ++set) {
		DescriptorPool::CreateInfo pci;
		auto const& sld = data.sets[set];
		pci.bindingData = sld.bindingData;
		pci.buffering = buffering;
		pci.layout = sld.layout;
		pci.setNumber = (u32)set;
		m_setPools.emplace(u32(set), DescriptorPool(m_vram, pci));
	}
}

bool ShaderInput::contains(u32 set) const noexcept {
	if (auto it = m_setPools.find(set); it != m_setPools.end()) { return !it->second.unassigned(); }
	return false;
}

DescriptorSet& ShaderInput::set(u32 set, std::size_t index) const {
	if (auto it = m_setPools.find(set); it != m_setPools.end()) { return it->second.set(index); }
	ENSURE(false, "Nonexistent set");
}

void ShaderInput::swap() {
	for (auto& [_, pool] : m_setPools) { pool.swap(); }
}

std::size_t PipelineFactory::Hasher::operator()(Spec const& spec) const {
	utils::HashGen ret;
	return ret << spec.vertexInput << spec.fixedState << spec.shader;
}

PipelineFactory::Spec PipelineFactory::spec(ShaderSpec shader, PFlags flags, VertexInputInfo vertexInput) {
	Spec ret;
	EXPECT(!shader.moduleURIs.empty());
	ret.shader = shader;
	ret.fixedState.flags = flags;
	ret.vertexInput = vertexInput.bindings.empty() ? VertexInfoFactory<Vertex>{}(0) : std::move(vertexInput);
	return ret;
}

vk::UniqueShaderModule PipelineFactory::makeModule(vk::Device device, SpirV const& spirV) {
	vk::ShaderModuleCreateInfo createInfo;
	createInfo.codeSize = spirV.spirV.size() * sizeof(u32);
	createInfo.pCode = spirV.spirV.data();
	return device.createShaderModuleUnique(createInfo);
}

PipelineFactory::PipelineFactory(not_null<VRAM*> vram, GetSpirV&& getSpirV, Buffering buffering) noexcept
	: m_getSpirV(std::move(getSpirV)), m_vram(vram), m_buffering(buffering) {
	EXPECT(m_getSpirV.has_value());
}

Pipeline PipelineFactory::get(Spec const& spec, vk::RenderPass renderPass) {
	EXPECT(!spec.shader.moduleURIs.empty());
	EXPECT(!Device::default_v(renderPass));
	auto const specHash = spec.hash();
	auto sit = m_storage.find(specHash);
	if (sit == m_storage.end()) {
		auto [i, b] = m_storage.emplace(specHash, SpecMap());
		sit = i;
		sit->second.spec = spec;
	}
	auto& specMap = sit->second;
	if (!specMap.meta.layout.active()) { specMap.meta = makeMeta(spec.shader); }
	auto pit = specMap.map.find(renderPass);
	if (pit == specMap.map.end() || pit->second.stale) {
		auto pipe = makePipe(specMap, renderPass);
		ENSURE(pipe, "Failed to create pipeline");
		auto [i, b] = specMap.map.insert_or_assign(renderPass, std::move(*pipe));
		pit = i;
	}
	return pit->second.pipe();
}

bool PipelineFactory::contains(Hash spec, vk::RenderPass renderPass) const {
	if (auto sit = m_storage.find(spec); sit != m_storage.end()) { return sit->second.map.contains(renderPass); }
	return false;
}

PipelineSpec const* PipelineFactory::find(Hash spec) const {
	if (auto sit = m_storage.find(spec); sit != m_storage.end()) { return &sit->second.spec; }
	return nullptr;
}

std::size_t PipelineFactory::markStale(Hash shaderURI) {
	std::size_t ret{};
	for (auto& [_, specMap] : m_storage) {
		for (Hash const uri : specMap.spec.shader.moduleURIs) {
			if (uri == shaderURI) {
				specMap.map.clear();						  // destroy pipelines
				specMap.meta = makeMeta(specMap.spec.shader); // recreate shader
				++ret;
				break;
			}
		}
	}
	return ret;
}

std::size_t PipelineFactory::pipeCount(Hash spec) const noexcept {
	if (auto sit = m_storage.find(spec); sit != m_storage.end()) { return sit->second.map.size(); }
	return {};
}

void PipelineFactory::clear(Hash spec) noexcept {
	if (auto sit = m_storage.find(spec); sit != m_storage.end()) { sit->second.map.clear(); }
}

std::optional<PipelineFactory::Pipe> PipelineFactory::makePipe(SpecMap const& spec, vk::RenderPass renderPass) const {
	Pipe ret;
	ret.layout = spec.meta.layout;
	utils::PipeData data;
	data.renderPass = renderPass;
	data.layout = spec.meta.layout;
	ktl::fixed_vector<vk::UniqueShaderModule, 4> modules;
	ktl::fixed_vector<utils::ShaderModule, 4> sm;
	for (auto const uri : spec.spec.shader.moduleURIs) {
		auto const spirV = m_getSpirV(uri);
		EXPECT(!spirV.spirV.empty());
		if (!spirV.spirV.empty()) {
			modules.push_back(makeModule(m_vram->m_device->device(), spirV));
			sm.push_back({*modules.back(), spirV.type});
		}
	}
	// TODO
	// in.cache = m_pipelineCache;
	if (auto p = utils::makeGraphicsPipeline(*m_vram->m_device, sm, spec.spec, data)) {
		ret.pipeline = ret.pipeline.make(*p, m_vram->m_device);
		ret.input = ShaderInput(m_vram, spec.meta.spd);
		return ret;
	}
	return std::nullopt;
}

PipelineFactory::Meta PipelineFactory::makeMeta(ShaderSpec const& shader) const {
	ktl::fixed_vector<SpirV, 4> spirV;
	for (auto const& mod : shader.moduleURIs) {
		auto spV = m_getSpirV(mod);
		EXPECT(!spV.spirV.empty());
		if (!spV.spirV.empty()) { spirV.push_back(std::move(spV)); }
	}
	Meta ret;
	ret.spd.buffering = m_buffering;
	auto setBindings = utils::extractBindings(spirV);
	std::vector<vk::DescriptorSetLayout> layouts;
	for (auto& [set, binds] : setBindings.sets) {
		std::vector<vk::DescriptorSetLayoutBinding> bindings;
		ktl::fixed_vector<DescriptorSet::BindingData, max_bindings_v> bindingData;
		for (auto& data : binds) {
			if (data.binding.descriptorType != vk::DescriptorType()) {
				bindings.push_back(data.binding);
				Texture::Type const texType = data.imageType == vk::ImageViewType::eCube ? Texture::Type::eCube : Texture::Type::e2D;
				bindingData.push_back({std::move(data.name), data.binding, texType});
			}
		}
		auto const descLayout = m_vram->m_device->makeDescriptorSetLayout(bindings);
		ret.setLayouts.push_back(Defer<vk::DescriptorSetLayout>::make(descLayout, m_vram->m_device));
		ret.bindings.push_back({bindings.begin(), bindings.end()});
		layouts.push_back(descLayout);

		ShaderInput::PoolData::Set sld;
		sld.bindingData = std::move(bindingData);
		sld.layout = descLayout;
		ret.spd.sets.push_back(std::move(sld));
	}
	ret.layout = ret.layout.make(m_vram->m_device->makePipelineLayout(setBindings.push, layouts), m_vram->m_device);
	return ret;
}

} // namespace le::graphics
