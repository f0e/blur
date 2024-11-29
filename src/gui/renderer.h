
#pragma once

#include "common/rendering.h"
#include "common/rendering_frame.h"
#include "ui/ui.h"

namespace ui {
	class Container;
}

namespace gui::renderer {
	namespace fonts {
		inline SkFont font;
		inline SkFont header_font;
		inline SkFont smaller_header_font;
	}

	inline uint32_t current_notification_id = 0;

	struct Notification {
		std::chrono::steady_clock::time_point end_time;
		std::string text;
		ui::NotificationType type;
		uint32_t id = current_notification_id++;
	};

	inline std::vector<Notification> notifications;
	inline const float NOTIFICATION_LENGTH = 4.f;

	inline os::NativeCursor current_cursor;
	inline bool set_cursor_this_frame = false;

	inline std::optional<Render> current_render_copy;

	void init_fonts();

	void set_cursor(os::NativeCursor cursor);

	enum class Screens : uint8_t {
		MAIN,
		CONFIG
	};

	inline Screens screen = Screens::MAIN;

	inline ui::Container main_container;
	inline ui::Container config_container;
	inline ui::Container config_preview_container;
	inline ui::Container notification_container;
	inline ui::Container nav_container;

	namespace components {
		void render(
			ui::Container& container,
			const Render& render,
			bool current,
			float delta_time,
			bool& is_progress_shown,
			float& bar_percent
		);

		void main_screen(ui::Container& container, float delta_time);

		namespace configs { // naming it configs to avoid conflict with common lol
			inline BlurSettings settings;
			inline BlurSettings current_global_settings;

			inline bool loaded_config = false;

			inline bool interpolate_scale = true;
			inline float interpolated_fps_mult = 5.f;
			inline int interpolated_fps = 1200;

			inline std::vector<std::unique_ptr<FrameRender>> renders;
			inline std::mutex render_mutex;

			void set_interpolated_fps();

			void options(ui::Container& container, BlurSettings& settings);
			void preview(ui::Container& container, BlurSettings& settings);
			void screen(ui::Container& container, ui::Container& preview_container, float delta_time);
		}

	}

	bool redraw_window(os::Window* window, bool force_render);

	void add_notification(
		const std::string& text, ui::NotificationType type, std::chrono::steady_clock::time_point end_time
	);
	void add_notification(
		const std::string& text,
		ui::NotificationType type,
		std::chrono::duration<float> duration = std::chrono::duration<float>(NOTIFICATION_LENGTH)
	);

	void on_render_finished(Render* render, bool success);
}
