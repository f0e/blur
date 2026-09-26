#include "mask_preview.h"

bool MaskPreview::applies(const BlurSettings& settings) {
	return (settings.interpolate || settings.deduplicate) && (!settings.mask.empty() || settings.auto_mask);
}

PlayerBlurPreview::State MaskPreview::update(const Request& request) {
	if (!m_settings || !config_blur::same_masking(*m_settings, request.settings))
		m_settings = request.settings;

	if (!m_preview)
		m_preview = std::make_unique<BlurPreview>();

	m_preview->update(
		{
			.video_path = request.video_path,
			.video_info = request.video_info,
			.settings = *m_settings,
			.app_settings = request.app_settings,
			.mask = true,
		}
	);

	return {
		.overlay = m_preview->ready_player(),
		.status = m_preview->status(),
	};
}

std::optional<rendering::RenderError> MaskPreview::take_error() {
	return m_preview ? m_preview->take_error() : std::nullopt;
}

void MaskPreview::handle_event(const SDL_Event& event, bool& to_render) {
	if (m_preview)
		m_preview->handle_event(event, to_render);
}

bool MaskPreview::save(
	const std::filesystem::path& path, std::function<void(std::optional<std::string> error)> on_done
) const {
	return m_preview && m_preview->save_frame(path, std::move(on_done));
}
