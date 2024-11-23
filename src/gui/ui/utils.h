#pragma once

namespace utils {
	SkFont create_font_from_data(const unsigned char* font_data, size_t data_size, float font_height);

#ifdef _WIN32
	double get_display_refresh_rate(HMONITOR hMonitor);
#elif defined(__APPLE__)
	double get_display_refresh_rate(void* nsScreen);
#else
	double get_display_refresh_rate(int screenNumber);
#endif
}
