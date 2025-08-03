#include "main.h"
#include "../renderer.h"

#include "common/rendering.h"

#include "../tasks.h"
#include "../gui.h"

#include "../ui/ui.h"
#include "../render/render.h"
#include <SDL3/SDL_dialog.h>

namespace main = gui::components::main;

void main::open_files_button(ui::Container& container, const std::string& label) {
	ui::add_button("open file button", container, label, fonts::dejavu, [] {
		static auto file_callback = [](void* userdata, const char* const* files, int filter) {
			if (files && *files) {
				std::vector<std::filesystem::path> wpaths;

				std::span<const char* const> span_files(files, SIZE_MAX); // big size, we stop manually

				for (const auto& file : span_files) {
					if (file == nullptr)
						break; // null-terminated array

					wpaths.emplace_back(u::string_to_path(file));
				}

				// TODO FISH: TEST
				tasks::add_files(wpaths);
			}
		};

		const SDL_DialogFileFilter filters[] = {
			{ "Video files",
			  "webm;mkv;flv;vob;ogv;ogg;rrc;gifv;mng;mov;avi;qt;wmv;yuv;rm;rmvb;asf;amv;mp4;m4p;m4v;mpg;mp2;mpeg;mpe;"
			  "mpv;svi;3gp;3g2;mxf;roq;nsv;f4v;f4p;f4a;f4b;mod;ts;m2ts;mts;divx;bik;wtv;drc" }
		};

		SDL_ShowOpenFileDialog(
			file_callback, // Properly typed callback function
			nullptr,       // userdata
			nullptr,       // parent window (nullptr for default)
			filters,       // file filters
			1,             // number of filters
			"",            // default path
			true           // allow multiple files
		);
	});
};

void main::render_screen(
	ui::Container& container,
	Render& render,
	bool current,
	float delta_time,
	bool& is_progress_shown,
	float& bar_percent
) {
	// todo: ui concept
	// screen start|      [faded]last_video current_video [faded]next_video next_video2 next_video3 (+5) |
	// screen end animate sliding in as it moves along the queue
	ui::add_text(
		std::format("video {} name text", render.get_render_id()),
		container,
		render.get_video_name(),
		gfx::Color(255, 255, 255, (current ? 255 : 100)),
		fonts::smaller_header_font,
		FONT_CENTERED_X
	);

	if (!current)
		return;

	auto render_status = render.get_status();
	int bar_width = 300;

	std::string preview_path = render.get_preview_path().string();
	if (!preview_path.empty() && render_status.current_frame > 0) {
		auto element = ui::add_image(
			"preview image",
			container,
			preview_path,
			gfx::Size(container.get_usable_rect().w, container.get_usable_rect().h / 2),
			std::to_string(render_status.current_frame)
		);
		if (element) {
			bar_width = (*element)->element->rect.w;
		}
	}

	if (render_status.init_frames) {
		float render_progress = (float)render_status.current_frame / (float)render_status.total_frames;
		bar_percent = u::lerp(bar_percent, render_progress, 5.f * delta_time, 0.005f);

		ui::add_bar(
			"progress bar",
			container,
			bar_percent,
			gfx::Color(51, 51, 51, 255),
			gfx::Color::white(),
			bar_width,
			std::format("{:.1f}%", render_progress * 100),
			gfx::Color::white(),
			&fonts::dejavu
		);

		container.push_element_gap(6);

		if (render.is_paused()) {
			ui::add_text(
				"paused text",
				container,
				"Paused",
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}

		bool status_fps_init = render_status.fps != 0.f;

		if (!status_fps_init)
			container.pop_element_gap();

		ui::add_text(
			"progress text",
			container,
			std::format("frame {}/{}", render_status.current_frame, render_status.total_frames),
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu,
			FONT_CENTERED_X
		);

		if (status_fps_init) {
			ui::add_text(
				"progress text fps",
				container,
				std::format("{:.2f} frames per second", render_status.fps),
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);

			container.pop_element_gap();

			int remaining_frames = render_status.total_frames - render_status.current_frame;
			int eta_seconds = static_cast<int>(remaining_frames / render_status.fps);

			int hours = eta_seconds / 3600;
			int minutes = (eta_seconds % 3600) / 60;
			int seconds = eta_seconds % 60;

			std::ostringstream eta_stream;
			if (hours > 0)
				eta_stream << hours << " hour" << (hours > 1 ? "s " : " ");
			if (minutes > 0)
				eta_stream << minutes << " minute" << (minutes > 1 ? "s " : " ");
			if (seconds > 0 || (hours == 0 && minutes == 0))
				eta_stream << seconds << " second" << (seconds != 1 ? "s" : "");

			ui::add_text(
				"progress text eta",
				container,
				std::format("~{} left", eta_stream.str()),
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}

		is_progress_shown = true;
	}
	else {
		if (render.is_paused()) {
			ui::add_text(
				"paused text",
				container,
				"Paused",
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}
		else {
			ui::add_text(
				"initialising render text",
				container,
				"Initialising render...",
				gfx::Color::white(),
				fonts::dejavu,
				FONT_CENTERED_X
			);
		}
	}
}

void main::home_screen(ui::Container& container, float delta_time) {
	static float bar_percent = 0.f;

	bool queue_empty = rendering.get_queue().empty() && !current_render_copy;

	if (queue_empty) {
		bar_percent = 0.f;

		gfx::Point title_pos = container.get_usable_rect().center();
		if (container.rect.h > 275)
			title_pos.y = int(renderer::PAD_Y + fonts::header_font.height());
		else
			title_pos.y = 10 + fonts::header_font.height();

		ui::add_text_fixed(
			"blur title text", container, title_pos, "blur", gfx::Color::white(), fonts::header_font, FONT_CENTERED_X
		);

		if (!initialisation_res) {
			ui::add_text(
				"failed to initialise text",
				container,
				"Failed to initialise",
				gfx::Color::white(),
				fonts::dejavu,
				FONT_CENTERED_X
			);

			ui::add_text(
				"failed to initialise reason",
				container,
				initialisation_res.error(),
				gfx::Color::white(renderer::MUTED_SHADE),
				fonts::dejavu,
				FONT_CENTERED_X
			);

			return;
		}

		open_files_button(container, "Open files");

		ui::add_text(
			"drop file text", container, "or drop them anywhere", gfx::Color::white(), fonts::dejavu, FONT_CENTERED_X
		);
	}
	else {
		bool is_progress_shown = false;

		rendering.lock();
		{
			auto current_render = rendering.get_current_render();

			// displays final state of the current render once where it would have been skipped otherwise
			auto render_current_edge_case = [&] {
				if (!current_render_copy)
					return;

				if (current_render) {
					if ((*current_render)->get_render_id() == (*current_render_copy).get_render_id()) {
						u::log("render final frame: wasnt deleted so just render normally");
						return;
					}
				}

				u::log("render final frame: it was deleted bro, rendering separately");

				render_screen(container, *current_render_copy, true, delta_time, is_progress_shown, bar_percent);

				current_render_copy.reset();
			};

			render_current_edge_case();

			for (const auto [i, render] : u::enumerate(rendering.get_queue())) {
				bool current = current_render && render.get() == current_render.value();

				render_screen(container, *render, current, delta_time, is_progress_shown, bar_percent);
			}
		}
		rendering.unlock();

		if (!is_progress_shown) {
			bar_percent = 0.f; // Reset when no progress bar is shown
		}
	}
}
