#pragma once
#include <memory>
#include <type_traits>
#include <unordered_map>
#include "core/assert.hpp"
#include "core/map_store.hpp"
#include "asset.hpp"

namespace le
{
inline class Resources* g_pResources = nullptr;

class Resources final
{
public:
	class Service final
	{
	public:
		Service();
		~Service();
	};

private:
	TMapStore<std::unordered_map<std::string, std::unique_ptr<Asset>>> m_resources;

public:
	Resources();
	~Resources();

	template <typename T>
	T* create(stdfs::path const& id, typename T::Info info)
	{
		static_assert(std::is_base_of_v<Asset, T>, "T must derive from Asset!");
		ASSERT(!m_resources.find(id.generic_string()).bResult, "ID already loaded!");
		auto uT = std::make_unique<T>(id, std::move(info));
		T* pT = nullptr;
		if (uT && uT->m_status != Asset::Status::eError)
		{
			uT->setup();
			pT = uT.get();
			m_resources.emplace(id.generic_string(), std::move(uT));
		}
		return pT;
	}

	template <typename T>
	T* get(stdfs::path const& id)
	{
		static_assert(std::is_base_of_v<Asset, T>, "T must derive from Asset!");
		auto [pT, bResult] = m_resources.find(id.generic_string());
		if (bResult && pT)
		{
			return dynamic_cast<T*>(pT->get());
		}
		return nullptr;
	}

	template <typename T>
	bool unload(stdfs::path const& id)
	{
		static_assert(std::is_base_of_v<Asset, T>, "T must derive from Asset!");
		return m_resources.unload(id.generic_string());
	}

	void unloadAll();

	void update();

private:
	void init();

	friend class Service;
};
} // namespace le
