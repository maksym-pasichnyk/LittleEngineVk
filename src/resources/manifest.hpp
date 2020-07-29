#pragma once
#include <atomic>
#include <memory>
#include <core/gdata.hpp>
#include <core/std_types.hpp>
#include <core/tasks.hpp>
#include <core/utils.hpp>
#include <engine/resources/resource_list.hpp>
#include <engine/resources/resources.hpp>

namespace le::res
{
template <typename T>
struct ResourceData final
{
	typename T::CreateInfo createInfo;
	stdfs::path id;
};

class Manifest
{
public:
	enum class Status : s8
	{
		eIdle,
		eReady,
		eExtractingData,
		eLoadingResources,
		eWaitingForResources,
		eTerminating,
		eError,
	};

	struct Info final
	{
		std::vector<ResourceData<res::Shader>> shaders;
		std::vector<ResourceData<res::Texture>> textures;
		std::vector<ResourceData<res::Texture>> cubemaps;
		std::vector<ResourceData<res::Material>> materials;
		std::vector<ResourceData<res::Mesh>> meshes;
		std::vector<ResourceData<res::Model>> models;
		std::vector<ResourceData<res::Font>> fonts;

		void intersect(ResourceList ids);

		ResourceList exportList() const;
		bool isEmpty() const;
	};

public:
	static std::string const s_tName;

protected:
	struct Data final
	{
		std::atomic<std::size_t> idCount = 0;
		std::atomic<std::size_t> dataCount = 0;
	};

public:
	ResourceList m_loaded;
	Info m_toLoad;

protected:
	GData m_manifest;
	Data m_data;
	std::vector<std::shared_ptr<tasks::Handle>> m_running;
	std::vector<res::GUID> m_loading;
	Lockable<std::mutex> m_mutex;
	res::Semaphore m_semaphore;
	Status m_status = Status::eIdle;
	bool m_bParsed = false;

public:
	bool read(stdfs::path const& id);
	void start();
	Status update(bool bTerminate = false);
	ResourceList parse();
	void reset();
	bool isIdle() const;
	bool isReady() const;

	static void unload(ResourceList const& list);

protected:
	void loadData();
	void loadResources();
	bool eraseDone(bool bWaitingJobs);
	void addJobs(std::vector<std::shared_ptr<tasks::Handle>> handles);
};
} // namespace le::res
