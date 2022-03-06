#pragma once
#include <ktl/fixed_vector.hpp>
#include <levk/core/std_types.hpp>
#include <charconv>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace le::utils {
///
/// \brief Convert `byteCount` bytes into human-friendly format
/// \returns pair of size in `f32` and the corresponding unit
///
std::pair<f32, std::string_view> friendlySize(u64 byteCount) noexcept;

///
/// \brief Demangle a compiler symbol name
///
std::string_view demangle(std::string_view name, bool minimal);

///
/// \brief Obtain demangled type name of an object or a type
///
template <typename T, bool Minimal = true>
std::string_view tName(T const* t = nullptr);

///
/// \brief Remove namespace prefixes from a type string
///
std::string_view removeNamespaces(std::string_view name) noexcept;

struct FromChars {
	template <typename T>
	bool operator()(std::string_view const str, T& out) const noexcept;
};
///
/// \brief Convert input to a numeric / boolean type
///
template <typename T>
T to(std::string_view input, T const& fallback = {}) noexcept;
///
/// \brief Convert raw bytes to string (ASCII only)
///
std::string toText(bytearray const& buffer);

std::string toString(std::istream const& in);

///
/// \brief Slice a string into a pair via the first occurence of `delimiter`
///
std::pair<std::string, std::string> bisect(std::string_view input, char delimiter);
///
/// \brief Substitute an input set of chars with a given replacement
///
void substitute(std::string& out_input, std::initializer_list<std::pair<char, char>> replacements);
///
/// \brief Tokenise a string in place via `delimiter`
///
template <std::size_t N>
ktl::fixed_vector<std::string_view, N> tokenise(std::string_view text, char delim);
///
/// \brief Concatenate a container of strings via delim
///
template <typename Cont, typename Delim>
std::string concatenate(Cont&& strings, Delim const& delim);

// impl

template <typename T, bool Minimal>
std::string_view tName(T const* t) {
	if constexpr (Minimal) {
		return demangle(t ? typeid(*t).name() : typeid(T).name(), true);
	} else {
		return demangle(t ? typeid(*t).name() : typeid(T).name(), false);
	}
}

template <typename T>
T to(std::string_view const str, T const& fallback) noexcept {
	T ret;
	if (FromChars{}(str, ret)) { return ret; }
	return fallback;
}

template <std::size_t N>
ktl::fixed_vector<std::string_view, N> tokenise(std::string_view text, char delim) {
	ktl::fixed_vector<std::string_view, N> ret;
	while (!text.empty() && ret.has_space()) {
		std::size_t const idx = text.find_first_of(delim);
		if (idx < text.size()) {
			ret.push_back(text.substr(0, idx));
			text = idx + 1 < text.size() ? text.substr(idx + 1) : std::string_view();
		} else {
			ret.push_back(text);
			text = {};
		}
	}
	return ret;
}

template <typename Cont, typename Delim>
std::string concatenate(Cont&& strings, Delim const& delim) {
	std::stringstream ret;
	bool first = true;
	for (auto const& str : strings) {
		if (!first) { ret << delim; }
		ret << str;
		first = false;
	}
	return ret.str();
}

template <typename T>
bool FromChars::operator()(std::string_view const str, T& out) const noexcept {
	auto [_, ec] = std::from_chars(str.data(), str.data() + str.size(), out);
	return ec == std::errc();
}

template <>
bool FromChars::operator()<bool>(std::string_view const str, bool& out) const noexcept;
} // namespace le::utils
