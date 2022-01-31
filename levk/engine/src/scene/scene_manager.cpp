#include <dumb_tasks/executor.hpp>
#include <levk/engine/editor/editor.hpp>
#include <levk/engine/render/frame.hpp>
#include <levk/engine/render/shader_data.hpp>
#include <levk/engine/scene/scene_manager.hpp>

namespace le {
SceneManager::SceneManager(Engine::Service engine, Opt<Editor> editor) : m_shaderBufferMap(engine), m_engine(engine), m_editor(editor) {
	if (m_editor) { m_editor->init(m_engine.context(), m_engine.window()); }
}

SceneManager::~SceneManager() {
	close();
	if (m_editor) { m_editor->deinit(); }
}

Viewport const& SceneManager::sceneView() const noexcept {
	static constexpr Viewport fallback;
	return m_editor ? m_editor->view() : fallback;
}

bool SceneManager::open(Hash id) {
	if (auto it = m_scenes.find(id); it != m_scenes.end()) {
		close();
		m_active = &it->second;
		m_active->scene->open();
		logI(LC_EndUser, "[Scene] [{}] opened", m_active->id);
		return true;
	}
	return false;
}

void SceneManager::tick(Time_s dt) {
	if (m_active) {
		auto p = m_engine.profile("tick");
		m_active->scene->tick(dt);
	}
}

void SceneManager::render(graphics::RGBA clear) {
	auto p = m_engine.profile("render");
	graphics::RenderBegin rb{clear};
	if constexpr (levk_editor) {
		if (m_editor) { rb.view = m_editor->update(sceneRef()); }
	}
	if (auto frame = RenderFrame(m_engine, rb)) {
		if (m_active) {
			auto const view = ShaderSceneView::make(m_active->scene->camera(), m_engine.sceneSpace());
			m_active->scene->render(frame.renderPass(), view);
		}
		if constexpr (levk_editor) {
			if (m_editor) { m_editor->render(frame.renderPass().commandBuffers().front()); }
		}
		m_shaderBufferMap.swap();
	}
}

void SceneManager::close() {
	m_engine.executor().stop();
	if (m_active) {
		m_active->scene->close();
		logI(LC_EndUser, "[Scene] [{}] closed", m_active->id);
		m_active = {};
	}
}
} // namespace le
