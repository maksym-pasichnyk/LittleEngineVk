#include <dumb_test/dtest.hpp>
#include <ktl/delegate.hpp>

namespace {
struct tester_t {
	int& called;

	template <typename... T>
	void operator()(T const&...) {
		++called;
	}
};

TEST(delegate_attach) {
	using delegate = ktl::delegate<>;
	int called{};
	delegate d;
	tester_t t{called};
	[[maybe_unused]] auto tag = d.attach(t);
	EXPECT_EQ(d.size(), 1U);
	d();
	EXPECT_GT(t.called, 0);
}

TEST(delegate_detach) {
	using delegate = ktl::delegate<int>;
	int called{};
	delegate d;
	tester_t t{called};
	EXPECT_EQ(d.empty(), true);
	auto tag = d.attach(t);
	d(42);
	EXPECT_GT(t.called, 0);
	d.detach(tag);
	EXPECT_EQ(d.size(), 0U);
	int const prev = t.called;
	d(-1);
	EXPECT_EQ(t.called, prev);
}

TEST(delegate_signal) {
	using delegate = ktl::delegate<>;
	int called{};
	delegate d;
	tester_t t{called};
	auto s = d.make_signal();
	s += t;
	s += t;
	d();
	EXPECT_EQ(t.called, 2);
	auto s1 = std::move(s);
	d();
	EXPECT_EQ(t.called, 4);
}

TEST(delegate_lifetime) {
	using delegate = ktl::delegate<>;
	int called{};
	delegate::handle s0;
	tester_t t0{called};
	{
		delegate d;
		s0 = d.make_signal();
		s0 += t0;
		EXPECT_EQ(t0.called, 0);
		auto s1 = d.make_signal();
		s1 += t0;
		d();
		EXPECT_EQ(t0.called, 2);
		s1.clear();
		d();
		EXPECT_EQ(t0.called, 3);
	}
	EXPECT_EQ(s0.active(), false);
}
} // namespace
