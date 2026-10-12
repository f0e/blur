#include "render_optimise.h"
#include "render_pipeline.h"
#include "common/devices.h"

namespace {
	// gpu decoding is left to the user until it's stable. turning this on makes it one of the picked settings
	constexpr bool OPTIMISE_GPU_DECODING = false;

	// timing starts once frames come out, so script startup and engine builds aren't counted, but the first frames
	// still include the pipeline filling up
	constexpr auto WARMUP = std::chrono::seconds(2);
	constexpr auto MEASURE = std::chrono::seconds(6);

	constexpr std::string_view CACHE_FILENAME = "optimised-gpu-settings.json";

	using rendering::optimise::GpuSettings;

	bool enabled(const BlurSettings& settings) {
		return settings.optimise_gpu_settings && settings.uses_interpolation_method("svp") &&
		       !settings.advanced.manual_svp;
	}

	// without one of these svp has the gpu to itself, and all gpu always won
	bool shares_gpu(const BlurSettings& settings) {
		return settings.uses_interpolation_method("rife") || settings.uses_interpolation_method("rife (tensorrt)") ||
		       settings.uses_interpolation_method("gimm-vfi (tensorrt)");
	}

	// the first is the default, everything on the gpu that's allowed to be
	// gpu_rendering_supported is passed in so the ui can avoid waiting on the startup test
	std::vector<GpuSettings> get_candidates(
		const BlurSettings& settings, [[maybe_unused]] bool gpu_rendering_supported
	) {
#ifdef __APPLE__
		// there's no cpu svp on macos
		std::vector<bool> renderings = { true };
#else
		std::vector<bool> renderings = gpu_rendering_supported ? std::vector{ true, false } : std::vector{ false };
#endif
		std::vector<bool> searches =
			settings.optimise_prioritise_speed ? std::vector{ true, false } : std::vector{ false };
		std::vector<bool> decodings =
			OPTIMISE_GPU_DECODING ? std::vector{ false, true } : std::vector{ settings.gpu_decoding };

		std::vector<GpuSettings> candidates;
		for (bool search : searches) {
			for (bool rendering : renderings) {
				// gpu search with cpu rendering never won in testing
				if (search && !rendering && renderings.size() > 1)
					continue;

				for (bool decoding : decodings) {
					candidates.push_back(
						{
							.svp_gpu_search = search,
							.svp_gpu_rendering = rendering,
							.gpu_decoding = decoding,
						}
					);
				}
			}
		}

		return candidates;
	}

	BlurSettings apply(BlurSettings settings, const GpuSettings& gpu_settings) {
		settings.svp_gpu_search = gpu_settings.svp_gpu_search;
		settings.svp_gpu_rendering = gpu_settings.svp_gpu_rendering;
		settings.gpu_decoding = gpu_settings.gpu_decoding;
		return settings;
	}

	nlohmann::json to_json(const GpuSettings& gpu_settings) {
		return {
			{ "svp_gpu_search", gpu_settings.svp_gpu_search },
			{ "svp_gpu_rendering", gpu_settings.svp_gpu_rendering },
			{ "gpu_decoding", gpu_settings.gpu_decoding },
		};
	}

	GpuSettings from_json(const nlohmann::json& json) {
		return {
			.svp_gpu_search = json.at("svp_gpu_search").get<bool>(),
			.svp_gpu_rendering = json.at("svp_gpu_rendering").get<bool>(),
			.gpu_decoding = json.at("gpu_decoding").get<bool>(),
		};
	}

	std::string describe(const GpuSettings& gpu_settings) {
		std::string description = std::format(
			"svp gpu search {}, svp gpu rendering {}",
			gpu_settings.svp_gpu_search ? "on" : "off",
			gpu_settings.svp_gpu_rendering ? "on" : "off"
		);

		if (OPTIMISE_GPU_DECODING)
			description += std::format(", gpu decoding {}", gpu_settings.gpu_decoding ? "on" : "off");

		return description;
	}

	// everything that changes how much work the gpu and cpu each get
	nlohmann::json settings_key(const BlurSettings& settings, const GlobalAppSettings& app_settings) {
		nlohmann::json key = {
			{ "interpolate", settings.interpolate },
			{ "interpolated_fps", settings.interpolated_fps },
			{ "interpolation_method", settings.interpolation_method },
			{ "pre_interpolate", settings.pre_interpolate },
			{ "pre_interpolated_minimum_fps", settings.pre_interpolated_minimum_fps },
			{ "pre_interpolated_minimum_multiplier", settings.pre_interpolated_minimum_multiplier },
			{ "pre_interpolation_method", settings.pre_interpolation_method },
			{ "deduplicate", settings.deduplicate },
			{ "frame_timing_logs", settings.frame_timing_logs },
			{ "retime_method", settings.retime_method },
			{ "timescale", settings.timescale },
			{ "input_timescale", settings.input_timescale },
			{ "output_timescale", settings.output_timescale },
			{ "blur_output_fps", settings.blur_output_fps },
			{ "rife_model", settings.rife_model },
			{ "rife_trt_model", settings.rife_trt_model },
			{ "rife_trt_ensemble", settings.rife_trt_ensemble },
			{ "gimm_trt_model", settings.gimm_trt_model },
			{ "svp_interpolation_preset", settings.advanced.svp_interpolation_preset },
			{ "svp_interpolation_algorithm", settings.advanced.svp_interpolation_algorithm },
			{ "interpolation_blocksize", settings.advanced.interpolation_blocksize },
			{ "interpolation_mask_area", settings.advanced.interpolation_mask_area },
			{ "svp_search_size", settings.advanced.svp_search_size },
			// a cpu encoder competes with cpu svp
			{ "encode_preset", settings.encode_preset },
			{ "gpu_encoding", settings.gpu_encoding },
			{ "gpu_type", app_settings.gpu_type },
			{ "prioritise_speed", settings.optimise_prioritise_speed },
		};

		if (!OPTIMISE_GPU_DECODING)
			key["gpu_decoding"] = settings.gpu_decoding;

		return key;
	}

	nlohmann::json hardware_key(const GlobalAppSettings& app_settings) {
		auto device_indices = devices::get_device_indices(app_settings);

		return {
			// includes driver versions, which can change the speed too
			{ "hardware", devices::get_hardware() },
			{ "rife_device", device_indices.rife },
			{ "tensorrt_device", device_indices.tensorrt },
		};
	}

	nlohmann::json video_key(const media::VideoInfo& video_info) {
		long fps =
			video_info.fps_den > 0 ? std::lround(static_cast<double>(video_info.fps_num) / video_info.fps_den) : -1;

		// pixel format covers chroma subsampling and bit depth
		return {
			{ "width", video_info.width },
			{ "height", video_info.height },
			{ "fps", fps },
			{ "pix_fmt", video_info.pix_fmt.value_or("") },
		};
	}

	std::string describe_video(const nlohmann::json& video) {
		try {
			return std::format(
				"{}x{} {}fps {}",
				video.at("width").get<int>(),
				video.at("height").get<int>(),
				video.at("fps").get<long>(),
				video.at("pix_fmt").get<std::string>()
			);
		}
		catch (const nlohmann::json::exception&) {
			return "unknown video";
		}
	}

	std::mutex cache_mutex;
	std::optional<nlohmann::json> cache;

	std::filesystem::path cache_path() {
		return blur.settings_path / CACHE_FILENAME;
	}

	// a list of picks. lock cache_mutex first
	nlohmann::json& get_cache() {
		if (!cache) {
			cache = nlohmann::json::parse(std::ifstream(cache_path()), nullptr, false);
			if (!cache->is_array())
				cache = nlohmann::json::array();
		}

		return *cache;
	}

	void save_cache() {
		std::ofstream(cache_path()) << get_cache().dump(1, '\t');
	}

	std::optional<GpuSettings> find_saved(
		const nlohmann::json& hardware, const nlohmann::json& settings, const nlohmann::json& video
	) {
		std::lock_guard lock(cache_mutex);

		for (const auto& entry : get_cache()) {
			try {
				if (entry.at("hardware") == hardware && entry.at("settings") == settings && entry.at("video") == video)
					return from_json(entry.at("pick"));
			}
			catch (const nlohmann::json::exception&) {
				continue;
			}
		}

		return {};
	}

	void save(
		const nlohmann::json& hardware,
		const nlohmann::json& settings,
		const nlohmann::json& video,
		const GpuSettings& pick,
		const std::vector<std::pair<GpuSettings, float>>& results
	) {
		auto fps = nlohmann::json::array();
		for (const auto& [gpu_settings, result_fps] : results) {
			auto result = to_json(gpu_settings);
			result["fps"] = result_fps;
			fps.push_back(result);
		}

		std::lock_guard lock(cache_mutex);

		auto& entries = get_cache();

		std::erase_if(entries.get_ref<nlohmann::json::array_t&>(), [&](const nlohmann::json& entry) {
			return entry.value("hardware", nlohmann::json()) == hardware &&
			       entry.value("settings", nlohmann::json()) == settings &&
			       entry.value("video", nlohmann::json()) == video;
		});

		entries.push_back(
			{
				{ "hardware", hardware },
				{ "settings", settings },
				{ "video", video },
				{ "pick", to_json(pick) },
				{ "fps", fps },
			}
		);

		save_cache();
	}

	struct Measurement {
		enum class Outcome : uint8_t {
			MEASURED,
			TOO_SHORT,
			FAILED,
			STOPPED,
			PAUSED,
		};

		Outcome outcome = Outcome::FAILED;
		float fps = 0.f;
	};

	Measurement measure(
		const RenderCommands& commands, const std::shared_ptr<rendering::RenderState>& render_state, bool debug
	) {
		using clock = std::chrono::steady_clock;

		auto state = std::make_shared<rendering::RenderState>();
		state->forward_init_stages_to(render_state);

		std::optional<clock::time_point> first_frame_time;
		std::optional<clock::time_point> start_time;
		int start_frame = 0;
		std::optional<float> fps;

		auto on_progress = [&] {
			// a frame means any engine build is done
			render_state->set_init_stage(rendering::RenderState::InitStage::OPTIMISING);

			auto now = clock::now();
			int frame = state->get_progress().current_frame;

			if (!first_frame_time) {
				first_frame_time = now;
				return;
			}

			if (!start_time) {
				if (now - *first_frame_time >= WARMUP) {
					start_time = now;
					start_frame = frame;
				}
				return;
			}

			if (!fps && now - *start_time >= MEASURE) {
				fps = (frame - start_frame) / std::chrono::duration<float>(now - *start_time).count();
				state->stop();
			}
		};

		// the render being stopped or paused has to reach this run before it's made any frames too, e.g. while an
		// engine builds
		std::atomic<bool> done = false;
		std::thread watcher([&] {
			while (!done) {
				if (render_state->wants_stop() || render_state->wants_pause()) {
					state->stop();
					return;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		});

		auto result = rendering::detail::execute_pipeline(commands, state, debug, true, on_progress);

		done = true;
		watcher.join();

		if (render_state->wants_stop())
			return { .outcome = Measurement::Outcome::STOPPED };

		if (render_state->wants_pause())
			return { .outcome = Measurement::Outcome::PAUSED };

		if (fps)
			return { .outcome = Measurement::Outcome::MEASURED, .fps = *fps };

		if (!result) {
			u::log_error("gpu settings benchmark failed: {}", result.error().user_message);
			return { .outcome = Measurement::Outcome::FAILED };
		}

		return { .outcome = Measurement::Outcome::TOO_SHORT };
	}

	BlurSettings skip(
		const BlurSettings& settings,
		const GpuSettings& default_settings,
		const std::shared_ptr<rendering::RenderState>& state,
		const std::string& reason
	) {
		auto note = std::format("GPU settings weren't optimised because {}", reason);
		u::log(note);
		state->set_note(note);

		return apply(settings, default_settings);
	}
}

BlurSettings rendering::optimise::for_render(
	const BlurSettings& settings,
	const GlobalAppSettings& app_settings,
	const media::VideoInfo& video_info,
	const std::shared_ptr<RenderState>& state,
	const std::string& output_extension,
	const BuildCommands& build_commands
) {
	if (!enabled(settings))
		return settings;

	auto candidates = get_candidates(settings, devices::wait_svp_gpu_supported());
	if (!shares_gpu(settings) || candidates.size() == 1)
		return apply(settings, candidates.front());

	auto hardware = hardware_key(app_settings);
	auto settings_json = settings_key(settings, app_settings);
	auto video = video_key(video_info);

	if (auto saved = find_saved(hardware, settings_json, video))
		return apply(settings, *saved);

	// the timings would be measuring the other render as much as this one
	if (detail::running_pipelines > 0)
		return skip(settings, candidates.front(), state, "blur was busy with another render");

	state->set_init_stage(RenderState::InitStage::OPTIMISING);

	auto output_path = std::filesystem::temp_directory_path() / ("blur-optimise" + output_extension);

	std::vector<std::pair<GpuSettings, float>> results;

	for (const auto& candidate : candidates) {
		auto commands = build_commands(apply(settings, candidate), output_path);
		if (!commands) {
			u::log_error("failed to build gpu settings benchmark: {}", commands.error());
			continue;
		}

		auto measurement = measure(*commands, state, settings.advanced.debug);

		std::error_code ec;
		std::filesystem::remove(output_path, ec);

		switch (measurement.outcome) {
			case Measurement::Outcome::MEASURED:
				u::log("{}: {:.2f} fps", describe(candidate), measurement.fps);
				results.emplace_back(candidate, measurement.fps);
				break;
			case Measurement::Outcome::FAILED:
				break;
			case Measurement::Outcome::STOPPED:
				return apply(settings, candidates.front());
			case Measurement::Outcome::PAUSED:
				state->set_init_stage(RenderState::InitStage::NONE);
				return skip(settings, candidates.front(), state, "the render was paused while they were tested");
			case Measurement::Outcome::TOO_SHORT:
				state->set_init_stage(RenderState::InitStage::NONE);
				return skip(settings, candidates.front(), state, "the video is too short to test them");
		}
	}

	state->set_init_stage(RenderState::InitStage::NONE);

	if (results.empty())
		return skip(settings, candidates.front(), state, "none of them could be tested");

	auto best = std::ranges::max_element(results, {}, &std::pair<GpuSettings, float>::second)->first;

	save(hardware, settings_json, video, best, results);

	u::log("optimised gpu settings for {}: {}", describe_video(video), describe(best));

	return apply(settings, best);
}

BlurSettings rendering::optimise::for_preview(
	const BlurSettings& settings, const GlobalAppSettings& app_settings, const media::VideoInfo& video_info
) {
	if (!enabled(settings))
		return settings;

	auto candidates = get_candidates(settings, devices::wait_svp_gpu_supported());
	if (!shares_gpu(settings) || candidates.size() == 1)
		return apply(settings, candidates.front());

	auto saved = find_saved(hardware_key(app_settings), settings_key(settings, app_settings), video_key(video_info));

	return apply(settings, saved.value_or(candidates.front()));
}

std::vector<rendering::optimise::SavedPick> rendering::optimise::list_saved(
	const BlurSettings& settings, const GlobalAppSettings& app_settings
) {
	auto settings_json = settings_key(settings, app_settings);

	std::vector<SavedPick> picks;

	std::lock_guard lock(cache_mutex);

	for (const auto& entry : get_cache()) {
		try {
			if (entry.at("settings") != settings_json)
				continue;

			picks.push_back(
				{
					.video = describe_video(entry.at("video")),
					.settings = from_json(entry.at("pick")),
				}
			);
		}
		catch (const nlohmann::json::exception&) {
			continue;
		}
	}

	return picks;
}

void rendering::optimise::clear_saved(const BlurSettings& settings, const GlobalAppSettings& app_settings) {
	auto settings_json = settings_key(settings, app_settings);

	std::lock_guard lock(cache_mutex);

	std::erase_if(get_cache().get_ref<nlohmann::json::array_t&>(), [&](const nlohmann::json& entry) {
		return entry.value("settings", nlohmann::json()) == settings_json;
	});

	save_cache();
}

bool rendering::optimise::benchmarks(const BlurSettings& settings) {
	return enabled(settings) && shares_gpu(settings) &&
	       get_candidates(settings, devices::get_svp_gpu_supported().value_or(true)).size() > 1;
}
