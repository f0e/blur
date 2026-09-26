#include "mask_preview.h"

bool MaskPreview::applies(const BlurSettings& settings) {
	return (settings.interpolate || settings.deduplicate) && (!settings.mask.empty() || settings.auto_mask);
}

PlayerBlurPreview::State MaskPreview::update(const Request& request) {
	if (!m_settings || !config_blur::same_masking(*m_settings, request.settings))
		m_settings = request.settings;

	m_preview.update(
		{
			.video_path = request.video_path,
			.video_info = request.video_info,
			.settings = *m_settings,
			.app_settings = request.app_settings,
			.mask = true,
		}
	);

	return {
		.overlay = m_preview.ready_player(),
		.status = m_preview.status(),
	};
}
