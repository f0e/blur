#pragma once

namespace helpers {
	template<typename container_type>
	struct enumerate_wrapper {
		using iterator_type = std::conditional_t<std::is_const_v<container_type>, typename container_type::const_iterator, typename container_type::iterator>;
		using pointer_type = std::conditional_t<std::is_const_v<container_type>, typename container_type::const_pointer, typename container_type::pointer>;
		using reference_type = std::conditional_t<std::is_const_v<container_type>, typename container_type::const_reference, typename container_type::reference>;

		constexpr enumerate_wrapper(container_type& c)
			: container(c) {
		}

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
				return std::pair<size_t, reference_type>{ index, * value };
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

	template<typename... args_t>
	inline void debug_log(std::string_view str, args_t&&... args) {
#ifdef _DEBUG
		std::cout << "[debug]";
		printf(str.data(), std::forward<args_t>(args)...);
		putchar('\n');
#endif
	};

	std::string trim(std::string_view str);
	std::string random_string(int len);
	std::vector<std::string> split_string(std::string str, const std::string& delimiter);
	std::wstring towstring(const std::string& str);
	std::string tostring(const std::wstring& wstr);
	std::string to_lower(const std::string& str);

	void set_hidden(const std::filesystem::path& path);

	int exec(std::wstring command, std::wstring run_dir = L".");

	bool detect_command(const std::string& command);

	std::string get_executable_path();

	void read_line_from_handle(HANDLE handle, std::function<void(std::string)> on_line_fn);

	HWND get_window(DWORD dwProcessId);

	template <typename TP>
	inline std::time_t to_time_t(TP tp) {
		using namespace std::chrono;
		auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now()
			+ system_clock::now());
		return system_clock::to_time_t(sctp);
	}
}