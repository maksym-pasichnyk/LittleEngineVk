#include <levk/core/io.hpp>
#include <levk/core/io/zip_media.hpp>
#include <levk/core/log_channel.hpp>
#include <levk/core/utils/data_store.hpp>
#include <levk/core/utils/error.hpp>
#include <levk/engine/assets/asset_loaders_store.hpp>
#include <levk/engine/build_version.hpp>
#include <levk/engine/editor/editor.hpp>
#include <levk/engine/engine.hpp>
#include <levk/engine/gui/view.hpp>
#include <levk/engine/input/driver.hpp>
#include <levk/engine/input/receiver.hpp>
#include <levk/engine/render/layer.hpp>
#include <levk/engine/scene/scene_registry.hpp>
#include <levk/engine/utils/engine_config.hpp>
#include <levk/engine/utils/engine_stats.hpp>
#include <levk/engine/utils/error_handler.hpp>
#include <levk/graphics/utils/utils.hpp>
#include <levk/window/glue.hpp>
#include <levk/window/window.hpp>

namespace le {
namespace {
template <typename T>
void profilerNext(T& out_profiler, Time_s total) {
	if constexpr (!std::is_same_v<T, utils::NullProfileDB>) { out_profiler.next(total); }
}

std::optional<utils::EngineConfig> load(io::Path const& path) {
	if (path.empty()) { return std::nullopt; }
	if (auto json = dj::json(); json.load(path.generic_string())) { return io::fromJson<utils::EngineConfig>(json); }
	return std::nullopt;
}

bool save(utils::EngineConfig const& config, io::Path const& path) {
	if (path.empty()) { return false; }
	dj::json original;
	original.read(path.generic_string());
	auto overwrite = io::toJson(config);
	for (auto& [id, json] : overwrite.as<dj::map_t>()) {
		if (original.contains(id)) {
			original[id] = std::move(*json);
		} else {
			original.insert(id, std::move(*json));
		}
	}
	dj::serial_opts_t opts;
	opts.sort_keys = opts.pretty = true;
	return original.save(path.generic_string(), opts);
}

graphics::Bootstrap::MakeSurface makeSurface(window::Window const& winst) {
	return [&winst](vk::Instance vkinst) { return window::makeSurface(vkinst, winst); };
}

graphics::RenderContext::GetSpirV getShader(AssetStore const& store) {
	return [&store](Hash uri) {
		ENSURE(store.exists<graphics::SpirV>(uri), "Shader doesn't exist");
		return *store.find<graphics::SpirV>(uri);
	};
}
} // namespace

struct Engine::Impl {
	io::Service io;
	std::optional<window::Manager> wm;
	std::optional<Window> win;
	std::optional<GFX> gfx;
	AssetStore store;
	input::Driver input;
	input::ReceiverStore receivers;
	input::Frame inputFrame;
	graphics::ScreenView view;
	Profiler profiler;
	time::Point lastPoll{};
	Editor editor;
	utils::EngineStats::Counter stats;
	utils::ErrorHandler errorHandler;
	Service service;
	scene::Space space;
	io::Path configPath;

	Impl(std::optional<io::Path> logPath, LogChannel active) : io(logPath.value_or("levk-log.txt"), active), service(this) {}
};

Engine::GFX::GFX(not_null<Window const*> winst, Boot::CreateInfo const& bci, AssetStore const& store, std::optional<VSync> vsync)
	: boot(bci, makeSurface(*winst)), context(&boot.vram, getShader(store), vsync, winst->framebufferSize()) {}

Version Engine::version() noexcept { return g_engineVersion; }

Span<graphics::PhysicalDevice const> Engine::availableDevices() {
	auto const channels = dlog::channels();
	if (s_devices.empty()) {
		dlog::set_channels({LC_EndUser});
		s_devices = graphics::Device::physicalDevices();
	}
	dlog::set_channels(channels);
	return s_devices;
}

bool Engine::drawImgui(graphics::CommandBuffer cb) {
	if constexpr (levk_editor) {
		if (auto eng = Services::find<Service>()) {
			eng->m_impl->editor.render(cb);
			return true;
		}
	}
	return false;
}

Engine::Engine(std::unique_ptr<Impl>&& impl) noexcept : m_impl(std::move(impl)) {}

Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Engine::~Engine() noexcept {
	if (m_impl) {
		unboot();
		Services::untrack<Service>();
	}
}

void Engine::boot(Boot::CreateInfo info, std::optional<VSync> vsync) {
	unboot();
	info.device.instance.extensions = window::instanceExtensions(*m_impl->win);
	if (auto gpuOverride = DataObject<CustomDevice>("gpuOverride")) { info.device.customDeviceName = gpuOverride->name; }
	m_impl->gfx.emplace(&*m_impl->win, info, m_impl->store, vsync);
	auto const& surface = m_impl->gfx->context.surface();
	logI("[Engine] Swapchain image count: [{}] VSync: [{}]", surface.imageCount(), graphics::vSyncNames[surface.format().vsync]);
	logD("[Engine] Device supports lazily allocated memory: {}", m_impl->gfx->boot.device.physicalDevice().supportsLazyAllocation());
	Services::track<Context, VRAM, AssetStore, Profiler>(&m_impl->gfx->context, &m_impl->gfx->boot.vram, &m_impl->store, &m_impl->profiler);
	m_impl->editor.init(&m_impl->gfx->context, &*m_impl->win);
	addDefaultAssets();
	m_impl->win->show();
}

bool Engine::unboot() noexcept {
	if (booted()) {
		saveConfig();
		m_impl->store.clear();
		Services::untrack<Context, VRAM, AssetStore, Profiler>();
		m_impl->editor.deinit();
		m_impl->gfx->boot.vram.shutdown();
		m_impl->gfx.reset();
		io::ZIPMedia::fsDeinit();
		return true;
	}
	return false;
}

void Engine::poll(Opt<input::EventParser> custom) {
	f32 const rscale = m_impl->gfx ? m_impl->gfx->context.renderer().renderScale() : 1.0f;
	input::Driver::In in{m_impl->win->pollEvents(), {service().framebufferSize(), service().sceneSpace()}, rscale, &*m_impl->win, custom};
	m_impl->inputFrame = m_impl->input.update(in, service().editor().view());
	for (auto it = m_impl->receivers.rbegin(); it != m_impl->receivers.rend(); ++it) {
		if ((*it)->block(m_impl->inputFrame.state)) { break; }
	}
	if (m_impl->inputFrame.state.focus == input::Focus::eGained) { m_impl->store.update(); }
	profilerNext(m_impl->profiler, time::diffExchg(m_impl->lastPoll));
}

bool Engine::booted() const noexcept { return m_impl->gfx.has_value(); }

Engine::Service Engine::service() const noexcept { return m_impl->service; }

void Engine::saveConfig() const {
	utils::EngineConfig config;
	config.win.position = m_impl->win->position();
	config.win.size = m_impl->win->windowSize();
	config.win.maximized = m_impl->win->maximized();
	if (save(config, m_impl->configPath)) { logI("[Engine] Config saved to {}", m_impl->configPath.generic_string()); }
}

void Engine::addDefaultAssets() {
	static_assert(detail::reloadable_asset_v<graphics::Texture>, "ODR violation! include asset_loaders.hpp");
	static_assert(!detail::reloadable_asset_v<int>, "ODR violation! include asset_loaders.hpp");
	auto& boot = m_impl->gfx->boot;
	auto sampler = m_impl->store.add("samplers/default", graphics::Sampler(&boot.device, {vk::Filter::eLinear, vk::Filter::eLinear}));
	{
		auto si = graphics::Sampler::info({vk::Filter::eLinear, vk::Filter::eLinear});
		si.maxLod = 0.0f;
		m_impl->store.add("samplers/no_mip_maps", graphics::Sampler(&boot.device, si));
	}
	{
		auto si = graphics::Sampler::info({vk::Filter::eLinear, vk::Filter::eLinear});
		si.mipmapMode = vk::SamplerMipmapMode::eLinear;
		si.addressModeU = si.addressModeV = si.addressModeW = vk::SamplerAddressMode::eClampToBorder;
		si.borderColor = vk::BorderColor::eIntOpaqueBlack;
		m_impl->store.add("samplers/font", graphics::Sampler(&boot.device, si));
	}
	/*Textures*/ {
		using Tex = graphics::Texture;
		auto v = &boot.vram;
		vk::Sampler s = sampler->sampler();
		m_impl->store.add("textures/red", Tex(v, s, colours::red, {1, 1}));
		m_impl->store.add("textures/black", Tex(v, s, colours::black, {1, 1}));
		m_impl->store.add("textures/white", Tex(v, s, colours::white, {1, 1}));
		m_impl->store.add("textures/magenta", Tex(v, s, colours::magenta, {1, 1}));
		m_impl->store.add("textures/blank", Tex(v, s, 0x0, {1, 1}));
		Tex blankCube(v, s);
		blankCube.construct(Tex::unitCubemap(0x0));
		m_impl->store.add("cubemaps/blank", std::move(blankCube));
	}
	/* meshes */ {
		auto cube = m_impl->store.add<graphics::MeshPrimitive>("meshes/cube", graphics::MeshPrimitive(&boot.vram));
		cube->construct(graphics::makeCube());
		auto cone = m_impl->store.add<graphics::MeshPrimitive>("meshes/cone", graphics::MeshPrimitive(&boot.vram));
		cone->construct(graphics::makeCone());
		auto wf_cube = m_impl->store.add<graphics::MeshPrimitive>("wireframes/cube", graphics::MeshPrimitive(&boot.vram));
		wf_cube->construct(graphics::makeCube(1.0f, {}, graphics::Topology::eLineList));
	}
	/* materials */ { m_impl->store.add("materials/default", Material{}); }
	/* render layers */ {
		RenderLayer layer;
		m_impl->store.add("render_layers/default", layer);
		layer.flags = RenderFlag::eAlphaBlend;
		layer.order = 100;
		m_impl->store.add("render_layers/ui", layer);
		layer.flags = {};
		layer.order = -100;
		m_impl->store.add("render_layers/skybox", layer);
	}
}

std::optional<Engine> Engine::Builder::operator()() const {
	auto impl = std::make_unique<Impl>(std::move(m_logFile), m_logChannels);
	auto wm = window::Manager::make();
	if (!wm) {
		logE("[Engine] Window manager not ready");
		return std::nullopt;
	}
	impl->wm = std::move(wm);
	if (m_media) { impl->store.resources().media(m_media); }
	logI("LittleEngineVk v{} | {}", version().toString(false), time::format(time::sysTime(), "{:%a %F %T %Z}"));
	logI("Platform: {} {} ({})", levk_arch_name, levk_OS_name, os::cpuID());
	auto winInfo = m_windowInfo;
	winInfo.options.autoShow = false;
	if (auto config = load(m_configPath)) {
		logI("[Engine] Config loaded from {}", m_configPath.generic_string());
		if (config->win.size.x > 0 && config->win.size.y > 0) { winInfo.config.size = config->win.size; }
		winInfo.config.position = config->win.position;
	}
	impl->win = impl->wm->makeWindow(winInfo);
	if (!impl->win) {
		logE("[Engine] Failed to create window");
		return std::nullopt;
	}
	impl->errorHandler.deleteFile();
	impl->configPath = std::move(m_configPath);
	if (!impl->errorHandler.activeHandler()) { impl->errorHandler.setActive(); }
	Services::track(&impl->service);
	return Engine(std::move(impl));
}

void Engine::Service::setRenderer(std::unique_ptr<Renderer>&& renderer) const {
	m_impl->editor.deinit();
	m_impl->gfx->context.setRenderer(std::move(renderer));
	m_impl->editor.init(&m_impl->gfx->context, &*m_impl->win);
}

void Engine::Service::nextFrame() const {
	auto pr_ = profile("nextFrame");
	m_impl->gfx->context.waitForFrame();
	updateStats();
}

std::optional<graphics::RenderPass> Engine::Service::beginRenderPass(Opt<SceneRegistry> scene, RGBA clear, ClearDepth depth) const {
	graphics::RenderBegin rb;
	rb.clear = clear;
	rb.depth = depth;
	if constexpr (levk_editor) {
		[[maybe_unused]] bool const imgui_begun = m_impl->editor.beginFrame();
		EXPECT(imgui_begun);
		rb.view = m_impl->view = m_impl->editor.update(scene ? scene->ediScene() : edi::SceneRef());
	}
	return m_impl->gfx->context.beginMainPass(rb, m_impl->win->framebufferSize());
}

bool Engine::Service::endRenderPass(RenderPass& out_rp) const { return m_impl->gfx->context.endMainPass(out_rp, m_impl->win->framebufferSize()); }

void Engine::Service::pushReceiver(not_null<input::Receiver*> context) const { context->attach(m_impl->receivers); }
void Engine::Service::updateViewStack(gui::ViewStack& out_stack) const { out_stack.update(m_impl->inputFrame); }

window::Manager& Engine::Service::windowManager() const noexcept { return *m_impl->wm; }
Editor& Engine::Service::editor() const noexcept { return m_impl->editor; }
Engine::GFX& Engine::Service::gfx() const {
	ENSURE(m_impl->gfx.has_value(), "Not booted");
	return *m_impl->gfx;
}
Engine::Renderer& Engine::Service::renderer() const { return gfx().context.renderer(); }
Engine::Window& Engine::Service::window() const {
	ENSURE(m_impl->win.has_value(), "Not booted");
	return *m_impl->win;
}
input::Frame const& Engine::Service::inputFrame() const noexcept { return m_impl->inputFrame; }
AssetStore& Engine::Service::store() const noexcept { return m_impl->store; }
input::Receiver::Store& Engine::Service::receiverStore() noexcept { return m_impl->receivers; }

bool Engine::Service::closing() const { return window().closing(); }
glm::vec2 Engine::Service::sceneSpace() const noexcept { return m_impl->space(m_impl->inputFrame.space); }
Extent2D Engine::Service::framebufferSize() const noexcept {
	if (m_impl->gfx) { return m_impl->gfx->context.surface().extent(); }
	return m_impl->win->framebufferSize();
}
Extent2D Engine::Service::windowSize() const noexcept { return m_impl->win->windowSize(); }
Engine::Stats const& Engine::Service::stats() const noexcept { return m_impl->stats.stats; }

void Engine::Service::updateStats() const {
	m_impl->stats.update();
	m_impl->stats.stats.gfx.bytes.buffers = m_impl->gfx->boot.vram.bytes(graphics::Memory::Type::eBuffer);
	m_impl->stats.stats.gfx.bytes.images = m_impl->gfx->boot.vram.bytes(graphics::Memory::Type::eImage);
	m_impl->stats.stats.gfx.drawCalls = graphics::CommandBuffer::s_drawCalls.load();
	m_impl->stats.stats.gfx.triCount = graphics::MeshPrimitive::s_trisDrawn.load();
	m_impl->stats.stats.gfx.extents.window = m_impl->win->windowSize();
	if (m_impl->gfx) {
		m_impl->stats.stats.gfx.extents.swapchain = m_impl->gfx->context.surface().extent();
		m_impl->stats.stats.gfx.extents.renderer =
			Renderer::scaleExtent(m_impl->stats.stats.gfx.extents.swapchain, m_impl->gfx->context.renderer().renderScale());
	}
	graphics::CommandBuffer::s_drawCalls.store(0);
	graphics::MeshPrimitive::s_trisDrawn.store(0);
}
} // namespace le
