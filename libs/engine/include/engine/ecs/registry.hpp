#pragma once
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>
#include "core/flags.hpp"
#include "core/std_types.hpp"
#include "core/time.hpp"
#include "entity.hpp"
#include "component.hpp"

namespace le
{
class Registry
{
public:
	enum class DestroyMode : u8
	{
		eDeferred,
		eImmediate,
		eCOUNT_,
	};

	enum class Flag : u8
	{
		eDisabled,
		eDestroyed,
		eDebug,
		eCOUNT_
	};
	using Flags = TFlags<Flag>;

	template <typename... T>
	using View = std::unordered_map<Entity::ID, std::tuple<T*...>>;

private:
	static std::unordered_map<std::type_index, Component::Sign> s_signs;

protected:
	std::unordered_map<Entity::ID, Flags> m_entityFlags;
	std::unordered_map<Entity::ID, std::string> m_entityNames;
	std::unordered_map<Component::Sign, std::string> m_componentNames;
	std::unordered_map<Entity::ID, std::unordered_map<Component::Sign, std::unique_ptr<Component>>> m_db;
	mutable std::mutex m_mutex;

private:
	Entity::ID m_nextID = 0;
	DestroyMode m_destroyMode;

public:
	static std::string const s_tName;

public:
	Registry(DestroyMode destroyMode = DestroyMode::eDeferred);
	virtual ~Registry();

public:
	template <typename T>
	static Component::Sign signature();

	template <typename... Comps>
	constexpr static size_t count()
	{
		static_assert((std::is_base_of_v<Component, Comps> && ...), "Comp must derive from Component!");
		return sizeof...(Comps);
	}

public:
	Entity spawnEntity(std::string name);

	template <typename Comp1, typename... Comps>
	Entity spawnEntity(std::string name);

	bool destroyEntity(Entity entity);
	bool destroyComponents(Entity entity);
	bool setEnabled(Entity entity, bool bEnabled);
	bool setDebug(Entity entity, bool bDebug);

	bool isEnabled(Entity entity) const;
	bool isAlive(Entity entity) const;
	bool isDebugSet(Entity entity) const;

	template <typename Comp, typename... Args>
	Comp* addComponent(Entity entity, Args... args);

	template <typename Comp1, typename Comp2, typename... Comps>
	void addComponent(Entity entity);

	template <typename Comp>
	Comp const* component(Entity entity) const;

	template <typename Comp>
	Comp* component(Entity entity);

	template <typename Comp>
	bool destroyComponent(Entity entity);

	template <typename Comp1, typename... Comps>
	View<Comp1 const, Comps const...> view(Flags mask = Flag::eDestroyed | Flag::eDisabled, Flags pattern = {}) const;

	template <typename Comp1, typename... Comps>
	View<Comp1, Comps...> view(Flags mask = Flag::eDestroyed | Flag::eDisabled, Flags pattern = {});

	void cleanDestroyed();

private:
	template <typename Comp, typename... Args>
	Component* addComponent_Impl(Entity entity, Args... args);

	template <typename Comp1, typename Comp2, typename... Comps>
	void addComponent_Impl(Entity entity);

	template <typename Comp>
	static Comp* component_Impl(std::unordered_map<Component::Sign, std::unique_ptr<Component>>& compMap);

	template <typename Comp>
	static Comp const* component_Impl(std::unordered_map<Component::Sign, std::unique_ptr<Component>> const& compMap);

	// const helper
	template <typename T, typename Comp>
	static Comp* component_Impl(T* pThis, Entity::ID id);

	// const helper
	template <typename T, typename Comp1, typename... Comps>
	static View<Comp1, Comps...> view(T* pThis, Flags mask, Flags pattern);

	Component* attach(Component::Sign sign, std::unique_ptr<Component>&& uComp, Entity entity);
	void detach(Component const* pComponent, Entity::ID id);

	using EFMap = std::unordered_map<Entity::ID, Flags>;
	EFMap::iterator destroyEntity(EFMap::iterator iter, Entity::ID id);
};

template <typename T>
Component::Sign Registry::signature()
{
	auto const& t = typeid(T);
	auto const index = std::type_index(t);
	auto search = s_signs.find(index);
	if (search == s_signs.end())
	{
		auto result = s_signs.emplace(index, (Component::Sign)t.hash_code());
		ASSERT(result.second, "Insertion failure");
		search = result.first;
	}
	return search->second;
}

template <typename Comp1, typename... Comps>
Entity Registry::spawnEntity(std::string name)
{
	auto entity = spawnEntity(std::move(name));
	addComponent_Impl<Comp1, Comps...>(entity);
}

template <typename Comp, typename... Args>
Comp* Registry::addComponent(Entity entity, Args... args)
{
	if (m_entityFlags.find(entity.id) != m_entityFlags.end())
	{
		return dynamic_cast<Comp*>(addComponent_Impl<Comp>(entity, std::forward<Args>(args)...));
	}
	return nullptr;
}

template <typename Comp1, typename Comp2, typename... Comps>
void Registry::addComponent(Entity entity)
{
	if (m_entityFlags.find(entity.id) != m_entityFlags.end())
	{
		addComponent_Impl<Comp1, Comp2, Comps...>(entity);
	}
	return;
}

template <typename Comp>
Comp const* Registry::component(Entity entity) const
{
	std::scoped_lock<std::mutex> lock(m_mutex);
	return component_Impl<Registry const, Comp const>(this, entity.id);
}

template <typename Comp>
Comp* Registry::component(Entity entity)
{
	std::scoped_lock<std::mutex> lock(m_mutex);
	return component_Impl<Registry, Comp>(this, entity.id);
}

template <typename Comp>
bool Registry::destroyComponent(Entity entity)
{
	static_assert(std::is_base_of_v<Component, Comp>, "Comp must derive from Component!");
	if (m_entityFlags.find(entity.id) != m_entityFlags.end())
	{
		auto const sign = signature<Comp>();
		if (auto search = m_db[entity.id].find(sign); search != m_db[entity.id].end())
		{
			detach(search->second.get(), entity);
			return true;
		}
	}
	return false;
}

template <typename Comp1, typename... Comps>
typename Registry::View<Comp1 const, Comps const...> Registry::view(Flags mask, Flags pattern) const
{
	return view<Registry const, Comp1 const, Comps const...>(this, mask, pattern);
}

template <typename Comp1, typename... Comps>
typename Registry::View<Comp1, Comps...> Registry::view(Flags mask, Flags pattern)
{
	return view<Registry, Comp1, Comps...>(this, mask, pattern);
}

template <typename Comp, typename... Args>
Component* Registry::addComponent_Impl(Entity entity, Args... args)
{
	static_assert(std::is_base_of_v<Component, Comp>, "Comp must derive from Component!");
	static_assert((std::is_constructible_v<Comp, Args> && ...), "Cannot construct Comp with given Args...");
	return attach(signature<Comp>(), std::make_unique<Comp>(std::forward<Args>(args)...), entity);
}

template <typename Comp1, typename Comp2, typename... Comps>
void Registry::addComponent_Impl(Entity entity)
{
	addComponent_Impl<Comp1>(entity);
	addComponent_Impl<Comp2, Comps...>(entity);
}

template <typename Comp>
Comp* Registry::component_Impl(std::unordered_map<Component::Sign, std::unique_ptr<Component>>& compMap)
{
	auto const sign = signature<Comp>();
	if (auto search = compMap.find(sign); search != compMap.end())
	{
		return dynamic_cast<Comp*>(search->second.get());
	}
	return nullptr;
}

template <typename Comp>
Comp const* Registry::component_Impl(std::unordered_map<Component::Sign, std::unique_ptr<Component>> const& compMap)
{
	auto const sign = signature<Comp>();
	if (auto search = compMap.find(sign); search != compMap.end())
	{
		return dynamic_cast<Comp const*>(search->second.get());
	}
	return nullptr;
}

template <typename T, typename Comp>
Comp* Registry::component_Impl(T* pThis, Entity::ID id)
{
	static_assert(std::is_base_of_v<Component, Comp>, "Comp must derive from Component!");
	auto cSearch = pThis->m_db.find(id);
	if (cSearch != pThis->m_db.end())
	{
		return component_Impl<Comp>(cSearch->second);
	}
	return nullptr;
}

template <typename T, typename Comp1, typename... Comps>
typename Registry::View<Comp1, Comps...> Registry::view(T* pThis, Flags mask, Flags pattern)
{
	View<Comp1, Comps...> ret;
	auto const signs = {signature<Comp1>(), (signature<Comps>(), ...)};
	std::scoped_lock<std::mutex> lock(pThis->m_mutex);
	for (auto& [id, flags] : pThis->m_entityFlags)
	{
		if ((flags & mask) == (pattern & mask))
		{
			auto search = pThis->m_db.find(id);
			ASSERT(search != pThis->m_db.end(), "Invariant violated!");
			auto& compMap = search->second;
			auto checkSigns = [&compMap](auto sign) -> bool { return compMap.find(sign) != compMap.end(); };
			bool const bHasAll = std::all_of(signs.begin(), signs.end(), checkSigns);
			if (bHasAll)
			{
				ret[id] = std::make_tuple(component_Impl<Comp1>(compMap), (component_Impl<Comps>(compMap))...);
			}
		}
	}
	return ret;
}
} // namespace le
