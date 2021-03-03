#include <iostream>
#include <core/transform.hpp>
#include <demo.hpp>
#include <dumb_json/djson.hpp>
#include <graphics/bitmap_text.hpp>
#include <graphics/common.hpp>
#include <graphics/context/bootstrap.hpp>
#include <graphics/geometry.hpp>
#include <graphics/mesh.hpp>
#include <graphics/render_context.hpp>
#include <graphics/shader.hpp>
#include <graphics/shader_buffer.hpp>
#include <graphics/texture.hpp>
#include <graphics/utils/utils.hpp>
#include <window/bootstrap.hpp>

#include <core/utils/algo.hpp>
#include <core/utils/std_hash.hpp>
#include <core/utils/string.hpp>
#include <dtasks/error_handler.hpp>
#include <dtasks/task_scheduler.hpp>
#include <engine/assets/asset_store.hpp>
#include <engine/camera.hpp>
#include <engine/input/input.hpp>
#include <engine/render/drawer.hpp>

namespace le::demo {
enum class Flag { eRecreated, eResized, ePaused, eClosed, eInit, eTerm, eDebug0, eCOUNT_ };
using Flags = kt::enum_flags<Flag>;

static void poll(Flags& out_flags, window::EventQueue queue) {
	while (auto e = queue.pop()) {
		switch (e->type) {
		case window::Event::Type::eClose: {
			out_flags.set(Flag::eClosed);
			break;
		}
		case window::Event::Type::eSuspend: {
			out_flags[Flag::ePaused] = e->payload.bSet;
			break;
		}
		case window::Event::Type::eResize: {
			auto const& resize = e->payload.resize;
			if (resize.bFramebuffer) {
				out_flags.set(Flag::eResized);
			}
			break;
		}
		case window::Event::Type::eInit:
			out_flags.set(Flag::eInit);
			break;
		case window::Event::Type::eTerm:
			out_flags.set(Flag::eTerm);
			break;
		default:
			break;
		}
	}
}

struct GPULister : os::ICmdArg {
	inline static constexpr std::array names = {"gpu-list"sv, "list-gpus"sv};

	View<std::string_view> keyVariants() const override {
		return names;
	}

	bool halt(std::string_view) override {
		graphics::g_log.minVerbosity = LibLogger::Verbosity::eEndUser;
		graphics::Instance inst(graphics::Instance::CreateInfo{});
		std::stringstream str;
		str << "Available GPUs:\n";
		int i = 0;
		for (auto const& d : inst.availableDevices(graphics::Device::requiredExtensions)) {
			str << '\t' << i++ << ". " << d << "\n";
		}
		str << "\n";
		std::cout << str.str();
		return true;
	}

	Usage usage() const override {
		return {"", "List supported GPUs"};
	}
};

struct GPUPicker : os::ICmdArg {
	inline static constexpr std::array names = {"use-gpu"sv, "pick-gpu"sv};

	inline static std::optional<std::size_t> s_picked;

	View<std::string_view> keyVariants() const override {
		return names;
	}

	bool halt(std::string_view params) override {
		s32 idx = utils::to<s32>(params, -1);
		if (idx >= 0) {
			s_picked = (std::size_t)idx;
			logD("Using custom GPU index: {}", idx);
		}
		return false;
	}

	Usage usage() const override {
		return {"<0-...>", "Select a custom available GPU"};
	}
};

void listCmdArgs();

struct HelpCmd : os::ICmdArg {
	inline static constexpr std::array names = {"h"sv, "help"sv};

	View<std::string_view> keyVariants() const override {
		return names;
	}

	bool halt(std::string_view) override {
		listCmdArgs();
		return true;
	}

	Usage usage() const override {
		return {"", "List all command line arguments"};
	}
};

struct Font {
	io::Path atlasID;
	io::Path samplerID;
	io::Path materialID;
	std::optional<graphics::Texture> atlas;
	std::array<graphics::Glyph, maths::max<u8>()> glyphs;
	s32 orgSizePt = 0;

	graphics::Glyph deserialise(u8 c, dj::node_t const& json) {
		graphics::Glyph ret;
		ret.ch = c;
		ret.st = {json.get("x").as<s32>(), json.get("y").as<s32>()};
		ret.uv = ret.cell = {json.get("width").as<s32>(), json.get("height").as<s32>()};
		ret.offset = {json.get("originX").as<s32>(), json.get("originY").as<s32>()};
		auto const pAdvance = json.find("advance");
		ret.xAdv = pAdvance ? pAdvance->as<s32>() : ret.cell.x;
		if (auto pBlank = json.find("isBlank")) {
			ret.bBlank = pBlank->as<bool>();
		}
		return ret;
	}

	void deserialise(dj::node_t const& json) {
		if (auto pAtlas = json.find("sheetID")) {
			atlasID = pAtlas->as<std::string>();
		}
		if (auto pSampler = json.find("samplerID")) {
			samplerID = pSampler->as<std::string>();
		}
		if (auto pMaterial = json.find("materialID")) {
			materialID = pMaterial->as<std::string>();
		}
		if (auto pSize = json.find("size")) {
			orgSizePt = pSize->as<s32>();
		}
		if (auto pGlyphsData = json.find("glyphs")) {
			for (auto& [key, value] : pGlyphsData->as<dj::map_nodes_t>()) {
				if (!key.empty()) {
					graphics::Glyph const glyph = deserialise((u8)key[0], *value);
					if (glyph.cell.x > 0 && glyph.cell.y > 0) {
						glyphs[(std::size_t)glyph.ch] = glyph;
					} else {
						logW("Could not deserialise Glyph '{}'!", key[0]);
					}
				}
			}
		}
	}

	bool create(graphics::VRAM& vram, io::Reader const& reader, io::Path const& id, io::Path const& path, vk::Sampler sampler, vk::Format format) {
		auto jsonText = reader.string(path);
		if (!jsonText) {
			return false;
		}
		auto json = dj::node_t::make(*jsonText);
		if (!json) {
			return false;
		}
		deserialise(*json);
		auto bytes = reader.bytes(path.parent_path() / atlasID);
		if (!bytes) {
			return false;
		}
		atlas = graphics::Texture((id / "atlas").generic_string(), vram);
		graphics::Texture::CreateInfo info;
		info.sampler = sampler;
		info.data = graphics::Texture::Img{std::move(*bytes)};
		info.format = format;
		if (!atlas->construct(info)) {
			return false;
		}
		return true;
	}
};

struct Text {
	using Type = graphics::Mesh::Type;

	graphics::BitmapText text;
	std::optional<graphics::Mesh> mesh;
	static constexpr glm::mat4 const model = glm::mat4(1.0f);

	void create(graphics::VRAM& vram, io::Path const& id, Type type = Type::eDynamic) {
		mesh = graphics::Mesh((id / "mesh").generic_string(), vram, type);
	}

	bool set(Font const& font, std::string_view str) {
		text.text = str;
		if (mesh) {
			return mesh->construct(text.generate(font.glyphs, font.atlas->data().size));
		}
		return false;
	}
};

GPULister g_gpuLister;
GPUPicker g_gpuPicker;
HelpCmd g_help;
std::array<Ref<os::ICmdArg>, 3> const g_cmdArgs = {g_gpuLister, g_gpuPicker, g_help};

void listCmdArgs() {
	std::stringstream str;
	for (os::ICmdArg const& arg : g_cmdArgs) {
		str << '[';
		bool bFirst = true;
		for (auto key : arg.keyVariants()) {
			if (!bFirst) {
				str << ", ";
			}
			bFirst = false;
			str << (key.length() == 1 ? "-"sv : "--"sv) << key;
		}
		auto const u = arg.usage();
		if (!u.params.empty()) {
			str << '=' << u.params;
		}
		str << "] : " << u.summary << '\n';
	}
	std::cout << str.str();
}

class Engine {
  public:
	Engine(window::IInstance& inst) : m_win(inst), m_pDesktop(dynamic_cast<window::DesktopInstance*>(&inst)) {
	}

	Input::Out poll(bool consume) noexcept {
		auto ret = m_input.update(m_win.get().pollEvents(), consume, m_pDesktop);
		// TODO: pass state to editor etc
		for (Input::IContext& context : m_contexts) {
			if (context.block(ret.state)) {
				break;
			}
		}
		return ret;
	}

	void pushContext(Input::IContext& context) {
		context.m_inputToken = m_contexts.push<true>(context);
	}

	bool boot(graphics::Bootstrap::CreateInfo const& boot) {
		if (!m_boot && !m_gfx) {
			auto surface = [this](vk::Instance inst) { return makeSurface(inst); };
			m_boot.emplace(boot, surface, m_win.get().framebufferSize());
			m_gfx.emplace(m_boot->swapchain);
			return true;
		}
		return false;
	}

	bool unboot() noexcept {
		if (m_boot && m_gfx) {
			m_gfx.reset();
			m_boot.reset();
			return true;
		}
		return false;
	}

	bool booted() const noexcept {
		return m_boot.has_value() && m_gfx.has_value();
	}

	graphics::Bootstrap& boot() {
		ENSURE(m_boot.has_value(), "Not booted");
		return *m_boot;
	}

	graphics::RenderContext& context() {
		ENSURE(m_gfx.has_value(), "Not booted");
		return *m_gfx;
	}

	Ref<window::IInstance> m_win;

  private:
	vk::SurfaceKHR makeSurface(vk::Instance vkinst) {
		vk::SurfaceKHR ret;
		m_win.get().vkCreateSurface(vkinst, ret);
		return ret;
	}

	std::optional<graphics::Bootstrap> m_boot;
	std::optional<graphics::RenderContext> m_gfx;
	Input m_input;
	TTokenGen<Ref<Input::IContext>, TGSpec_deque> m_contexts;
	window::DesktopInstance* m_pDesktop = {};
};

using namespace dts;

struct TaskErr : error_handler_t {
	void operator()(std::runtime_error const& err, u64) const override {
		ENSURE(false, err.what());
	}
};
TaskErr g_taskErr;

using namespace std::chrono;

struct ViewMats {
	alignas(16) glm::mat4 mat_v;
	alignas(16) glm::mat4 mat_p;
	alignas(16) glm::mat4 mat_ui;
	alignas(16) glm::vec3 pos_v;
};

struct Albedo {
	alignas(16) glm::vec3 ambient;
	alignas(16) glm::vec3 diffuse;
	alignas(16) glm::vec3 specular;
};

struct DirLight {
	alignas(16) Albedo albedo;
	alignas(16) glm::vec3 direction;
};

struct Drawable {
	Transform tr;
	Albedo al{};
	std::optional<graphics::ShaderBuffer> mat_m;
	std::optional<graphics::ShaderBuffer> material;
	graphics::Mesh const* pMesh = {};
	graphics::Texture const* pDiffuse = {};
};

class DrawScene {
  public:
	struct {
		DrawLayer sky;
		DrawLayer test;
		DrawLayer test_tex;
		DrawLayer ui;
	} m_layers;
	struct {
		std::optional<graphics::ShaderBuffer> mats;
		std::optional<graphics::ShaderBuffer> lights;
	} m_view;

	void write(Camera const& cam, glm::vec2 fb, kt::fixed_vector<DirLight, 4> const& lights) {
		ViewMats const v{cam.view(), cam.perspective(fb), cam.ortho(fb), cam.position};
		m_view.mats->write<false>(v);
		if (!lights.empty()) {
			m_view.lights->write(lights.front());
		}
	}

	void update(DrawList<Drawable> const& list) const {
		auto& si = list.layer.pipeline->shaderInput();
		si.update(*m_view.mats, 0, 0, 0);
		if (list.layer.order >= 0 && si.contains(0, 1)) {
			si.update(*m_view.lights, 0, 1, 0);
		}
		bool const sb10 = si.contains(1, 0);
		bool const sb11 = si.contains(1, 1);
		bool const sb20 = si.contains(2, 0);
		for (std::size_t idx = 0; idx < list.ts.size(); ++idx) {
			Drawable& d = list.ts[idx];
			if (sb10) {
				d.mat_m->write(d.tr.model());
				si.update(*d.mat_m, 1, 0, idx);
				d.mat_m->swap();
			}
			if (sb11) {
				d.material->write(d.al);
				si.update(*d.material, 1, 1, idx);
				d.material->swap();
			}
			if (d.pDiffuse) {
				if (list.layer.order < 0) {
					si.update(*d.pDiffuse, 0, 1, idx);
				} else {
					if (sb20) {
						si.update(*d.pDiffuse, 2, 0, idx);
					}
				}
			}
		}
	}

	void swap() {
		m_view.mats->swap();
		m_view.lights->swap();
	}

	void draw(graphics::CommandBuffer const& cb, DrawList<Drawable> const& list) const {
		graphics::Pipeline const& pi = *list.layer.pipeline;
		auto& input = pi.shaderInput();
		bool const s1 = input.contains(1);
		bool const s2 = input.contains(2);
		cb.bindSet(pi.layout(), input.set(0).front());
		for (std::size_t idx = 0; idx < list.ts.size(); ++idx) {
			Drawable& d = list.ts[idx];
			if (s1) {
				cb.bindSet(pi.layout(), input.set(1).index(idx));
			}
			if (s2) {
				cb.bindSet(pi.layout(), input.set(2).index(idx));
			}
			d.pMesh->draw(cb);
		}
	}
};

class App : public Input::IContext {
  public:
	App(Engine& eng, io::Reader const& reader) : m_eng(eng) {
		dts::g_error_handler = &g_taskErr;
		auto loadShader = [this](std::string_view id, io::Path v, io::Path f) {
			AssetLoadData<graphics::Shader> shaderLD{m_eng.get().boot().device, {}};
			shaderLD.shaderPaths[graphics::Shader::Type::eVertex] = std::move(v);
			shaderLD.shaderPaths[graphics::Shader::Type::eFragment] = std::move(f);
			m_store.load<graphics::Shader>(id, std::move(shaderLD));
		};
		using PCI = graphics::Pipeline::CreateInfo;
		auto loadPipe = [this](std::string_view id, Hash shaderID, graphics::PFlags flags = {}, std::optional<PCI> pci = std::nullopt) {
			AssetLoadData<graphics::Pipeline> pipelineLD{m_eng.get().context(), {}, {}, {}, {}};
			pipelineLD.name = id;
			pipelineLD.shaderID = shaderID;
			pipelineLD.info = pci;
			pipelineLD.flags = flags;
			m_store.load<graphics::Pipeline>(id, std::move(pipelineLD));
		};
		m_store.resources().reader(reader);
		m_store.add("samplers/default", graphics::Sampler{eng.boot().device, graphics::Sampler::info({vk::Filter::eLinear, vk::Filter::eLinear})});
		auto sampler = m_store.get<graphics::Sampler>("samplers/default");
		m_data.font.create(eng.boot().vram, reader, "fonts/default", "fonts/default.json", sampler->sampler(), eng.context().textureFormat());
		m_data.text.create(eng.boot().vram, "text");
		m_data.text.text.size = 80U;
		m_data.text.text.colour = colours::yellow;
		m_data.text.text.pos = {0.0f, 200.0f, 0.0f};
		m_data.text.set(m_data.font, "Hi!");
		{
			graphics::Geometry gcube = graphics::makeCube(0.5f);
			auto const skyCubeI = gcube.indices;
			auto const skyCubeV = gcube.positions();
			auto cube = m_store.add<graphics::Mesh>("meshes/cube", graphics::Mesh("meshes/cube", eng.boot().vram));
			cube->construct(gcube);
			m_data.mesh.push_back(cube.m_id);
			auto cone = m_store.add<graphics::Mesh>("meshes/cone", graphics::Mesh("meshes/cone", eng.boot().vram));
			cone->construct(graphics::makeCone());
			m_data.mesh.push_back(cone.m_id);
			auto skycube = m_store.add<graphics::Mesh>("skycube", graphics::Mesh("skycube", eng.boot().vram));
			skycube->construct(View<glm::vec3>(skyCubeV), skyCubeI);
			m_data.mesh.push_back(skycube.m_id);
		}

		task_scheduler::stage_id load_shaders;
		PCI pci_skybox = eng.context().pipeInfo();
		pci_skybox.fixedState.depthStencilState.depthWriteEnable = false;
		pci_skybox.fixedState.vertexInput = eng.context().vertexInput({0, sizeof(glm::vec3), {{vk::Format::eR32G32B32Sfloat, 0}}});
		{
			task_scheduler::stage_t shaders;
			shaders.tasks.push_back([loadShader]() { loadShader("shaders/test", "shaders/test.vert", "shaders/test.frag"); });
			shaders.tasks.push_back([loadShader]() { loadShader("shaders/test_tex", "shaders/test.vert", "shaders/test_tex.frag"); });
			shaders.tasks.push_back([loadShader]() { loadShader("shaders/ui", "shaders/ui.vert", "shaders/ui.frag"); });
			shaders.tasks.push_back([loadShader]() { loadShader("shaders/skybox", "shaders/skybox.vert", "shaders/skybox.frag"); });
			load_shaders = m_tasks.stage(std::move(shaders));
		}
		{
			task_scheduler::stage_t pipes;
			pipes.tasks.push_back([loadPipe]() { loadPipe("pipelines/test", "shaders/test", graphics::PFlags::inverse()); });
			pipes.tasks.push_back([loadPipe]() { loadPipe("pipelines/test_tex", "shaders/test_tex", graphics::PFlags::inverse()); });
			pipes.tasks.push_back([loadPipe]() { loadPipe("pipelines/ui", "shaders/ui", graphics::PFlags::inverse()); });
			pipes.tasks.push_back([loadPipe, pci_skybox]() { loadPipe("pipelines/skybox", "shaders/skybox", {}, pci_skybox); });
			pipes.deps.push_back(load_shaders);
			m_data.load_pipes = m_tasks.stage(std::move(pipes));
		}

		task_scheduler::stage_t texload;
		AssetLoadData<graphics::Texture> textureLD{eng.boot().vram, {}, {}, {}, {}, {}, "samplers/default"};
		textureLD.name = "cubemaps/sky_dusk";
		textureLD.prefix = "skyboxes/sky_dusk";
		textureLD.ext = ".jpg";
		textureLD.imageIDs = {"right", "left", "up", "down", "front", "back"};
		texload.tasks.push_back([this, textureLD]() {
			m_store.load<graphics::Texture>(textureLD.name, textureLD);
			m_data.tex.push_back(textureLD.name);
		});
		textureLD.name = "textures/container2";
		textureLD.prefix.clear();
		textureLD.ext.clear();
		textureLD.imageIDs = {"textures/container2.png"};
		texload.tasks.push_back([this, textureLD]() {
			m_store.load<graphics::Texture>(textureLD.name, textureLD);
			m_data.tex.push_back(textureLD.name);
		});
		textureLD.name = "textures/red";
		textureLD.imageIDs.clear();
		textureLD.raw.bytes = graphics::utils::convert({0xff, 0, 0, 0xff});
		textureLD.raw.size = {1, 1};
		texload.tasks.push_back([this, textureLD]() {
			m_store.load<graphics::Texture>(textureLD.name, textureLD);
			m_data.tex.push_back(textureLD.name);
		});
		m_data.load_tex = m_tasks.stage(std::move(texload));

		// m_input = m_eng.get().push(this);
		eng.m_win.get().show();
	}

	bool block(Input::State const&) override {
		return false;
	}

	void init1() {
		auto pipe_test = m_store.find<graphics::Pipeline>("pipelines/test");
		auto pipe_testTex = m_store.find<graphics::Pipeline>("pipelines/test_tex");
		auto pipe_ui = m_store.find<graphics::Pipeline>("pipelines/ui");
		auto pipe_sky = m_store.find<graphics::Pipeline>("pipelines/skybox");
		m_data.cam.position = {0.0f, 2.0f, 4.0f};
		auto skycube = m_store.get<graphics::Mesh>("skycube");
		auto cube = m_store.get<graphics::Mesh>("meshes/cube");
		auto cone = m_store.get<graphics::Mesh>("meshes/cone");
		auto skymap = m_store.get<graphics::Texture>("cubemaps/sky_dusk");

		m_scene.m_layers.sky = DrawLayer{&pipe_sky->get(), -10};
		m_scene.m_layers.test = DrawLayer{&pipe_test->get(), 0};
		m_scene.m_layers.test_tex = DrawLayer{&pipe_testTex->get(), 0};
		m_scene.m_layers.ui = DrawLayer{&pipe_ui->get(), 10};
		m_scene.m_view.mats = graphics::ShaderBuffer(m_eng.get().boot().vram, "view_mats", {});
		m_scene.m_view.lights = graphics::ShaderBuffer(m_eng.get().boot().vram, "lights", {});
		auto spawn = [this](std::string name, graphics::Mesh const& mesh, DrawLayer const& layer, bool mat = false) {
			auto [e, c] = m_registry.spawn<Drawable>(name);
			auto& [d] = c;
			d.pMesh = &mesh;
			d.mat_m = graphics::ShaderBuffer(m_eng.get().boot().vram, name + "/mat_m", {});
			if (mat) {
				d.material = graphics::ShaderBuffer(m_eng.get().boot().vram, name + "/material", {});
			}
			m_registry.attach<DrawLayer>(e, layer);
			return std::pair<decf::entity_t, Drawable&>(e, d);
		};
		{
			auto [_, d] = spawn("skybox", *skycube, m_scene.m_layers.sky);
			d.pDiffuse = &skymap.get();
		}
		{
			auto [e, d] = spawn("prop_0", *cube, m_scene.m_layers.test_tex, true);
			d.pDiffuse = &m_store.get<graphics::Texture>("textures/container2").get();
			d.al.diffuse = colours::cyan.toVec3();
			m_data.entities["cube_tex"] = e;
		}
		{
			auto [e, d] = spawn("prop_1", *cube, m_scene.m_layers.test);
			d.tr.position({-5.0f, -1.0f, -2.0f});
			m_data.entities["prop_1"] = e;
		}
		{
			auto [_, d] = spawn("prop_2", *cone, m_scene.m_layers.test);
			d.tr.position({1.0f, -2.0f, -3.0f});
		}
		{
			auto [e, d] = spawn("ui_1", *m_data.text.mesh, m_scene.m_layers.ui);
			d.pDiffuse = &*m_data.font.atlas;
		}
	}

	bool ready(std::initializer_list<Ref<dts::task_scheduler::stage_id>> sts) const {
		for (dts::task_scheduler::stage_id& st : sts) {
			if (st.id > 0) {
				if (m_tasks.stage_done(st)) {
					st = {};
				} else {
					return false;
				}
			}
		}
		return true;
	}

	void tick(Time_s dt) {
		if (!ready({m_data.load_pipes, m_data.load_tex})) {
			return;
		}
		if (m_registry.empty()) {
			init1();
		}
		// camera
		{
			glm::vec3 const moveDir = glm::normalize(glm::cross(m_data.cam.position, graphics::up));
			m_data.cam.position += moveDir * dt.count() * 0.75f;
			m_data.cam.look(-m_data.cam.position);
		}
		m_registry.get<Drawable>(m_data.entities["cube_tex"]).tr.rotate(glm::radians(-180.0f) * dt.count(), glm::normalize(glm::vec3(1.0f)));
		m_registry.get<Drawable>(m_data.entities["prop_1"]).tr.rotate(glm::radians(360.0f) * dt.count(), graphics::up);
	}

	void render() {
		if (m_eng.get().context().waitForFrame()) {
			// write / update
			if (m_data.load_tex.id == 0) {
				m_scene.write(m_data.cam, m_eng.get().context().extent(), {});
			}

			// draw
			if (auto r = m_eng.get().context().render(Colour(0x040404ff))) {
				if (m_data.load_tex.id == 0) {
					auto& cb = r->primary();
					cb.setViewportScissor(m_eng.get().context().viewport(), m_eng.get().context().scissor());
					batchDraw<Drawable>(m_scene, m_registry, cb);
				}
			}
		}
	}

  private:
	struct Data {
		std::vector<Hash> tex;
		std::vector<Hash> mesh;
		std::unordered_map<Hash, decf::entity_t> entities;

		Font font;
		Text text;
		Camera cam;

		task_scheduler::stage_id load_pipes, load_tex;
	};

	Data m_data;
	DrawScene m_scene;
	decf::registry_t m_registry;
	task_scheduler m_tasks;
	std::future<void> m_ready;

  public:
	AssetStore m_store;
	Ref<Engine> m_eng;
};

struct FlagsInput : Input::IContext {
	Flags& flags;

	FlagsInput(Flags& flags) : flags(flags) {
	}

	bool block(Input::State const& state) override {
		bool ret = false;
		if (state.any({window::Key::eLeftControl, window::Key::eRightControl})) {
			if (state.pressed(window::Key::eW)) {
				flags.set(Flag::eClosed);
				ret = true;
			}
			if (state.released(window::Key::eD)) {
				flags.set(Flag::eDebug0);
				ret = true;
			}
		}
		return ret;
	}
};

bool run(CreateInfo const& info, io::Reader const& reader) {
	os::args({info.args.argc, info.args.argv});
	if (os::halt(g_cmdArgs)) {
		return true;
	}
	try {
		window::Instance::CreateInfo winInfo;
		winInfo.config.androidApp = info.androidApp;
		winInfo.config.title = "levk demo";
		winInfo.config.size = {1280, 720};
		winInfo.options.bCentreCursor = true;
		winInfo.options.verbosity = LibLogger::Verbosity::eLibrary;
		window::Instance winst(winInfo);
		graphics::Bootstrap::CreateInfo bootInfo;
		bootInfo.instance.extensions = winst.vkInstanceExtensions();
		bootInfo.instance.bValidation = levk_debug;
		bootInfo.instance.validationLog = dl::level::info;
		bootInfo.logVerbosity = LibLogger::Verbosity::eLibrary;
		std::optional<App> app;
		bootInfo.device.pickOverride = GPUPicker::s_picked;
		Engine engine(winst);
		Flags flags;
		FlagsInput flagsInput(flags);
		engine.pushContext(flagsInput);
		time::Point t = time::now();
		while (true) {
			Time_s dt = time::now() - t;
			t = time::now();
			auto [_, queue] = engine.poll(true);
			poll(flags, std::move(queue));
			if (flags.test(Flag::eClosed)) {
				app.reset();
				engine.unboot();
				break;
			}
			if (flags.test(Flag::ePaused)) {
				continue;
			}
			if (flags.test(Flag::eInit)) {
				app.reset();
				engine.boot(bootInfo);
				app.emplace(engine, reader);
			}
			if (flags.test(Flag::eTerm)) {
				app.reset();
				engine.unboot();
			}
			if (flags.test(Flag::eResized)) {
				/*if (!context.recreated(winst.framebufferSize())) {
					ENSURE(false, "Swapchain failure");
				}*/
				flags.reset(Flag::eResized);
			}
			if (engine.booted() && engine.context().reconstructed(winst.framebufferSize())) {
				continue;
			}

			if (flags.test(Flag::eDebug0)) {
				app->m_store.update();
				logD("Debug0");
				flags.reset(Flag::eDebug0);
			}

			if (app) {
				// threads::sleep(5ms);
				app->tick(dt);
				app->render();
			}
			flags.reset(Flag::eRecreated | Flag::eInit | Flag::eTerm);
		}
	} catch (std::exception const& e) {
		logE("exception: {}", e.what());
	}
	return true;
}
} // namespace le::demo
