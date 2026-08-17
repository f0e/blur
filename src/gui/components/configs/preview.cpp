#include "configs.h"

#include "common/rendering.h"
#include "common/weighting.h"

#include "../../tasks.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../notifications.h"

namespace {
	BlurSettings previewed_settings;
	bool first = true;
	size_t preview_id = 0;
	std::atomic<bool> loading = false;
	std::mutex preview_mutex;
	std::shared_ptr<render::Texture> preview_texture;
	std::vector<uint8_t> pending_jpeg;

	// the render backing the newest preview request. anything older has been stopped and isn't allowed
	// to publish its result anymore
	std::mutex current_render_mutex;
	std::shared_ptr<rendering::RenderState> current_render_state;

	std::shared_ptr<rendering::RenderState> begin_preview_render() {
		auto state = std::make_shared<rendering::RenderState>();

		std::lock_guard lock(current_render_mutex);

		if (current_render_state)
			current_render_state->stop();

		current_render_state = state;

		return state;
	}

	bool is_current_preview_render(const std::shared_ptr<rendering::RenderState>& state) {
		std::lock_guard lock(current_render_mutex);
		return current_render_state == state;
	}
}

namespace configs = gui::components::configs;

void configs::reset_config_preview() {
	preview_texture.reset();
	pending_jpeg.clear();
	preview_id = 0;
	loading = false;
	first = true;
	previewed_settings = {};
}

void configs::config_preview(ui::Container& container) {
	static auto debounce_time = std::chrono::milliseconds(50);
	auto now = std::chrono::steady_clock::now();
	static auto last_render_time = now;

	auto sample_video_path = blur.settings_path / "sample_video.mp4";
	bool sample_video_exists = std::filesystem::exists(sample_video_path);

	// upload pending surface on render thread
	std::vector<uint8_t> jpeg;
	{
		std::lock_guard lock(preview_mutex);
		jpeg = std::move(pending_jpeg);
		pending_jpeg.clear();
	}

	if (auto texture = render::texture_from_jpeg(jpeg)) {
		preview_texture = std::move(texture);
		preview_id++;
	}

	auto render_preview = [&] {
		if (!sample_video_exists) {
			preview_texture.reset();
			return;
		}

		if (first) {
			first = false;
		}
		else {
			if (settings == previewed_settings && !just_added_sample_video)
				return;

			if (now - last_render_time < debounce_time)
				return;
		}

		u::log("generating config preview");

		previewed_settings = settings;
		just_added_sample_video = false;
		last_render_time = now;

		loading = true;

		auto local_settings = settings;
		auto local_app_settings = app_settings;
		std::thread([sample_video_path, local_settings, local_app_settings] {
			auto state = begin_preview_render();

			auto res = rendering::render_frame(sample_video_path, local_settings, local_app_settings, state);

			// the result is for stale settings, throw it away
			if (!is_current_preview_render(state))
				return;

			loading = false;

			std::lock_guard lock(preview_mutex);

			if (res) {
				pending_jpeg = std::move(res->frame_jpeg);

				u::log("config preview finished rendering");
			}
			else {
				components::notifications::show_failure_notification(
					"Failed to generate config preview.", res.error(), std::chrono::duration<float>(10.f)
				);
			}
		}).detach();
	};

	render_preview();

	if (preview_texture && preview_texture->is_valid()) {
		ui::add_image(
			"config preview image",
			container,
			preview_texture,
			container.get_usable_rect().size(),
			std::to_string(preview_id),
			gfx::Color::white(loading ? 100 : 255)
		);
	}
	else {
		if (sample_video_exists) {
			if (loading) {
				ui::add_text(
					"loading config preview text",
					container,
					"Loading config preview...",
					gfx::Color::white(100),
					fonts::dejavu,
					FONT_CENTERED_X
				);
			}
			else {
				ui::add_text(
					"failed to generate preview text",
					container,
					"Failed to generate preview.",
					gfx::Color::white(100),
					fonts::dejavu,
					FONT_CENTERED_X
				);
			}
		}
		else {
			ui::add_text(
				"sample video does not exist text",
				container,
				"No preview video found.",
				gfx::Color::white(100),
				fonts::dejavu,
				FONT_CENTERED_X
			);

			ui::add_text(
				"sample video does not exist text 2",
				container,
				"Drop a video here to add one.",
				gfx::Color::white(100),
				fonts::dejavu,
				FONT_CENTERED_X
			);

			ui::add_button("open preview file button", container, "Or open file", fonts::dejavu, [] {
				static auto file_callback = [](void* userdata, const char* const* files, int filter) {
					if (files != nullptr && *files != nullptr) {
						const char* file = *files;
						tasks::add_sample_video(u::string_to_path(file));
					}
				};

				const std::array filters = {
					SDL_DialogFileFilter{
						.name = "Video files",
						.pattern =
							"webm;mkv;flv;vob;ogv;ogg;rrc;gifv;mng;mov;avi;qt;wmv;yuv;rm;rmvb;asf;amv;mp4;m4p;m4v;mpg;"
							"mp2;mpeg;mpe;mpv;svi;3gp;3g2;mxf;roq;nsv;f4v;f4p;f4a;f4b;mod;ts;m2ts;mts;divx;bik;wtv;"
							"drc",
					},
				};

				SDL_ShowOpenFileDialog(file_callback, nullptr, nullptr, filters.data(), 0, "", false);
			});
		}
	}
}

// todo: refactor
void configs::preview(ui::Container& header_container, ui::Container& content_container) {
	int interp_fps = 1200;
	bool parsed_interp_fps = false;

	if (settings.interpolate) {
		std::istringstream iss(settings.interpolated_fps);
		int temp_fps{};
		if ((iss >> temp_fps) && iss.eof()) {
			interp_fps = temp_fps;
			parsed_interp_fps = true;
		}
	}

	ui::add_tabs("preview tab", header_container, TABS, selected_tab, fonts::dejavu, [] {
		old_tab.clear();
	});

	if (selected_tab == "output video") {
		config_preview(content_container);
	}
	else {
		auto weight_settings = settings;

		if (!hovered_weighting.empty())
			weight_settings.blur_weighting = hovered_weighting;

		auto weights_res = weighting::get_weights(weight_settings, interp_fps);
		if (weights_res.error.empty()) {
			ui::add_weighting_graph("weighting graph", content_container, weights_res.weights, parsed_interp_fps);
		}
		else {
			ui::add_text(
				"weighting error",
				content_container,
				weights_res.error,
				gfx::Color(255, 50, 50, 255),
				fonts::dejavu,
				FONT_CENTERED_X | FONT_OUTLINE
			);
		}
	}

	ui::add_separator("config preview separator", content_container, ui::SeparatorStyle::FADE_BOTH);

	auto validation_res = config_blur::validate(settings, false);
	if (!validation_res) {
		ui::add_text(
			"config validation error/s",
			content_container,
			validation_res.error(),
			gfx::Color(255, 50, 50, 255),
			fonts::dejavu,
			FONT_CENTERED_X | FONT_OUTLINE
		);

		ui::add_button(
			"fix config button", content_container, "Reset invalid config options to defaults", fonts::dejavu, [&] {
				config_blur::validate(settings, true);
			}
		);
	}

	ui::add_button("export config", content_container, "Export", fonts::dejavu, [] {
		std::string exported_config = config_blur::export_concise(settings);
		SDL_SetClipboardText(exported_config.c_str());

		gui::components::notifications::add(
			"Exported config to clipboard", ui::NotificationType::INFO, {}, std::chrono::duration<float>(2.f)
		);
	});

	ui::set_next_same_line(content_container);

	ui::add_button("import config", content_container, "Import", fonts::dejavu, [] {
		size_t len = 0;
		void* clipboard_data = SDL_GetClipboardData("text/plain", &len);

		if (clipboard_data && len > 0) {
			std::string clipboard_text(static_cast<char*>(clipboard_data), len);
			SDL_free(clipboard_data);

			try {
				auto clipboard_settings = config_blur::parse(clipboard_text);

				ui::reset_tied_sliders();
				settings = clipboard_settings;

				gui::components::notifications::add(
					"Imported config from clipboard", ui::NotificationType::INFO, {}, std::chrono::duration<float>(2.f)
				);
			}
			catch (const std::exception& e) {
				gui::components::notifications::add(
					std::string("Failed to load config: ") + e.what(),
					ui::NotificationType::NOTIF_ERROR,
					{},
					std::chrono::duration<float>(3.f)
				);
			}
		}
		else {
			gui::components::notifications::add(
				"Clipboard is empty or unreadable",
				ui::NotificationType::NOTIF_ERROR,
				{},
				std::chrono::duration<float>(2.f)
			);
		}
	});

	ui::add_button("open config folder", content_container, "Open config folder", fonts::dejavu, [] {
		std::string file_url = std::format("file://{}", blur.settings_path);
		if (!SDL_OpenURL(file_url.c_str())) {
			u::log_error("Failed to open config folder: {}", SDL_GetError());
		}
	});
}
