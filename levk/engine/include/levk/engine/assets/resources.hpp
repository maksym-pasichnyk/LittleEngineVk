#pragma once
#include <ktl/async/kmutex.hpp>
#include <ktl/either.hpp>
#include <levk/core/hash.hpp>
#include <levk/core/io/file_monitor.hpp>
#include <levk/core/io/media.hpp>
#include <levk/core/not_null.hpp>
#include <levk/core/os.hpp>
#include <shared_mutex>
#include <unordered_map>

constexpr bool levk_resourceMonitor = levk_debug;

namespace le {
class Resource {
  public:
	enum class Type { eText, eBinary };

	io::Path const& uri() const { return m_uri; }
	std::string_view string() const noexcept;
	Span<std::byte const> bytes() const noexcept;
	Type type() const noexcept;

	bool monitoring() const noexcept;
	std::optional<io::FileMonitor::Status> status() const;

  private:
	bool load(io::Media const& media, io::Path uri, Type type, bool monitor, bool silent);

	using Data = ktl::either<bytearray, std::string>;

	Data m_data;
	io::Path m_uri;
	Type m_type = Type::eBinary;
	std::optional<io::FileMonitor> m_monitor;

	friend class Resources;
};

class Resources {
  public:
	enum class Flag { eMonitor, eReload, eSilent };
	using Flags = ktl::enum_flags<Flag>;

	using FMode = io::FileMonitor::Mode;

	void media(not_null<io::Media const*> media);
	io::Media const& media() const;
	io::FSMedia& fsMedia();

	Resource const* find(Hash uri) const noexcept;
	Resource const* load(io::Path uri, Resource::Type type, Flags flags = {});
	Resource const* loadFirst(Span<io::Path> uris, Resource::Type type, Flags flags = {});
	bool loaded(Hash uri) const noexcept;

	void update();
	void clear();

  protected:
	Resource const* loadImpl(io::Path uri, Resource::Type type, Flags flags);

	using ResourceMap = std::unordered_map<Hash, Resource>;

	ktl::strict_tmutex<ResourceMap> m_loaded;
	io::FSMedia m_fsMedia;
	io::Media const* m_media = nullptr;
};
} // namespace le
