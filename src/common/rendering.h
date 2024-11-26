#pragma once

#include "config.h"

#include <vector>
#include <thread>
#include <filesystem>

struct RenderStatus {
	bool finished = false;
	bool init = false;
	int current_frame;
	int total_frames;
	std::chrono::steady_clock::time_point start_time;
	std::chrono::duration<double> elapsed_time;
	float fps;

	void update_progress_string(bool first);
	std::string progress_string;
};

class Render {
private:
	RenderStatus status;

	std::wstring video_name;

	std::filesystem::path video_path;
	std::filesystem::path video_folder;

	std::filesystem::path output_path;
	std::filesystem::path temp_path;
	std::filesystem::path preview_path;

	BlurSettings settings;

	void build_output_filename();

	struct RenderCommands {
		std::wstring vspipe_path;
		std::vector<std::wstring> vspipe;

		std::wstring ffmpeg_path;
		std::vector<std::wstring> ffmpeg;
	};

	RenderCommands build_render_commands();

	bool do_render(RenderCommands render_commands);

public:
	Render(
		const std::filesystem::path& input_path,
		std::optional<std::filesystem::path> output_path = {},
		std::optional<std::filesystem::path> config_path = {}
	);

	bool create_temp_path();
	bool remove_temp_path();

	void render();

	[[nodiscard]] std::wstring get_video_name() const {
		return video_name;
	}

	[[nodiscard]] std::filesystem::path get_output_video_path() const {
		return output_path;
	}

	[[nodiscard]] BlurSettings get_settings() const {
		return settings;
	}

	[[nodiscard]] RenderStatus get_status() const {
		return status;
	}

	[[nodiscard]] std::filesystem::path get_preview_path() const {
		return preview_path;
	}
};

class Rendering {
private:
	std::unique_ptr<std::thread> thread_ptr;

public:
	std::vector<std::shared_ptr<Render>> queue;
	std::shared_ptr<Render> current_render;
	std::optional<std::function<void()>> progress_callback;
	bool renders_queued;

	void render_videos();

	void queue_render(std::shared_ptr<Render> render);

	void stop_rendering();

	void run_callbacks() {
		if (progress_callback)
			(*progress_callback)();
	}
};

inline Rendering rendering;
