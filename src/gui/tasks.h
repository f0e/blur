#pragma once

namespace tasks {
	inline int finished_renders = 0;

	struct PendingVideo {
		size_t video_id;
		std::filesystem::path video_path;
		std::optional<u::VideoInfo> video_info;
		float start = 0.f;
		float end = 1.f;
		bool start_immediately = false;

		// which blur config this video renders with. seeded from whatever it resolves to when it was added,
		// then overridable per video from the queue screen
		std::string config_name;

		// set when a render was attempted while this video had no config, so the queue screen can warn
		// under the dropdown until one gets picked
		bool config_missing_warning = false;

		// how config_name was arrived at, so the queue can say whether it came from a rule or the default
		config_blur::ConfigSource config_source = config_blur::ConfigSource::NONE;
		std::string config_rule_pattern;

		// the masks this video renders with: a filename in the masks folder (empty for none), and whether one
		// is also worked out from the video itself and applied over it. both are seeded from the config above,
		// then overridable per video from the queue screen
		std::string mask;
		bool auto_mask = false;
	};

	void run(const std::vector<std::string>& arguments);

	void add_files(const std::vector<std::filesystem::path>& path_strs);
	void add_sample_video(const std::filesystem::path& path_str);
	void process_pending_files();

	void cancel_all_pending();
	void cancel_pending(size_t video_id);
	void start_pending_videos();

	std::vector<std::shared_ptr<tasks::PendingVideo>> get_pending_copy();
}
