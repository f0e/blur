#include "taskbar.h"

#ifdef _WIN32
#	include <SDL3/SDL.h>
#	include "common/utils.h"

namespace {
	// the shell wants whole numbers, so pick a denominator fine enough that the bar moves smoothly
	constexpr ULONGLONG PROGRESS_RESOLUTION = 1000;

	ITaskbarList3* g_taskbar = nullptr;
	HWND g_hwnd = nullptr;
	bool g_com_initialised = false;

	os::taskbar::ProgressState g_last_state = os::taskbar::ProgressState::NONE;
	ULONGLONG g_last_value = 0;

	TBPFLAG to_flag(os::taskbar::ProgressState state) {
		switch (state) {
			case os::taskbar::ProgressState::INDETERMINATE:
				return TBPF_INDETERMINATE;
			case os::taskbar::ProgressState::NORMAL:
				return TBPF_NORMAL;
			case os::taskbar::ProgressState::PAUSED:
				return TBPF_PAUSED;
			case os::taskbar::ProgressState::ERRORED:
				return TBPF_ERROR;
			case os::taskbar::ProgressState::NONE:
				break;
		}
		return TBPF_NOPROGRESS;
	}
}

void os::taskbar::initialise(SDL_Window* window) {
	if (g_taskbar)
		return;

	if (!window)
		return;

	g_hwnd = static_cast<HWND>(
		SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)
	);
	if (!g_hwnd) {
		u::log_error("taskbar progress: couldn't get the window handle");
		return;
	}

	// sdl's video backend already does this, but don't rely on that. RPC_E_CHANGED_MODE means the
	// thread is initialised in the other apartment model, which the taskbar interface is fine with -
	// we just mustn't uninitialise someone else's apartment on the way out
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	g_com_initialised = SUCCEEDED(hr);

	hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_taskbar));
	if (FAILED(hr)) {
		u::log_error("taskbar progress: failed to create the taskbar list ({:#x})", (uint32_t)hr);
		g_taskbar = nullptr;
		return;
	}

	hr = g_taskbar->HrInit();
	if (FAILED(hr)) {
		u::log_error("taskbar progress: failed to initialise the taskbar list ({:#x})", (uint32_t)hr);
		g_taskbar->Release();
		g_taskbar = nullptr;
	}
}

void os::taskbar::set_progress(ProgressState state, float progress) {
	if (!g_taskbar || !g_hwnd)
		return;

	auto value = (ULONGLONG)std::lround(std::clamp(progress, 0.f, 1.f) * (float)PROGRESS_RESOLUTION);

	bool state_changed = state != g_last_state;
	bool determinate =
		state == ProgressState::NORMAL || state == ProgressState::PAUSED || state == ProgressState::ERRORED;

	// value first: SetProgressValue promotes NOPROGRESS/INDETERMINATE to NORMAL, so setting it after
	// the state would throw away a paused/errored colour. it leaves an already-set state alone
	if (determinate && (state_changed || value != g_last_value)) {
		g_taskbar->SetProgressValue(g_hwnd, value, PROGRESS_RESOLUTION);
		g_last_value = value;
	}

	if (state_changed) {
		g_taskbar->SetProgressState(g_hwnd, to_flag(state));
		g_last_state = state;
	}
}

void os::taskbar::cleanup() {
	if (g_taskbar) {
		if (g_hwnd)
			g_taskbar->SetProgressState(g_hwnd, TBPF_NOPROGRESS);

		g_taskbar->Release();
		g_taskbar = nullptr;
	}

	g_hwnd = nullptr;
	g_last_state = ProgressState::NONE;
	g_last_value = 0;

	if (g_com_initialised) {
		CoUninitialize();
		g_com_initialised = false;
	}
}
#elif defined(__linux__)
#	include <sdbus-c++/sdbus-c++.h>
#	include "common/utils.h"

// the unity launcher entry protocol - a plain session-bus signal that kde's task manager, dash-to-dock,
// docky and friends all listen for. it's keyed on a desktop file id, so it only lights up for installs
// that shipped one; everywhere else the signal just goes nowhere
namespace {
	constexpr const char* PATH = "/com/canonical/unity/launcherentry/blur";
	constexpr const char* INTERFACE = "com.canonical.Unity.LauncherEntry";
	constexpr const char* APP_URI = "application://blur.desktop";

	std::unique_ptr<sdbus::IConnection> g_connection;
	std::unique_ptr<sdbus::IObject> g_object;
	bool g_initialised = false;

	os::taskbar::ProgressState g_last_state = os::taskbar::ProgressState::NONE;
	float g_last_progress = -1.f;
}

void os::taskbar::initialise(SDL_Window* /*window*/) {
	if (g_initialised)
		return;

	try {
		g_connection = sdbus::createSessionBusConnection();
		g_object = sdbus::createObject(*g_connection, sdbus::ObjectPath{ PATH });
		g_initialised = true;
	}
	catch (const std::exception& e) {
		u::log_error("taskbar progress: failed to connect to the session bus: {}", e.what());
		g_object.reset();
		g_connection.reset();
	}
}

void os::taskbar::set_progress(ProgressState state, float progress) {
	if (!g_initialised)
		return;

	progress = std::clamp(progress, 0.f, 1.f);

	bool visible = state != ProgressState::NONE;

	// nothing to show a fraction for while we're indeterminate, so peg it to full - the launcher has no
	// indeterminate mode, and an empty bar reads as "stuck"
	if (state == ProgressState::INDETERMINATE)
		progress = 1.f;

	if (state == g_last_state && progress == g_last_progress)
		return;

	g_last_state = state;
	g_last_progress = progress;

	try {
		std::map<std::string, sdbus::Variant> properties{
			{ "progress", sdbus::Variant(double(progress)) },
			{ "progress-visible", sdbus::Variant(visible) },
			{ "urgent", sdbus::Variant(state == ProgressState::ERRORED) },
		};

		g_object->emitSignal("Update").onInterface(INTERFACE).withArguments(std::string(APP_URI), properties);
	}
	catch (const std::exception& e) {
		u::log_error("taskbar progress: failed to emit launcher update: {}", e.what());
	}
}

void os::taskbar::cleanup() {
	if (g_initialised)
		set_progress(ProgressState::NONE);

	g_object.reset();
	g_connection.reset();
	g_initialised = false;

	g_last_state = ProgressState::NONE;
	g_last_progress = -1.f;
}
#elif !defined(__APPLE__)
// macos lives in taskbar_mac.mm

void os::taskbar::initialise(SDL_Window* /*window*/) {}

void os::taskbar::set_progress(ProgressState /*state*/, float /*progress*/) {}

void os::taskbar::cleanup() {}
#endif
