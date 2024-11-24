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

SkFont utils::create_font_from_data(const unsigned char* font_data, size_t data_size, float font_height) {
	sk_sp<SkData> skData = SkData::MakeWithCopy(font_data, data_size);

	// Create a typeface from SkData
	sk_sp<SkTypeface> typeface = SkTypeface::MakeFromData(skData);

	if (!typeface) {
		printf("failed to create font\n");
		return SkFont();
	}

	return SkFont(typeface, SkIntToScalar(font_height));
}

#ifdef _WIN32
double utils::get_display_refresh_rate(HMONITOR hMonitor)
#elif defined(__APPLE__)
double utils::get_display_refresh_rate(void* nsScreen) // Take NSScreen* as void* to avoid Obj-C++ dependency
#else
double utils::get_display_refresh_rate(int screenNumber)
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
	uint32_t displayID = 0;

	// This code assumes the Obj-C++ library is using ARC
	// The following C functions are documented and stable API
	typedef void* (*CGSGetDisplayIDFromDisplayNameFuncPtr)(void* display);
	static CGSGetDisplayIDFromDisplayNameFuncPtr CGSGetDisplayIDFromDisplayName =
		(CGSGetDisplayIDFromDisplayNameFuncPtr)dlsym(RTLD_DEFAULT, "CGSGetDisplayIDFromDisplayName");

	if (CGSGetDisplayIDFromDisplayName && nsScreen) {
		displayID = (uint32_t)(uintptr_t)CGSGetDisplayIDFromDisplayName(nsScreen);
	}

	if (displayID == 0) {
		displayID = CGMainDisplayID(); // Fallback to main display
	}

	CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
	if (!mode) {
		return 0.0;
	}

	double refreshRate = CGDisplayModeGetRefreshRate(mode);
	CGDisplayModeRelease(mode);

	// If refresh rate is 0, try to calculate it from the nominal refresh period
	if (refreshRate == 0.0) {
		CVDisplayLinkRef displayLink;
		if (CVDisplayLinkCreateWithCGDisplay(displayID, &displayLink) == kCVReturnSuccess) {
			const CVTime time = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(displayLink);
			if (time.timeScale > 0 && time.timeValue > 0) {
				refreshRate = double(time.timeScale) / double(time.timeValue);
			}
			CVDisplayLinkRelease(displayLink);
		}
	}

	return refreshRate;

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

bool utils::show_file_selector( // aseprite
	const std::string& title,
	const std::string& initialPath,
	const base::paths& extensions,
	os::FileDialog::Type type,
	base::paths& output
) {
	const std::string defExtension = ""; //

	if (os::instance()->nativeDialogs()) {
		os::FileDialogRef dlg =
			os::instance()->nativeDialogs()->makeFileDialog();

		if (dlg) {
			dlg->setTitle(title);

			// Must be set before setFileName() as the Linux impl might
			// require the default extension to fix the initial file name
			// with the default extension.
			if (!defExtension.empty()) {
				dlg->setDefaultExtension(defExtension);
			}

#if LAF_LINUX // As the X11 version doesn't store the default path to
              // start navigating, we use our own
              // get_initial_path_to_select_filename()
			dlg->setFileName(get_initial_path_to_select_filename(initialPath));
#else // !LAF_LINUX
			dlg->setFileName(initialPath);
#endif

			dlg->setType(type);

			for (const auto& ext : extensions)
				dlg->addFilter(ext, ext + " files (*." + ext + ")");

			auto res = dlg->show(os::instance()->defaultWindow());
			if (res != os::FileDialog::Result::Error) {
				if (res == os::FileDialog::Result::OK) {
					if (type == os::FileDialog::Type::OpenFiles)
						dlg->getMultipleFileNames(output);
					else
						output.push_back(dlg->fileName());

#if LAF_LINUX // Save the path in the configuration file
					if (!output.empty()) {
						set_current_dir_for_file_selector(base::get_file_path(output[0]));
					}
#endif

					return true;
				}
				else {
					return false;
				}
			}
			else {
				// Fallback to default file selector if we weren't able to
				// open the native dialog...
			}
		}
	}

	// FileSelector fileSelector(type);

	// if (!defExtension.empty())
	// 	fileSelector.setDefaultExtension(defExtension);

	// return fileSelector.show(title, initialPath, extensions, output);
	return false;
}
