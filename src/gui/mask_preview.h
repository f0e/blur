#pragma once

#include "blur_preview.h"

class MaskPreview {
public:
	struct Request {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members) only lives for the call it's made for
		const std::filesystem::path& video_path;
		const media::VideoInfo& video_info;
		const BlurSettings& settings;
		const GlobalAppSettings& app_settings;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
	};

	[[nodiscard]] static bool applies(const BlurSettings& settings);

	PreviewState update(const Request& request);

	std::optional<rendering::RenderError> take_error() {
		return m_preview.take_error();
	}

	void handle_event(const SDL_Event& event, bool& to_render) {
		m_preview.handle_event(event, to_render);
	}

	bool save(const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done) const {
		return m_preview.save_frame(path, std::move(on_done));
	}

private:
	BlurPreview m_preview;

	// a mask is worked out from the whole video, so it only needs reloading when the masking settings change
	std::optional<BlurSettings> m_settings;
};
