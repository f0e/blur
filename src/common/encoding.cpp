#include "encoding.h"
#include "config_encoding_presets.h"
#include "config_app.h"

namespace {
	std::atomic<bool> encoding_probe_done = false;

	struct EncodingDevice {
		std::string type;   // "nvidia", "amd", "intel", "mac"
		std::string method; // Specific encoding method (e.g., "nvenc", "amf", "qsv", "videotoolbox")
		bool is_primary;    // Whether this is likely the primary GPU
	};

	bool test_hardware_device(const std::string& device_type) {
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

		if (!c) {
			u::log_error("failed to test hardware device {}: {}", device_type, c.error());
			return false;
		}

		std::string line;
		if (std::getline(error_stream, line)) {
			// any error output means the device is not available
			u::safe_terminate(*c);
			return false;
		}

		c->wait();
		return true;
	}

	std::vector<EncodingDevice> get_hardware_encoding_devices() {
		// static init is thread safe, so anyone calling mid-probe waits for it rather than probing again
		static const std::vector<EncodingDevice> devices = [] {
			std::vector<EncodingDevice> devices;

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
		}();

		return devices;
	}

	bool test_codec(const std::string& codec) {
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

		if (!c) {
			u::log_error("failed to test codec {}: {}", codec, c.error());
			return false;
		}

		c->wait();

		return c->exit_code() == 0;
	}

	std::set<std::string> get_available_codecs(const std::set<std::string>& codecs) {
		static std::unordered_map<std::string, bool> codec_available_cache;
		static std::mutex codec_mutex; // held while probing, so a codec being probed isn't probed again

		std::lock_guard lock(codec_mutex);

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
}

std::vector<std::string> encoding::get_available_gpu_types() {
	auto devices = get_hardware_encoding_devices();
	std::vector<std::string> gpu_types;

	gpu_types.reserve(devices.size());
	for (const auto& device : devices) {
		gpu_types.push_back(device.type);
	}

	return gpu_types;
}

std::string encoding::get_primary_gpu_type() {
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

std::vector<std::string> encoding::get_supported_encoding_presets(bool gpu_encoding, const std::string& gpu_type) {
	return get_supported_encoding_presets(config_encoding_presets::get_config(), gpu_encoding, gpu_type);
}

std::vector<std::string> encoding::get_supported_encoding_presets(
	const EncodingPresetSettings& presets, bool gpu_encoding, const std::string& gpu_type
) {
	get_hardware_encoding_devices();

	auto available_presets = config_encoding_presets::get_available_presets(presets, gpu_encoding, gpu_type);

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

void encoding::probe_support() {
	auto presets = config_encoding_presets::get_config();

	get_supported_encoding_presets(presets, false, "cpu");

	for (const auto& gpu_type : get_available_gpu_types())
		get_supported_encoding_presets(presets, true, gpu_type);

	encoding_probe_done = true;
}

bool encoding::support_probed() {
	return encoding_probe_done;
}

std::vector<std::string> encoding::ffmpeg_string_to_args(const std::string& str) {
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

void encoding::verify_gpu_encoding(BlurSettings& settings) {
	if (!blur.initialised)
		return;

	auto app_config = config_app::get_app_config();

	if (app_config.gpu_type.empty() || !u::contains(get_available_gpu_types(), app_config.gpu_type)) {
		app_config.gpu_type = get_primary_gpu_type();
	}

	if (app_config.gpu_type == "cpu") {
		settings.gpu_encoding = false;
	}

	auto available_codecs = get_supported_encoding_presets(settings.gpu_encoding, app_config.gpu_type);

	if (!u::contains(available_codecs, settings.encode_preset)) {
		settings.encode_preset = "h264";
	}

	// todo: this is dumb
	auto app_config_path = config_app::get_app_config_path();
	config_app::create(app_config_path, app_config);
}
