#pragma once

namespace updates {
	struct UpdateCheckRes {
		bool is_latest = true; // assumption for fails
		std::string latest_tag;
		std::string latest_tag_url;
	};

	tl::expected<UpdateCheckRes, std::string> is_latest_version(bool include_beta = false);

	// platforms without an installer can only send people to the release page
	constexpr bool can_self_update() {
#if defined(_WIN32) || defined(__APPLE__)
		return true;
#else
		return false;
#endif
	}

	using ProgressCallback = std::function<void(const std::string& text, float progress, bool done)>;
	using CancelCallback = std::function<bool()>; // return true to abort the download

	bool update_to_tag(
		const std::string& tag,
		const std::optional<ProgressCallback>& progress_callback = {},
		const std::optional<CancelCallback>& cancel_callback = {}
	);
	bool update_to_latest(
		bool include_beta = false,
		const std::optional<ProgressCallback>& progress_callback = {},
		const std::optional<CancelCallback>& cancel_callback = {}
	);
}
