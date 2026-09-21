#include "devices.h"
#include "rife_models.h"
#include "script_status.h"

namespace {
	std::map<int, std::string> list_devices(const std::string& type) {
		namespace bp = boost::process;

		bp::ipstream out_stream;
		bp::ipstream err_stream;

		auto c = u::run_command(
			blur.vspipe_path,
			u::get_vspipe_args({ "--info" }, "get_devices.py", { std::format("type={}", type) }, "--"),
			bp::std_out > out_stream,
			bp::std_err > err_stream,
			u::setup_vspipe_environment()
		);

		std::map<int, std::string> device_list;

		if (type == "rife") {
			static const std::regex device_pattern(R"(\[(\d+)\s+(.*?)\])");

			std::string line;
			while (std::getline(err_stream, line)) {
				std::smatch match;
				if (std::regex_search(line, match, device_pattern))
					device_list[std::stoi(match[1].str())] = match[2].str();
			}
		}
		else if (type == "tensorrt") {
			std::string output(std::istreambuf_iterator<char>(out_stream), {});

			// nothing's printed without the tensorrt plugin
			try {
				for (const auto& entry : output.empty() ? nlohmann::json::array() : nlohmann::json::parse(output)) {
					device_list[entry.at("device_id").get<int>()] =
						entry.at("properties").value("name", "Unknown Device");
				}
			}
			catch (const nlohmann::json::exception& e) {
				u::log_error("failed to list tensorrt devices: {}", e.what());
			}
		}

		c.wait();

		// devices are picked by name, so identical gpus need telling apart
		std::map<std::string, int> name_counts;
		for (auto& [index, name] : device_list) {
			int count = ++name_counts[name];
			if (count > 1)
				name = std::format("{} ({})", name, count);
		}

		return device_list;
	}

	std::optional<int> find_fastest_device(
		const std::map<int, std::string>& device_list, const std::string& type, const std::string& model_arg
	) {
		namespace bp = boost::process;

		devices::benchmarking = true;

		std::optional<float> fastest_time;
		std::optional<int> fastest_index;

		for (const auto& [index, name] : device_list) {
			std::vector<std::string> script_args = {
				std::format("type={}", type),
				std::format("device_index={}", index),
				std::format("settings_path={}", u::path_to_string(blur.settings_path)),
				model_arg,
			};

			if (fastest_time)
				script_args.push_back(std::format("max_time={}", *fastest_time));

			bp::ipstream err_stream;
			auto c = u::run_command(
				blur.vspipe_path,
				u::get_vspipe_args({ "--info" }, "benchmarks.py", script_args, "--"),
				bp::std_out.null(),
				bp::std_err > err_stream,
				u::setup_vspipe_environment()
			);

			auto statuses = script_status::read_all(err_stream);

			c.wait();

			if (c.exit_code() != 0) {
				u::log("{} device {} failed the benchmark (exit code {})", type, index, c.exit_code());
				continue;
			}

			// no time means it gave up once it was slower than the fastest so far
			auto time_status = statuses.find("benchmark-time");
			if (time_status == statuses.end()) {
				u::log("{} device {} is slower", type, index);
				continue;
			}

			float time = std::stof(time_status->second);
			u::log("{} device {} took {}s", type, index, time);

			fastest_time = time;
			fastest_index = index;
		}

		devices::benchmarking = false;

		return fastest_index;
	}

	// gpu speed doesn't depend on the model
	std::optional<std::string> get_benchmark_model(
		const std::vector<std::string>& models, const std::string& preferred
	) {
		if (u::contains(models, preferred))
			return preferred;

		if (models.empty())
			return {};

		return models.front();
	}

	std::optional<int> find_fastest_rife_device() {
		auto model = get_benchmark_model(rife_models::list(), config_blur::DEFAULT_CONFIG.rife_model);
		if (!model)
			return {};

		return find_fastest_device(
			devices::rife,
			"rife",
			std::format("rife_model_path={}", u::path_to_string(rife_models::get_path() / *model))
		);
	}

#ifdef TENSORRT
	std::optional<int> find_fastest_tensorrt_device() {
		auto model = get_benchmark_model(rife_models::list_trt(), config_blur::DEFAULT_CONFIG.rife_trt_model);
		if (!model)
			return {};

		return find_fastest_device(
			devices::tensorrt,
			"rife (tensorrt)",
			std::format("rife_trt_model_path={}", u::path_to_string(rife_models::get_trt_path() / (*model + ".onnx")))
		);
	}
#endif

	struct AutoDevice {
		std::string type;
		const std::map<int, std::string>& devices;
		std::optional<int> (*find_fastest)();

		std::atomic<int> device_index = -1;
		std::atomic<bool> picked = false;
	};

	AutoDevice rife_auto{
		.type = "rife",
		.devices = devices::rife,
		.find_fastest = find_fastest_rife_device,
	};
#ifdef TENSORRT
	AutoDevice tensorrt_auto{
		.type = "tensorrt",
		.devices = devices::tensorrt,
		.find_fastest = find_fastest_tensorrt_device,
	};
#endif

	nlohmann::json devices_to_json(const std::map<int, std::string>& devices) {
		auto json = nlohmann::json::object();
		for (const auto& [index, name] : devices)
			json[std::to_string(index)] = name;

		return json;
	}

	// only trusted if it was benchmarked against the devices there are now
	std::optional<int> get_cached_fastest_device(const nlohmann::json& cache, const AutoDevice& auto_device) {
		try {
			const auto& entry = cache.at(auto_device.type);
			if (entry.at("devices") != devices_to_json(auto_device.devices)) {
				// devices changed, we need to re-benchmark
				return {};
			}

			return entry.at("fastest_device").get<int>();
		}
		catch (const nlohmann::json::exception&) {
			return {};
		}
	}

	bool pick_auto_device(AutoDevice& auto_device, nlohmann::json& cache) {
		bool cache_changed = false;

		const auto& devices = auto_device.devices;

		if (devices.size() == 1) {
			auto_device.device_index = devices.begin()->first;
		}
		else if (devices.size() > 1) {
			if (auto cached = get_cached_fastest_device(cache, auto_device)) {
				auto_device.device_index = *cached;
			}
			else if (auto fastest = auto_device.find_fastest()) {
				auto_device.device_index = *fastest;

				cache[auto_device.type] = {
					{ "fastest_device", *fastest },
					{ "devices", devices_to_json(devices) },
				};
				cache_changed = true;

				u::log("picked {} device {} as the fastest", auto_device.type, *fastest);
			}
		}

		auto_device.picked = true;
		auto_device.picked.notify_all();

		return cache_changed;
	}

	std::optional<std::string> get_auto_device_name(const AutoDevice& auto_device) {
		if (!auto_device.picked)
			return {};

		auto it = auto_device.devices.find(auto_device.device_index);
		if (it == auto_device.devices.end())
			return {};

		return it->second;
	}

	int get_device_index(const AutoDevice& auto_device, const std::string& setting) {
		// the device lists are ready by the time it's picked too
		auto_device.picked.wait(false);

		if (setting != "auto") {
			for (const auto& [index, name] : auto_device.devices) {
				if (name == setting)
					return index;
			}

			u::log("{} device '{}' isn't available, using auto", auto_device.type, setting);
		}

		return auto_device.device_index;
	}
}

void devices::initialise() {
	rife = list_devices("rife");
#ifdef TENSORRT
	tensorrt = list_devices("tensorrt");
#endif

	initialised = true;

	auto cache_path = blur.settings_path / "device-benchmark.json";

	auto cache = nlohmann::json::parse(std::ifstream(cache_path), nullptr, false);
	if (!cache.is_object())
		cache = nlohmann::json::object();

	bool cache_changed = pick_auto_device(rife_auto, cache);
#ifdef TENSORRT
	cache_changed |= pick_auto_device(tensorrt_auto, cache);
#endif

	if (cache_changed) {
		// write new json
		std::ofstream(cache_path) << cache.dump(1, '\t');
	}
}

std::optional<std::string> devices::get_auto_rife_device() {
	return get_auto_device_name(rife_auto);
}

std::optional<std::string> devices::get_auto_tensorrt_device() {
#ifdef TENSORRT
	return get_auto_device_name(tensorrt_auto);
#else
	return {};
#endif
}

devices::DeviceIndices devices::get_device_indices(
	const BlurSettings& settings, const GlobalAppSettings& app_settings
) {
	DeviceIndices indices;

	if (settings.uses_interpolation_method("rife"))
		indices.rife = get_device_index(rife_auto, app_settings.rife_device);

#ifdef TENSORRT
	if (settings.uses_interpolation_method("rife (tensorrt)"))
		indices.tensorrt = get_device_index(tensorrt_auto, app_settings.tensorrt_device);
#endif

	return indices;
}
