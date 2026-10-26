#include "helpers.h"

#include "common/config_app.h"

namespace {
	// everything off its default, so a setting the writer or the parser misses shows up. new settings go here too
	GlobalAppSettings changed_settings() {
		GlobalAppSettings settings;
		settings.output_prefix = "blurred";
		settings.gpu_type = "amd";
		settings.rife_device = "1";
		settings.lossless_scaling_path = "some/folder";
		settings.gui_width = 1000;
		settings.gui_height = 700;
		settings.blur_amount_tied_to_fps = false;
		settings.dpi_scale_override = 1.5f;
		settings.preview_volume = 30;
		settings.preview_hardware_decoding = !settings.preview_hardware_decoding;
		settings.pre_render_output_encoding = false;
		settings.sample_video_path = "D:/clips/sample.mp4";
		settings.config_preview_seek = 0.25f;
		settings.render_success_notifications = true;
		settings.render_failure_notifications = true;
		settings.taskbar_progress = false;
		settings.check_updates = false;
		settings.check_beta = true;
		settings.dismissed_update_version = "3.1.0";
		settings.skip_queue = true;
		settings.fully_blur_first_frame = false;
		return settings;
	}
}

TEST(ConfigApp, RoundTripsThroughItsFileFormat) {
	auto settings = changed_settings();

	EXPECT_EQ(config_app::parse(config_app::generate_config_string(settings, false)), settings);
}

TEST(ConfigApp, SharingLeavesOutThisMachinesSettings) {
	auto settings = changed_settings();

	auto shared = config_app::parse(config_app::export_shareable(settings));

	EXPECT_EQ(shared.output_prefix, settings.output_prefix);
	EXPECT_EQ(shared.preview_volume, settings.preview_volume);

	const GlobalAppSettings defaults;
	EXPECT_EQ(shared.gpu_type, defaults.gpu_type);
	EXPECT_EQ(shared.rife_device, defaults.rife_device);
	EXPECT_EQ(shared.lossless_scaling_path, defaults.lossless_scaling_path);
	EXPECT_EQ(shared.sample_video_path, defaults.sample_video_path);
	EXPECT_EQ(shared.config_preview_seek, defaults.config_preview_seek);
	EXPECT_EQ(shared.dismissed_update_version, defaults.dismissed_update_version);
}

TEST(ConfigApp, ImportingKeepsThisMachinesSettings) {
	auto current = changed_settings();

	GlobalAppSettings imported;
	config_app::copy_machine_settings(imported, current);

	EXPECT_EQ(imported.gpu_type, current.gpu_type);
	EXPECT_EQ(imported.sample_video_path, current.sample_video_path);
	EXPECT_EQ(imported.lossless_scaling_path, current.lossless_scaling_path);
	EXPECT_EQ(imported.dismissed_update_version, current.dismissed_update_version);
	EXPECT_EQ(imported.output_prefix, GlobalAppSettings{}.output_prefix);
}

TEST(ConfigApp, ClampsThePreviewSeek) {
	EXPECT_EQ(config_app::parse(std::string("config preview seek: 2\n")).config_preview_seek, 1.f);
	EXPECT_EQ(config_app::parse(std::string("config preview seek: -1\n")).config_preview_seek, 0.f);
}
