#include "configs.h"

#include "common/rendering_frame.h"
#include "common/weighting.h"

#include "../../tasks.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../notifications.h"

namespace configs = gui::components::configs;

void configs::config_preview(ui::Container& container) {
	static BlurSettings previewed_settings;
	static bool first = true;

	static auto debounce_time = std::chrono::milliseconds(50);
	auto now = std::chrono::steady_clock::now();
	static auto last_render_time = now;

	static size_t preview_id = 0;
	static std::filesystem::path preview_path;
	static bool loading = false;
	static bool error = false;
	static std::mutex preview_mutex;

	auto sample_video_path = blur.settings_path / "sample_video.mp4";
	bool sample_video_exists = std::filesystem::exists(sample_video_path);

	auto render_preview = [&] {
		if (!sample_video_exists) {
			preview_path.clear();
			return;
		}

		if (first) {
			first = false;
		}
		else {
			if (settings == previewed_settings && !first && !just_added_sample_video)
				return;

			if (now - last_render_time < debounce_time)
				return;
		}

		u::log("generating config preview");

		previewed_settings = settings;
		just_added_sample_video = false;
		last_render_time = now;

		{
			std::lock_guard<std::mutex> lock(preview_mutex);
			loading = true;
		}

		auto local_settings = settings;
		auto local_app_settings = app_settings;
		std::thread([sample_video_path, local_settings, local_app_settings] {
			FrameRender* render = nullptr;

			{
				std::lock_guard<std::mutex> lock(render_mutex);

				// stop ongoing renders early, a new render's coming bro
				for (auto& render : renders) {
					render->stop();
				}

				render = renders.emplace_back(std::make_unique<FrameRender>()).get();
			}

			auto res = render->render(sample_video_path, local_settings, local_app_settings);

			if (render == renders.back().get())
			{ // todo: this should be correct right? any cases where this doesn't work?
				loading = false;

				if (res) {
					std::lock_guard<std::mutex> lock(preview_mutex);
					preview_id++;

					Blur::remove_temp_path(preview_path.parent_path());

					preview_path = *res;

					u::log("config preview finished rendering");
				}
				else {
					if (res.error() != "Input path does not exist") {
						gui::components::notifications::add(
							"Failed to generate config preview. Click to copy error message",
							ui::NotificationType::NOTIF_ERROR,
							[res](const std::string& id) {
								SDL_SetClipboardText(res.error().c_str());

								gui::components::notifications::close(id);

								gui::components::notifications::add(
									"Copied error message to clipboard",
									ui::NotificationType::INFO,
									{},
									std::chrono::duration<float>(2.f)
								);
							}
						);
					}
				}
			}

			render->set_can_delete();
		}).detach();
	};

	render_preview();

	// remove finished renders
	{
		std::lock_guard<std::mutex> lock(render_mutex);
		std::erase_if(renders, [](const auto& render) {
			return render->can_delete();
		});
	}

	try {
		if (!preview_path.empty() && std::filesystem::exists(preview_path) && !error) {
			auto element = ui::add_image(
				"config preview image",
				container,
				preview_path,
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
							tasks::add_sample_video(file);
						}
					};

					SDL_ShowOpenFileDialog(
						file_callback, // callback
						nullptr,       // userdata
						nullptr,       // parent window
						nullptr,       // file filters
						0,             // number of filters
						"",            // default path
						false          // allow multiple files
					);
				});
			}
		}
	}
	catch (std::filesystem::filesystem_error& e) {
		// i have no idea. std::filesystem::exists threw?
		u::log_error("std::filesystem::exists threw");
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
			"fix config button",
			content_container,
			"Reset invalid config options to defaults",
			fonts::dejavu,
			[&] {
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
		// Convert path to a file:// URL for SDL_OpenURL
		std::string file_url = "file://" + blur.settings_path.string(); // TODO FISH: TEST
		if (!SDL_OpenURL(file_url.c_str())) {
			u::log_error("Failed to open config folder: {}", SDL_GetError());
		}
	});
}
