#include <array>
#include <string>
#include <unordered_set>
#include <core/maths.hpp>
#include <core/time.hpp>
#include <core/utils/algo.hpp>
#include <core/utils/error.hpp>
#include <dumb_ecf/registry.hpp>
#include <dumb_tasks/scheduler.hpp>
#include <ktl/kthread.hpp>
#include <ktl/tmutex.hpp>

using namespace le;
using namespace decf;

namespace {
struct A {};
struct B {};
struct C {};
struct D {};
struct E {};
struct F {};

std::unordered_set<entity> g_spawned;
std::mutex g_mutex;

bool verify(entity entity) {
	std::scoped_lock lock(g_mutex);
	bool const bRet = !utils::contains(g_spawned, entity);
	ENSURE(bRet, "DUPLICATE");
	g_spawned.insert(entity);
	return bRet;
}
} // namespace

int main() {
	dts::task_queue tq;
	ktl::tmutex<registry> registry;
	constexpr s32 entityCount = 10000;
	std::array<entity, entityCount> entities;
	s32 idx = 0;
	bool bPass = true;
	for (auto& entity : entities) {
		tq.enqueue([&entity, &registry, &idx, &bPass]() {
			ktl::tlock lock(registry);
			entity = lock->spawn("e" + std::to_string(idx++));
			bPass &= verify(entity);
			auto const toss = maths::randomRange(0, 1 << 7);
			if (toss & 1 << 0) {
				if (!lock->find<A>(entity)) { lock->attach<A>(entity); }
			}
			if (toss & 1 << 1) {
				if (!lock->find<B>(entity)) { lock->attach<B>(entity); }
			}
			if (toss & 1 << 2) {
				if (!lock->find<C>(entity)) { lock->attach<C>(entity); }
			}
			if (toss & 1 << 3) {
				if (!lock->find<D>(entity)) { lock->attach<D>(entity); }
			}
			if (toss & 1 << 4) {
				if (!lock->find<D>(entity) && !lock->find<C>(entity)) {
					lock->attach<D>(entity);
					lock->attach<D>(entity);
				}
			}
			if (toss & 1 << 5) {
				if (!lock->find<E>(entity)) { lock->attach<E>(entity); }
			}
			if (toss & 1 << 6) {
				if (!lock->find<F>(entity)) { lock->attach<F>(entity); }
			}
		});
	}
	tq.wait_idle();
	if (!bPass) { return 1; }
	std::vector<dts::task_id> handles;
	{
		auto wait = []() -> Time_ms { return time::cast<Time_ms>(Time_us(maths::randomRange(0, 3000))); };
		for (s32 i = 0; i < entityCount / 10; ++i) {
			handles.push_back(tq.enqueue([&registry, &entities, wait]() {
				ktl::kthread::sleep_for(wait());
				std::size_t const idx = (std::size_t)maths::randomRange(0, (s32)entities.size() - 1);
				ktl::tlock lock(registry);
				lock->destroy(entities[idx]);
			}));
			handles.push_back(tq.enqueue([&registry, &entities, wait]() {
				ktl::kthread::sleep_for(wait());
				std::size_t const idx = (std::size_t)maths::randomRange(0, (s32)entities.size() - 1);
				ktl::tlock lock(registry);
				lock.get().detach<A>(entities[idx]);
				lock.get().detach<B>(entities[idx]);
				lock.get().detach<D>(entities[idx]);
			}));
			handles.push_back(tq.enqueue([&registry, &entities, wait]() {
				ktl::kthread::sleep_for(wait());
				std::size_t const idx = (std::size_t)maths::randomRange(0, (s32)entities.size() - 1);
				ktl::tlock lock(registry);
				lock->enable(entities[idx], false);
			}));
		}
	}
	{
		constexpr s32 viewIters = 10;
		for (s32 i = 0; i < viewIters; ++i) {
			[[maybe_unused]] auto viewA = ktl::tlock(registry)->view<A>();
			[[maybe_unused]] auto viewB = ktl::tlock(registry)->view<B>();
			[[maybe_unused]] auto viewC = ktl::tlock(registry)->view<C>();
			[[maybe_unused]] auto viewAB = ktl::tlock(registry)->view<A, B>();
			[[maybe_unused]] auto viewABC = ktl::tlock(registry)->view<A, B, C>();
			[[maybe_unused]] auto viewCEF = ktl::tlock(registry)->view<C, E, F>();
		}
	}
	tq.wait_tasks(handles);
	// To test:
	// - flags: disabled, destroyed, debug
	//		toggle multiple objects multiple times
	// - spawn, add, get, destroy
	return 0;
}
