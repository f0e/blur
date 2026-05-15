#include "utils.h"
#include "common/config_presets.h"
#include "common/config_app.h"

namespace {
	bool init_hw = false;
}

// NOLINTBEGIN gpt ass code
std::wstring u::towstring(const std::string& str) {
	if (str.empty())
		return std::wstring();

#ifdef _WIN32
	// Windows-specific implementation
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size_needed);
	return result;
#else
	// POSIX systems (Linux, macOS, etc.)
	std::vector<wchar_t> buf(str.size() + 1);
	std::mbstowcs(&buf[0], str.c_str(), str.size() + 1);
	return std::wstring(&buf[0]);
#endif
}

std::string u::tostring(const std::wstring& wstr) {
	if (wstr.empty()) {
		return std::string();
	}

#ifdef _WIN32
	// Windows-specific implementation
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size_needed, nullptr, nullptr);
	return result;
#else
	// POSIX systems (Linux, macOS, etc.)
	std::vector<char> buf((wstr.size() + 1) * MB_CUR_MAX);
	size_t converted = std::wcstombs(&buf[0], wstr.c_str(), buf.size());
	if (converted == static_cast<size_t>(-1)) {
		return std::string(); // Conversion failed
	}
	return std::string(&buf[0], converted);
#endif
}

// NOLINTEND

std::string u::trim(std::string_view str) {
	str.remove_prefix(std::min(str.find_first_not_of(" \t\r\v\n"), str.size()));
	str.remove_suffix(std::min(str.size() - str.find_last_not_of(" \t\r\v\n") - 1, str.size()));

	return std::string(str);
}

std::string u::random_string(int len) {
	std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

	std::random_device rd;
	std::mt19937 generator(rd());

	std::shuffle(str.begin(), str.end(), generator);

	return str.substr(0, len);
}

std::vector<std::string> u::split_string(std::string str, const std::string& delimiter) {
	std::vector<std::string> output;

	size_t pos = 0;
	while ((pos = str.find(delimiter)) != std::string::npos) {
		std::string token = str.substr(0, pos);
		output.push_back(token);
		str.erase(0, pos + delimiter.length());
	}

	output.push_back(str);

	return output;
}

std::string u::to_lower(const std::string& str) {
	std::string out = str;

	std::ranges::for_each(out, [](char& c) {
		c = std::tolower(c);
	});

	return out;
}

std::string u::truncate_with_ellipsis(const std::string& input, std::size_t max_length) {
	const std::string ellipsis = "...";
	if (input.length() > max_length) {
		if (max_length <= ellipsis.length()) {
			return ellipsis.substr(0, max_length); // handle very small max_length
		}
		return input.substr(0, max_length - ellipsis.length()) + ellipsis;
	}
	return input;
}

std::optional<std::filesystem::path> u::get_program_path(const std::string& program_name) {
	namespace bp = boost::process;
	namespace fs = boost::filesystem;

	fs::path program_path = bp::search_path(program_name);

	std::filesystem::path path(program_path.native());

	if (!std::filesystem::exists(path))
		return {};

	return path;
}

// NOLINTBEGIN gpt ass code
std::string u::get_executable_path() {
#if defined(_WIN32)
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	return std::string(path);
#elif defined(__linux__)
	char path[PATH_MAX];
	ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
	return std::string(path, (count > 0) ? count : 0);
#elif defined(__APPLE__)
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size); // Get the required size
	std::vector<char> path(size);
	if (_NSGetExecutablePath(path.data(), &size) == 0) {
		return std::string(path.data());
	}
	return "";
#else
#	error "Unsupported platform"
#endif
}

// NOLINTEND

constexpr int64_t PERIOD = 1;
constexpr int64_t TOLERANCE = 1'020'000;
constexpr int64_t MAX_TICKS = PERIOD * 9'500;

void u::sleep(double seconds) {
#ifndef WIN32
	std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
#else // KILLLLL WINDOWS
	using namespace std;
	using namespace chrono;

	auto t = high_resolution_clock::now();
	auto target = t + nanoseconds(int64_t(seconds * 1e9));

	static HANDLE timer;
	if (!timer)
		timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

	int64_t maxTicks = PERIOD * 9'500;
	for (;;) {
		int64_t remaining = (target - t).count();
		int64_t ticks = (remaining - TOLERANCE) / 100;
		if (ticks <= 0)
			break;
		if (ticks > maxTicks)
			ticks = maxTicks;

		LARGE_INTEGER due;
		due.QuadPart = -ticks;
		SetWaitableTimerEx(timer, &due, 0, NULL, NULL, NULL, 0);
		WaitForSingleObject(timer, INFINITE);
		t = high_resolution_clock::now();
	}

	// spin
	while (high_resolution_clock::now() < target)
		YieldProcessor();
#endif
}

std::filesystem::path u::get_resources_path() {
#ifdef __APPLE__
	// Resources path if part of a macos bundle
	CFBundleRef bundle = CFBundleGetMainBundle();
	if (bundle) {
		CFURLRef resources_url = CFBundleCopyResourcesDirectoryURL(bundle);
		if (resources_url) {
			std::array<char, PATH_MAX> path{};
			if (CFURLGetFileSystemRepresentation(
					resources_url, static_cast<Boolean>(true), (UInt8*)path.data(), path.size() // NOLINT
				))
			{
				CFRelease(resources_url);
				return path.data();
			}
			CFRelease(resources_url);
		}
	}
#endif

	// binary path otherwise
	return std::filesystem::path(u::get_executable_path()).parent_path();
}

std::filesystem::path u::get_settings_path() {
	std::filesystem::path settings_path;
	const std::string app_name = "blur";

#if defined(_WIN32) || defined(_WIN64)
	// Windows: %APPDATA%\blur
	const char* app_data = std::getenv("APPDATA");
	if (app_data) {
		settings_path = std::filesystem::path(app_data) / app_name;
	}
	else {
		// Fallback if APPDATA is not available
		const char* user_profile = std::getenv("USERPROFILE");
		if (user_profile) {
			settings_path = std::filesystem::path(user_profile) / "AppData" / "Roaming" / app_name;
		}
	}
#elif defined(__APPLE__)
	// macOS: ~/Library/Application Support/blur
	const char* home = std::getenv("HOME");
	if (home) {
		settings_path = std::filesystem::path(home) / "Library" / "Application Support" / app_name;
	}
#else
	// Linux/Unix: ~/.config/blur (XDG convention)
	const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
	if (xdg_config_home && xdg_config_home[0] != '\0') {
		settings_path = std::filesystem::path(xdg_config_home) / app_name;
	}
	else {
		const char* home = std::getenv("HOME");
		if (home) {
			settings_path = std::filesystem::path(home) / ".config" / app_name;
		}
	}
#endif

	// Create directories if they don't exist
	if (!settings_path.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(settings_path, ec);
	}

	return settings_path;
}

boost::process::environment u::setup_vspipe_environment() {
	auto env = boost::this_process::environment();

#ifdef __APPLE__
	if (blur.used_installer) {
		env["PYTHONHOME"] = (blur.resources_path / "python").native();
		env["PYTHONPATH"] = (blur.resources_path / "python/lib/python3.12/site-packages").native();
	}
#endif

#ifdef __linux__
	auto app_config = config_app::get_app_config();
	if (!app_config.vapoursynth_lib_path.empty()) {
		env["LD_LIBRARY_PATH"] = app_config.vapoursynth_lib_path;
		env["PYTHONPATH"] = app_config.vapoursynth_lib_path + "/python3.12/site-packages";
	}
#endif

	return env;
}

int u::get_video_preroll_frames(const std::filesystem::path& path, double fps, double max_preroll_seconds) {
	// some mp4 files use an edit list to trim content by offsetting the start time, leaving negative pts
	// packets at the head of the file that normal players skip but some vapoursynth source plugins decode as real
	// frames, causing audio/video desync

	// this count those frames so we can trim the clip manually in vapoursynth if needed

	namespace bp = boost::process;
	bp::ipstream pipe_stream;

	const int max_packets = static_cast<int>(std::ceil(fps * max_preroll_seconds));
	const auto read_intervals = std::format("%+#{}", max_packets);

	auto c = u::run_command(
		blur.ffprobe_path,
		{
			"-v",
			"error",
			"-select_streams",
			"v:0",
			"-show_packets",
			"-read_intervals",
			read_intervals,
			"-show_entries",
			"packet=pts",
			"-of",
			"json",
			u::path_to_string(path),
		},
		bp::std_out > pipe_stream,
		bp::std_err.null()
	);

	std::string output(std::istreambuf_iterator<char>(pipe_stream), {});
	c.wait();

	const auto j = nlohmann::json::parse(output);

	int skip = 0;
	for (const auto& pkt : j.value("packets", nlohmann::json::array())) {
		const auto pts = pkt.value("pts", 0LL);
		if (pts >= 0)
			break;
		skip++;
	}

	return skip;
}

u::VideoInfo u::get_video_info(const std::filesystem::path& path) {
	namespace bp = boost::process;

	bp::ipstream pipe_stream;

	auto c = u::run_command(
		blur.ffprobe_path,
		{
			"-v",
			"error",
			"-show_entries",
			// clang-format off
			"stream=index,codec_type,sample_rate,color_range,r_frame_rate,pix_fmt,color_space,color_transfer,color_primaries,width,height,start_time",
			// clang-format on
			"-show_entries",
			"format=duration",
			"-of",
			"json",
			u::path_to_string(path),
		},
		bp::std_out > pipe_stream,
		bp::std_err.null()
	);

	std::string output(std::istreambuf_iterator<char>(pipe_stream), {});

	c.wait();

	DEBUG_LOG("[ffprobe] {}", output);

	const auto j = nlohmann::json::parse(output);

	VideoInfo info;

	auto opt_str = [](const nlohmann::json& stream, const std::string& key) -> std::optional<std::string> {
		if (!stream.contains(key))
			return std::nullopt;

		auto s = stream[key].get<std::string>();

		if (s.empty() || s == "unknown" || s == "reserved")
			return std::nullopt;

		return s;
	};

	// format
	if (j.contains("format")) {
		const auto& fmt = j["format"];

		if (fmt.contains("duration"))
			info.duration = std::stod(fmt["duration"].get<std::string>());
	}

	// streams
	bool first_video_stream = false;

	for (const auto& stream : j.value("streams", nlohmann::json::array())) {
		const auto codec_type = stream.value("codec_type", "");

		if (codec_type == "video") {
			info.has_video_stream = true;

			if (first_video_stream)
				continue;

			first_video_stream = true;

			info.width = stream.value("width", 0);
			info.height = stream.value("height", 0);
			info.pix_fmt = opt_str(stream, "pix_fmt");
			info.color_range = opt_str(stream, "color_range");
			info.color_space = opt_str(stream, "color_space");
			info.color_transfer = opt_str(stream, "color_transfer");
			info.color_primaries = opt_str(stream, "color_primaries");

			if (stream.contains("r_frame_rate")) {
				const auto fps = u::split_string(stream["r_frame_rate"].get<std::string>(), "/");
				info.fps_num = std::stoi(fps[0]);
				info.fps_den = std::stoi(fps[1]);
			}

			if (stream.contains("start_time"))
				info.video_start_time = std::stod(stream["start_time"].get<std::string>());
		}
		else if (codec_type == "audio") {
			if (stream.contains("sample_rate"))
				info.audio_sample_rates.push_back(std::stoi(stream["sample_rate"].get<std::string>()));

			if (stream.contains("start_time"))
				info.audio_start_times.push_back(std::stod(stream["start_time"].get<std::string>()));
		}
	}

	info.preroll_frames = get_video_preroll_frames(path, (double)info.fps_num / info.fps_den);

	return info;
}

int16_t u::get_audio_percentile_peak(const std::vector<int16_t>& samples, float percentile) {
	if (samples.empty())
		return 1;

	// sort samples from quietest->loudest
	std::vector<int16_t> abs_samples;
	abs_samples.reserve(samples.size());
	for (int16_t sample : samples) {
		abs_samples.push_back(std::abs(sample));
	}

	std::ranges::sort(abs_samples);

	// get xth percentile amplitude
	auto idx = static_cast<size_t>(percentile * abs_samples.size());
	idx = std::min(idx, abs_samples.size() - 1);
	return std::max(abs_samples[idx], static_cast<int16_t>(1));
}

bool u::test_hardware_device(const std::string& device_type) {
	namespace bp = boost::process;

	bp::ipstream error_stream;

	auto c = u::run_command(
		blur.ffmpeg_path,
		{
			"-init_hw_device",
			(device_type + "=hw"),
			"-loglevel",
			"error",
		},
		bp::std_out.null(),
		bp::std_err > error_stream
	);

	std::string line;
	if (std::getline(error_stream, line)) {
		// any error output means the device is not available
		c.terminate();
		return false;
	}

	c.wait();
	return true;
}

std::vector<u::EncodingDevice> u::get_hardware_encoding_devices() {
	static std::vector<EncodingDevice> devices;

	if (init_hw)
		return devices;
	else
		init_hw = true;

	struct HardwareTest {
		std::string type;
		std::string method;
		std::string ffmpeg_device_type;
	};

	std::vector<HardwareTest> tests = {
		// in order of priority
		// e.g. if you have nvidia + amd/intel you'll want to use nvidia over them i assume
		{ .type = "nvidia", .method = "nvenc", .ffmpeg_device_type = "cuda" },
		{ .type = "amd", .method = "amf", .ffmpeg_device_type = "d3d11va" },
		{ .type = "intel", .method = "qsv", .ffmpeg_device_type = "qsv" },
#ifdef __APPLE__
		{ .type = "mac", .method = "videotoolbox", .ffmpeg_device_type = "videotoolbox" }
#endif
	};

	std::vector<std::future<bool>> futures;
	futures.reserve(tests.size());

	for (const auto& test : tests) {
		futures.push_back(std::async(std::launch::async, [&test]() {
			return test_hardware_device(test.ffmpeg_device_type);
		}));
	}

	for (size_t i = 0; i < tests.size(); ++i) {
		if (futures[i].get()) {
			devices.emplace_back(
				EncodingDevice{
					.type = tests[i].type,
					.method = tests[i].method,
					.is_primary = devices.empty(),
				}
			);
		}
	}

	return devices;
}

std::vector<std::string> u::get_available_gpu_types() {
	auto devices = get_hardware_encoding_devices();
	std::vector<std::string> gpu_types;

	gpu_types.reserve(devices.size());
	for (const auto& device : devices) {
		gpu_types.push_back(device.type);
	}

	return gpu_types;
}

std::string u::get_primary_gpu_type() {
	auto devices = get_hardware_encoding_devices();

	for (const auto& device : devices) {
		if (device.is_primary) {
			return device.type;
		}
	}

	if (!devices.empty()) {
		return devices[0].type;
	}

	return "cpu";
}

bool u::test_codec(const std::string& codec) {
	namespace bp = boost::process;

	bp::ipstream error_stream;

	auto c = u::run_command(
		blur.ffmpeg_path,
		{
			"-loglevel",
			"error",
			"-f",
			"lavfi",
			"-i",
			"nullsrc",
			"-c:v",
			codec,
			"-frames:v",
			"1",
			"-f",
			"null",
			"-",
		},
		bp::std_out.null(),
		bp::std_err > error_stream
	);

	c.wait();

	return c.exit_code() == 0;
}

std::set<std::string> u::get_available_codecs(const std::set<std::string>& codecs) {
	static std::unordered_map<std::string, bool> codec_available_cache;

	std::set<std::string> result;
	std::vector<std::future<std::pair<std::string, bool>>> futures;

	for (const auto& codec : codecs) {
		if (codec_available_cache.contains(codec)) {
			if (codec_available_cache[codec])
				result.insert(codec);

			continue;
		}

		futures.push_back(std::async(std::launch::async, [&codec]() {
			bool available = test_codec(codec);
			return std::make_pair(codec, available);
		}));
	}

	for (auto& future : futures) {
		auto [codec, available] = future.get();

		codec_available_cache[codec] = available;

		if (available)
			result.insert(codec);
	}

	return result;
}

std::vector<std::string> u::get_supported_presets(bool gpu_encoding, const std::string& gpu_type) {
	if (!init_hw)
		get_hardware_encoding_devices();

	auto available_presets = config_presets::get_available_presets(gpu_encoding, gpu_type);

	std::set<std::string> all_codecs;
	for (const auto& preset : available_presets) {
		all_codecs.insert(preset.codec);
	}

	auto available_codecs = get_available_codecs(all_codecs);

	std::vector<std::string> filtered_presets;
	for (const auto& preset : available_presets) {
		if (available_codecs.contains(preset.codec))
			filtered_presets.push_back(preset.name);
	}

	return filtered_presets;
}

std::vector<std::string> u::ffmpeg_string_to_args(const std::string& str) {
	std::vector<std::string> args;

	bool in_quote = false;
	std::string current_arg;

	for (size_t i = 0; i < str.length(); i++) {
		char c = str[i];

		if (c == '"') {
			in_quote = !in_quote;
			// don't add the quote character to the argument
		}
		else if (c == ' ' && !in_quote) {
			if (!current_arg.empty()) {
				args.push_back(current_arg);
				current_arg.clear();
			}
		}
		else {
			current_arg += c;
		}
	}

	if (!current_arg.empty()) {
		args.push_back(current_arg);
	}

	return args;
}

std::map<int, std::string> u::get_devices(const std::string& type) {
	namespace bp = boost::process;

	auto env = setup_vspipe_environment();

#if defined(__linux__)
	bool vapoursynth_plugins_bundled = std::filesystem::exists(blur.resources_path / "vapoursynth-plugins");
#endif

	std::filesystem::path get_devices_script_path = (blur.resources_path / "lib/get_devices.py");

	bp::ipstream out_stream, err_stream;

	auto c = u::run_command(
		blur.vspipe_path,
		{
			"-c",
			"y4m",
			"-a",
			std::format("type={}", type),
#if defined(__APPLE__)
			"-a",
			std::format("macos_bundled={}", blur.used_installer ? "true" : "false"),
#endif
#if defined(__linux__)
			"-a",
			std::format("linux_bundled={}", vapoursynth_plugins_bundled ? "true" : "false"),
#endif
			u::path_to_string(get_devices_script_path),
			"-",
		},
		bp::std_out > out_stream,
		bp::std_err > err_stream,
		env
	);

	std::map<int, std::string> gpu_map;

	if (type == "rife") {
		std::regex gpu_line_pattern(R"(\[(\d+)\s+(.*?)\])");

		std::string line;
		while (err_stream && std::getline(err_stream, line)) {
			boost::algorithm::trim(line);

			std::smatch match;
			if (std::regex_search(line, match, gpu_line_pattern)) {
				gpu_map[std::stoi(match[1].str())] = match[2].str();
			}
		}
	}
	else if (type == "tensorrt") {
		std::string output(std::istreambuf_iterator<char>(out_stream), {});
		boost::algorithm::trim(output);

		if (!output.empty()) {
			try {
				auto json = nlohmann::json::parse(output);

				for (const auto& entry : json) {
					int device_id = entry.at("device_id").get<int>();
					const auto& props = entry.at("properties");

					std::string name = props.value("name", "Unknown Device");
					gpu_map[device_id] = name;
				}
			}
			catch (const nlohmann::json::exception& e) {
				// optionally log: e.what()
			}
		}
	}

	c.wait();

	return gpu_map;
}

int u::get_fastest_device_index(
	const std::map<int, std::string>& device_map,
	const std::filesystem::path& benchmark_video_path,
	const std::string& benchmark_type,
	const std::vector<std::string>& extra_args
) {
	namespace bp = boost::process;

	float fastest_time = FLT_MAX;
	int fastest_index = -1;

	std::filesystem::path benchmark_script_path = blur.resources_path / "lib/benchmarks.py";

#if defined(__linux__)
	bool vapoursynth_plugins_bundled = std::filesystem::exists(blur.resources_path / "vapoursynth-plugins");
#endif

	auto run_benchmark = [&](int device_index) {
		auto env = setup_vspipe_environment();

		std::vector<std::string> args = {
			"-c",
			"y4m",
			"-p",
			"-a",
			std::format("type={}", benchmark_type),
			"-a",
			std::format("device_index={}", device_index),
			"-a",
			std::format("benchmark_video_path={}", benchmark_video_path),
#if defined(__APPLE__)
			"-a",
			std::format("macos_bundled={}", blur.used_installer ? "true" : "false"),
#endif
#if defined(__linux__)
			"-a",
			std::format("linux_bundled={}", vapoursynth_plugins_bundled ? "true" : "false"),
#endif
#if defined(_WIN32)
			"-a",
			"enable_lsmash=true",
#endif
			"-e",
			"2",
		};

		for (const auto& extra_arg : extra_args) {
			args.push_back("-a");
			args.push_back(extra_arg);
		}

		args.push_back(u::path_to_string(benchmark_script_path));
		args.push_back("-");

		return u::run_command(blur.vspipe_path, args, env, bp::std_out.null(), bp::std_err.null());
	};

	if (benchmark_type == "rife (tensorrt)") {
		// need to warm up engine probably
		run_benchmark(device_map.begin()->first).wait();
	}

	for (const auto& [device_index, device_name] : device_map) {
		auto start = std::chrono::steady_clock::now();
		auto c = run_benchmark(device_index);

		bool killed_early = false;

		while (c.running()) {
			float elapsed_seconds =
				std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::steady_clock::now() - start)
					.count();

			if (elapsed_seconds > fastest_time) {
				c.terminate();
				killed_early = true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		if (!killed_early) {
			float elapsed_seconds =
				std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::steady_clock::now() - start)
					.count();
			u::log("device {} took {}", device_index, elapsed_seconds);

			if (elapsed_seconds < fastest_time) {
				fastest_time = elapsed_seconds;
				fastest_index = device_index;
			}
		}
		else {
			u::log("device {} killed early (too slow)", device_index);
		}
	}

	return fastest_index;
}

std::optional<size_t> u::get_fastest_rife_device(BlurSettings& settings) {
	auto app_config = config_app::get_app_config();
	if (app_config.rife_device_index != -1)
		return std::nullopt;

	if (!blur.initialised_devices || blur.rife_devices.empty())
		return std::nullopt;

	if (blur.rife_devices.size() == 1)
		return 0;

	auto sample_video_path = blur.settings_path / "sample_video.mp4";
	if (!std::filesystem::exists(sample_video_path))
		return std::nullopt;

	auto rife_model_path = settings.get_rife_model_path();
	if (!rife_model_path)
		return std::nullopt;

	return u::get_fastest_device_index(
		blur.rife_devices, sample_video_path, "rife", { std::format("rife_model_path={}", *rife_model_path) }
	);
}

#ifdef TENSORRT
std::optional<size_t> u::get_fastest_tensorrt_device(BlurSettings& settings) {
	auto app_config = config_app::get_app_config();
	if (app_config.tensorrt_device_index != -1)
		return std::nullopt;

	if (!blur.initialised_devices || blur.tensorrt_devices.empty())
		return std::nullopt;

	if (blur.tensorrt_devices.size() == 1)
		return 0;

	auto sample_video_path = blur.settings_path / "sample_video.mp4";
	if (!std::filesystem::exists(sample_video_path))
		return std::nullopt;

	auto rife_trt_model = settings.advanced.rife_trt_model;
	if (rife_trt_model.empty())
		return std::nullopt;

	return u::get_fastest_device_index(
		blur.tensorrt_devices,
		sample_video_path,
		"rife (tensorrt)",
		{ std::format("rife_trt_model={}", rife_trt_model) }
	);
}
#endif

void u::set_fastest_devices(BlurSettings& settings) {
	auto app_config = config_app::get_app_config();

	auto rife_result = u::get_fastest_rife_device(settings);

#ifdef TENSORRT
	auto tensorrt_result = u::get_fastest_tensorrt_device(settings);

	if (!rife_result && !tensorrt_result)
		return;
#else
	if (!rife_result)
		return;
#endif

	if (rife_result) {
		app_config.rife_device_index = *rife_result;
		u::log("set rife_device_index to the fastest device ({})", app_config.rife_device_index);
	}

#ifdef TENSORRT
	if (tensorrt_result) {
		app_config.tensorrt_device_index = *tensorrt_result;
		u::log("set tensorrt_device_index to the fastest device ({})", app_config.tensorrt_device_index);
	}
#endif

	// todo: this is dumb
	auto app_config_path = config_app::get_app_config_path();
	config_app::create(app_config_path, app_config);
}

void u::verify_gpu_encoding(BlurSettings& settings) {
	if (!blur.initialised)
		return;

	auto app_config = config_app::get_app_config();

	if (app_config.gpu_type.empty() || !u::contains(u::get_available_gpu_types(), app_config.gpu_type)) {
		app_config.gpu_type = u::get_primary_gpu_type();
	}

	if (app_config.gpu_type == "cpu") {
		settings.gpu_encoding = false;
	}

	auto available_codecs = u::get_supported_presets(settings.gpu_encoding, app_config.gpu_type);

	if (!u::contains(available_codecs, settings.encode_preset)) {
		settings.encode_preset = "h264";
	}

	// todo: this is dumb
	auto app_config_path = config_app::get_app_config_path();
	config_app::create(app_config_path, app_config);
}

#ifdef WIN32
bool u::windows_toggle_suspend_process(DWORD pid, bool to_suspend) {
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE)
		return false;

	THREADENTRY32 te;
	te.dwSize = sizeof(te);

	if (!Thread32First(hSnapshot, &te)) {
		CloseHandle(hSnapshot);
		return false;
	}

	do {
		if (te.th32OwnerProcessID == pid) {
			HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
			if (hThread) {
				if (to_suspend)
					SuspendThread(hThread);
				else
					ResumeThread(hThread);
				CloseHandle(hThread);
			}
		}
	}
	while (Thread32Next(hSnapshot, &te));

	CloseHandle(hSnapshot);
	return true;
}
#endif

tl::expected<u::ParsedError, std::string> u::parse_error_output(const std::string& stderr_output) {
	ParsedError result;
	result.is_blur_exception = false;

	size_t json_start = stderr_output.find('{');
	size_t json_end = stderr_output.rfind('}');

	if (json_start != std::string::npos && json_end != std::string::npos && json_end > json_start) {
		try {
			std::string json_str = stderr_output.substr(json_start, json_end - json_start + 1);
			auto json = nlohmann::json::parse(json_str);

			if (json.contains("error_type") && json["error_type"] == "BlurException") {
				result.is_blur_exception = true;
				result.user_message = json.value("user_message", "An error occurred during processing");
				result.technical_details = json.value("technical_details", stderr_output);
				return result;
			}
		}
		catch (const nlohmann::json::exception& e) {
			DEBUG_LOG("Failed to parse JSON error: {}", e.what());
		}
	}

	return tl::unexpected(stderr_output);
}
