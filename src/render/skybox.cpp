#include <core/services.hpp>
#include <engine/assets/asset_store.hpp>
#include <engine/engine.hpp>
#include <engine/render/skybox.hpp>
#include <engine/utils/logger.hpp>
#include <graphics/geometry.hpp>
#include <graphics/mesh_primitive.hpp>
#include <graphics/texture.hpp>

namespace le {
Skybox::Skybox(not_null<Cubemap const*> cubemap) : m_cubemap(cubemap) { ENSURE(m_cubemap->type() == graphics::Texture::Type::eCube, "Invalid cubemap"); }

MeshView Skybox::mesh() const {
	if (auto store = Services::find<AssetStore>()) {
		auto cube = store->find<MeshPrimitive>("meshes/cube");
		if (!cube) { return {}; }
		if (!m_cubemap->ready()) { return {}; }
		m_material.map_Kd = m_cubemap;
		return MeshObj{.primitive = &*cube, .material = &m_material};
	}
	return {};
}
} // namespace le
