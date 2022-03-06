#pragma once
#include <levk/graphics/device/device.hpp>

namespace le::graphics {
template <typename T, typename Del>
struct DeferDeleter {
	void operator()(Device& device, T t) const {
		device.defer([&device, t = std::move(t)] { Del{}(device, std::move(t)); });
	}
};

template <typename Del>
struct DeferDeleter<void, Del> {
	void operator()(Device& device) const {
		device.defer([&device] { Del{}(device); });
	}
};

template <typename T, typename Del = Device::Deleter<T>>
using Defer = Device::Unique<T, DeferDeleter<T, Del>>;
} // namespace le::graphics
