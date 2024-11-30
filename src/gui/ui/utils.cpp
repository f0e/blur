#include "utils.h"

#ifdef _WIN32
#	include <windows.h>
#elif defined(__APPLE__)
#	include <CoreGraphics/CoreGraphics.h>
#	include <CoreVideo/CoreVideo.h>
#	include <dlfcn.h>
#else // X11
#	include <X11/Xlib.h>
#	include <X11/extensions/Xrandr.h>
#endif

gfx::Color utils::adjust_color(const gfx::Color& color, float anim) {
	return gfx::rgba(
		gfx::getr(color), gfx::getg(color), gfx::getb(color), gfx::ColorComponent(round((float)gfx::geta(color) * anim))
	); // seta is broken or smth i swear
}

SkFont utils::create_font_from_data(const unsigned char* font_data, size_t data_size, float font_height) {
	sk_sp<SkData> sk_data = SkData::MakeWithCopy(font_data, data_size);

	// Create a typeface from SkData
	sk_sp<SkTypeface> typeface = SkTypeface::MakeFromData(sk_data);

	if (!typeface) {
		u::log("failed to create font");
		return {};
	}

	return { typeface, SkIntToScalar(font_height) };
}

// NOLINTBEGIN

#ifdef _WIN32
double utils::get_display_refresh_rate(HMONITOR hMonitor)
#elif defined(__APPLE__)
double utils::get_display_refresh_rate(void* nsScreen) // Take NSScreen* as void* to avoid Obj-C++ dependency
#else
double utils::get_display_refresh_rate(intptr_t screenNumber)
#endif
{
#ifdef _WIN32
	MONITORINFOEXW monitorInfo = { sizeof(MONITORINFOEXW) };
	if (!GetMonitorInfoW(hMonitor, &monitorInfo)) {
		return 0.0;
	}

	DEVMODEW devMode = { sizeof(DEVMODEW) };
	if (!EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode)) {
		return 0.0;
	}

	return static_cast<double>(devMode.dmDisplayFrequency);

#elif defined(__APPLE__)
	// Get the CGDirectDisplayID using pure C API
	uint32_t display_id = 0;

	// This code assumes the Obj-C++ library is using ARC
	// The following C functions are documented and stable API
	using CGSGetDisplayIDFromDisplayNameFuncPtr = void* (*)(void*);
	static auto CGSGetDisplayIDFromDisplayName =
		(CGSGetDisplayIDFromDisplayNameFuncPtr)dlsym(RTLD_DEFAULT, "CGSGetDisplayIDFromDisplayName");

	if (CGSGetDisplayIDFromDisplayName && nsScreen) {
		display_id = (uint32_t)(uintptr_t)CGSGetDisplayIDFromDisplayName(nsScreen);
	}

	if (display_id == 0) {
		display_id = CGMainDisplayID(); // Fallback to main display
	}

	CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display_id);
	if (!mode) {
		return 0.0;
	}

	double refresh_rate = CGDisplayModeGetRefreshRate(mode);
	CGDisplayModeRelease(mode);

	// If refresh rate is 0, try to calculate it from the nominal refresh period
	if (refresh_rate == 0.0) {
		CVDisplayLinkRef display_link = nullptr;
		if (CVDisplayLinkCreateWithCGDisplay(display_id, &display_link) == kCVReturnSuccess) {
			const CVTime time = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(display_link);
			if (time.timeScale > 0 && time.timeValue > 0) {
				refresh_rate = double(time.timeScale) / double(time.timeValue);
			}
			CVDisplayLinkRelease(display_link);
		}
	}

	return refresh_rate;

#else // X11
	Display* display = XOpenDisplay(nullptr);
	if (!display) {
		return 0.0;
	}

	XRRScreenConfiguration* config = XRRGetScreenInfo(display, RootWindow(display, screenNumber));
	if (!config) {
		XCloseDisplay(display);
		return 0.0;
	}

	short rate = XRRConfigCurrentRate(config);
	XRRFreeScreenConfigInfo(config);
	XCloseDisplay(display);

	return static_cast<double>(rate);
#endif
}

// NOLINTEND

bool utils::show_file_selector( // aseprite
	const std::string& title,
	const std::string& initial_path,
	const base::paths& extensions,
	os::FileDialog::Type type,
	base::paths& output
) {
	const std::string def_extension;

	if (os::instance()->nativeDialogs()) {
		os::FileDialogRef dlg = os::instance()->nativeDialogs()->makeFileDialog();

		if (dlg) {
			dlg->setTitle(title);

			// Must be set before setFileName() as the Linux impl might
			// require the default extension to fix the initial file name
			// with the default extension.
			if (!def_extension.empty()) {
				dlg->setDefaultExtension(def_extension);
			}

			// #if LAF_LINUX // As the X11 version doesn't store the default path to
			//               // start navigating, we use our own
			//               // get_initial_path_to_select_filename()
			// 			dlg->setFileName(get_initial_path_to_select_filename(initialPath));
			// #else // !LAF_LINUX
			// 			dlg->setFileName(initial_path);
			// #endif

			dlg->setType(type);

			for (const auto& ext : extensions)
				dlg->addFilter(ext, std::format("{} files (*.{})", ext, ext));

			auto res = dlg->show(os::instance()->defaultWindow());
			if (res != os::FileDialog::Result::Error) {
				if (res == os::FileDialog::Result::OK) {
					if (type == os::FileDialog::Type::OpenFiles)
						dlg->getMultipleFileNames(output);
					else
						output.push_back(dlg->fileName());

					// #if LAF_LINUX // Save the path in the configuration file
					// 					if (!output.empty()) {
					// 						set_current_dir_for_file_selector(base::get_file_path(output[0]));
					// 					}
					// #endif

					return true;
				}

				return false;
			}

			// Fallback to default file selector if we weren't able to
			// open the native dialog...
		}
	}

	// FileSelector fileSelector(type);

	// if (!defExtension.empty())
	// 	fileSelector.setDefaultExtension(defExtension);

	// return fileSelector.show(title, initialPath, extensions, output);
	return false;
}
