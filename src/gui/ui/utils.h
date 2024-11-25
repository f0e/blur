#pragma once

namespace utils {
	gfx::Color adjust_color(const gfx::Color& color, float anim);

	SkFont create_font_from_data(const unsigned char* font_data, size_t data_size, float font_height);

	// NOLINTBEGIN
#ifdef _WIN32
	double get_display_refresh_rate(HMONITOR hMonitor);
#elif defined(__APPLE__)
	double get_display_refresh_rate(void* nsScreen);
#else
	double get_display_refresh_rate(int screenNumber);
#endif
	// NOLINTEND

	bool show_file_selector(
		const std::string& title,
		const std::string& initial_path,
		const base::paths& extensions,
		os::FileDialog::Type type,
		base::paths& output
	);
}
