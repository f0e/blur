#pragma once

#ifdef _DEBUG
#	define DEBUG_LOG(...) u::log(__VA_ARGS__)
#else
#	define DEBUG_LOG(...) ((void)0)
#endif

namespace u {
	inline void log(const std::string& msg) {
		std::cout << msg << '\n';
	}

	inline void log(const std::wstring& msg) {
		std::wcout << msg << L'\n';
	}

	template<typename... Args>
	void log(const std::format_string<Args...> format_str, Args&&... args) {
		std::cout << std::format(format_str, std::forward<Args>(args)...) << '\n';
	}

	template<typename... Args>
	void log(const std::wformat_string<Args...> format_str, Args&&... args) {
		std::wcout << std::format(format_str, std::forward<Args>(args)...) << L'\n';
	}

	inline void log_error(const std::string& msg) {
		std::cerr << msg << '\n';
	}

	inline void log_error(const std::wstring& msg) {
		std::wcerr << msg << L'\n';
	}

	template<typename... Args>
	void log_error(const std::format_string<Args...> format_str, Args&&... args) {
		std::cerr << std::format(format_str, std::forward<Args>(args)...) << '\n';
	}

	template<typename... Args>
	void log_error(const std::wformat_string<Args...> format_str, Args&&... args) {
		std::wcerr << std::format(format_str, std::forward<Args>(args)...) << L'\n';
	}

	// NOLINTBEGIN not my code bud
	template<typename container_type>
	struct enumerate_wrapper {
		using iterator_type = std::conditional_t<
			std::is_const_v<container_type>,
			typename container_type::const_iterator,
			typename container_type::iterator>;
		using pointer_type = std::conditional_t<
			std::is_const_v<container_type>,
			typename container_type::const_pointer,
			typename container_type::pointer>;
		using reference_type = std::conditional_t<
			std::is_const_v<container_type>,
			typename container_type::const_reference,
			typename container_type::reference>;

		constexpr enumerate_wrapper(container_type& c) : container(c) {}

		struct enumerate_wrapper_iter {
			size_t index;
			iterator_type value;

			constexpr bool operator!=(const iterator_type& other) const {
				return value != other;
			}

			constexpr enumerate_wrapper_iter& operator++() {
				++index;
				++value;
				return *this;
			}

			constexpr std::pair<size_t, reference_type> operator*() {
				return std::pair<size_t, reference_type>{ index, *value };
			}
		};

		constexpr enumerate_wrapper_iter begin() {
			return { 0, std::begin(container) };
		}

		constexpr iterator_type end() {
			return std::end(container);
		}

		container_type& container;
	};

	template<typename container_type>
	constexpr auto enumerate(container_type& c) {
		return enumerate_wrapper(c);
	}

	template<typename container_type>
	constexpr auto const_enumerate(const container_type& c) {
		return enumerate_wrapper(c);
	}

	// NOLINTEND

	template<typename Container>
	auto join(const Container& container, const typename Container::value_type::value_type* delimiter) {
		using CharT = typename Container::value_type::value_type;
		std::basic_ostringstream<CharT> result;

		auto it = container.begin();
		if (it != container.end()) {
			result << *it++; // Add the first element without a delimiter
		}
		while (it != container.end()) {
			result << delimiter << *it++;
		}
		return result.str();
	}

	std::string trim(std::string_view str);
	std::string random_string(int len);
	std::vector<std::string> split_string(std::string str, const std::string& delimiter);
	std::wstring towstring(const std::string& str);
	std::string tostring(const std::wstring& wstr);
	std::string to_lower(const std::string& str);

	std::optional<std::filesystem::path> get_program_path(const std::string& program_name);

	std::string get_executable_path();

	float lerp(
		float value, float target, float speed, float snap_offset = 0.01f
	); // if animations are jumping at the end then lower snap offset. todo: maybe dynamically generate it somehow

	void sleep(double seconds); // https://blog.bearcats.nl/perfect-sleep-function/ kill windows

	std::filesystem::path get_resources_path();
}
