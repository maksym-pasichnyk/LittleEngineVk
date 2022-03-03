#pragma once
#include <levk/graphics/device/device.hpp>

namespace le::graphics {
template <typename T>
struct Deleter {
	void operator()(Device& device, T const& t) const;
};

template <typename T, typename D = Deleter<T>>
class Defer {
  public:
	Defer() = default;
	Defer(T t, not_null<Device*> device) noexcept : m_t(std::move(t)), m_device(device) {}

	Defer(Defer&& rhs) noexcept : Defer() { exchg(*this, rhs); }
	Defer& operator=(Defer rhs) noexcept { return (exchg(*this, rhs), *this); }
	~Defer();

	bool active() const noexcept { return m_device && m_t != T{}; }
	T const& get() const noexcept { return m_t; }
	T& get() noexcept { return m_t; }
	operator T const&() const noexcept { return m_t; }

  private:
	static void exchg(Defer& lhs, Defer& rhs) noexcept;

	T m_t{};
	Device* m_device{};
};

template <typename D>
class Defer<void, D> {
  public:
	Defer(Opt<Device> device = {}) noexcept : m_device(device) {}

	Defer(Defer&& rhs) noexcept : Defer() { std::swap(m_device, rhs.m_device); }
	Defer& operator=(Defer rhs) noexcept { return (std::swap(m_device, rhs.m_device), *this); }
	~Defer();

	bool active() const noexcept { return m_device; }

  private:
	Device* m_device{};
};

// impl

template <typename T>
void Deleter<T>::operator()(Device& device, T const& t) const {
	device.destroy(t);
}

template <typename T, typename D>
Defer<T, D>::~Defer() {
	if (active()) {
		m_device->defer([d = m_device, t = std::move(m_t)] { D{}(*d, t); });
	}
}

template <typename T, typename D>
void Defer<T, D>::exchg(Defer& lhs, Defer& rhs) noexcept {
	std::swap(lhs.m_t, rhs.m_t);
	std::swap(lhs.m_device, rhs.m_device);
}

template <typename D>
Defer<void, D>::~Defer() {
	if (active()) {
		m_device->defer([d = m_device] { D{}(*d); });
	}
}
} // namespace le::graphics
