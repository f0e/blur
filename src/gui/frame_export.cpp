#include "frame_export.h"
#include "preview_files.h"
#include "sdl.h"
#include "components/notifications.h"
#include "fonts/icons.h"
#include "render/render.h"
#include "ui/helpers/video.h"

namespace {
	using OnWritten = std::function<void(std::optional<std::string> error)>;

	void remove_file(const std::filesystem::path& path) {
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}

	void notify_error(const std::string& error) {
		gui::components::notifications::add(
			std::format("Could not save the frame: {}", error), ui::NotificationType::NOTIF_ERROR
		);
	}

	// at full size, rather than the size it's drawn at
	void write_png(const ui::Frame& frame, const std::filesystem::path& path, const OnWritten& on_done) {
		if (frame.texture) {
			on_done(frame.texture->save_png(path) ? std::nullopt : std::optional<std::string>("it couldn't be read"));
			return;
		}

		if (frame.player) {
			frame.player->screenshot_to_file(path, on_done);
			return;
		}

		on_done("there isn't one");
	}

	// the dialog can call back from another thread
	void on_save_chosen(void* userdata, const char* const* files, int /*filter*/) {
		std::unique_ptr<std::filesystem::path> temp_path(static_cast<std::filesystem::path*>(userdata));

		// null on an error, empty when it's cancelled
		if (!files || !*files) {
			remove_file(*temp_path);
			return;
		}

		auto path = u::string_to_path(*files);
		if (!path.has_extension())
			path += ".png";

		// the dialog's already asked about replacing it
		std::error_code ec;
		std::filesystem::copy_file(*temp_path, path, std::filesystem::copy_options::overwrite_existing, ec);
		remove_file(*temp_path);

		if (ec) {
			notify_error(ec.message());
			return;
		}

		gui::components::notifications::add(
			std::format("Saved frame to {}", u::path_to_string(path.filename())),
			ui::NotificationType::SUCCESS,
			[path](const std::string&) {
				// file urls open it in the default image viewer
				std::string url = std::format("file://{}", u::path_to_string(path));
				if (!SDL_OpenURL(url.c_str()))
					u::log_error("Failed to open '{}': {}", u::path_to_string(path), SDL_GetError());
			},
			std::chrono::duration<float>(4.f)
		);
	}

	// written first, so it's the frame that was right clicked even if the preview moves on while the dialog's open
	void save(const ui::Frame& frame, const std::filesystem::path& default_path) {
		auto temp_path = preview_files::new_path("png");

		write_png(frame, temp_path, [temp_path, default_path](const std::optional<std::string>& error) {
			if (error) {
				remove_file(temp_path);
				notify_error(*error);
				return;
			}

			static constexpr std::array FILTERS = {
				SDL_DialogFileFilter{ .name = "PNG image", .pattern = "png" },
			};

			auto userdata = std::make_unique<std::filesystem::path>(temp_path);

			SDL_ShowSaveFileDialog(
				on_save_chosen,
				userdata.release(),
				sdl::window,
				FILTERS.data(),
				static_cast<int>(FILTERS.size()),
				u::path_to_string(default_path).c_str()
			);
		});
	}
}

std::vector<ui::ContextMenuItem> frame_export::menu_items(
	const ui::Frame& frame, const std::filesystem::path& video_path, bool mask
) {
	auto default_path = video_path.parent_path() / video_path.stem();
	default_path += mask ? " mask.png" : ".png";

	return {
		{
			.label = "Save frame",
			.icon = icons::SAVE,
			.on_press =
				[frame, default_path] {
					save(frame, default_path);
				},
		},
	};
}
