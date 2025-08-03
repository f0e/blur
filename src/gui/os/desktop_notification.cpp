#include "desktop_notification.h"

#ifdef _WIN32
#	include "common/blur.h"
#	include "../sdl.h"
#	include <shellapi.h>
#	include <winrt/Windows.Foundation.h>
#	include <winrt/Windows.UI.Notifications.h>
#	include <winrt/Windows.Data.Xml.Dom.h>
#	include <winrt/Windows.ApplicationModel.h>
using namespace winrt;
using namespace Windows::UI::Notifications;
using namespace Windows::Data::Xml::Dom;

static std::string g_app_name;
static desktop_notification::ClickCallback g_click_callback;
static bool g_initialised = false;

bool desktop_notification::initialise(const std::string& app_name) {
	if (g_initialised)
		return true;

	// winrt::init_apartment(); // don't need it?

	g_app_name = app_name;
	g_initialised = true;
	return true;
}

bool desktop_notification::show(const std::string& title, const std::string& message, ClickCallback on_click) {
	// don't show notification if window is focused
	if (SDL_GetWindowFlags(sdl::window) & SDL_WINDOW_INPUT_FOCUS)
		return false;

	if (!g_initialised)
		initialise(APPLICATION_NAME);

	g_click_callback = on_click;

	try {
		auto toast_xml = ToastNotificationManager::GetTemplateContent(ToastTemplateType::ToastText02);

		auto text_elements = toast_xml.GetElementsByTagName("text");
		text_elements.Item(0).AppendChild(toast_xml.CreateTextNode(winrt::to_hstring(title)));
		text_elements.Item(1).AppendChild(toast_xml.CreateTextNode(winrt::to_hstring(message)));

		// // ToastImageAndText02
		// auto image_elements = toast_xml.GetElementsByTagName("image");
		// auto image_element = image_elements.Item(0).as<Windows::Data::Xml::Dom::XmlElement>();
		// image_element.SetAttribute(
		// 	"src", winrt::hstring("file:///[something]/blur.ico")
		// );
		// image_element.SetAttribute("alt", winrt::hstring("Blur icon"));

		auto toast = ToastNotification(toast_xml);

		if (on_click) {
			toast.Activated([](ToastNotification const&, auto const&) {
				SDL_RaiseWindow(sdl::window); // probably won't work.

				if (g_click_callback)
					g_click_callback();
			});
		}

		ToastNotificationManager::CreateToastNotifier(winrt::to_hstring(g_app_name)).Show(toast);
		return true;
	}
	catch (...) {
		return false;
	}
}

bool desktop_notification::is_supported() {
	return true;
}

bool desktop_notification::has_permission() {
	return true;
}

void desktop_notification::cleanup() {
	if (!g_initialised)
		return;

	g_initialised = false;
}
#elif __linux__
#	include "desktop_notification.h"
#	include <sdbus-c++/sdbus-c++.h>
#	include "common/utils.h"

namespace {
	std::unique_ptr<sdbus::IConnection> g_connection;
	std::unique_ptr<sdbus::IProxy> g_proxy;
	std::string g_app_name;
	bool g_initialized = false;
}

static constexpr const char* SERVICE = "org.freedesktop.Notifications";
static constexpr const char* PATH = "/org/freedesktop/Notifications";
static constexpr const char* INTERFACE = "org.freedesktop.Notifications";

bool desktop_notification::initialise(const std::string& app_name) {
	if (g_initialized) {
		return true;
	}

	try {
		g_app_name = app_name;

		// Create connection and proxy with proper types
		g_connection = sdbus::createSessionBusConnection();
		g_proxy = sdbus::createProxy(*g_connection, sdbus::ServiceName{ SERVICE }, sdbus::ObjectPath{ PATH });

		g_initialized = true;
		return true;
	}
	catch (const std::exception& e) {
		u::log_error("Failed to initialise desktop notifications: {}", e.what());
		return false;
	}
}

bool desktop_notification::show(
	const std::string& title, const std::string& message, ClickCallback on_click
) { // TODO: on_click
	if (!g_initialized) {
		if (!initialise(APPLICATION_NAME)) {
			u::log_error("Desktop notifications not initialised");
			return false;
		}
	}

	try {
		std::vector<std::string> actions;
		// if (on_click) {
		// 	actions = { "default", "Click" };
		// }

		uint32_t notification_id;

		g_proxy->callMethod("Notify")
			.onInterface(INTERFACE)
			.withArguments(
				g_app_name,                              // app_name
				uint32_t(0),                             // replaces_id
				std::string(""),                         // app_icon
				title,                                   // summary
				message,                                 // body
				actions,                                 // actions
				std::map<std::string, sdbus::Variant>{}, // hints
				5000                                     // timeout (ms)
			)
			.storeResultsTo(notification_id);

		return true;
	}
	catch (const std::exception& e) {
		u::log_error("Failed to show desktop notification: {}", e.what());
		return false;
	}
}

bool desktop_notification::is_supported() {
	try {
		auto conn = sdbus::createSessionBusConnection();
		auto proxy = sdbus::createProxy(*conn, sdbus::ServiceName{ SERVICE }, sdbus::ObjectPath{ PATH });

		auto method = proxy->callMethod("GetCapabilities");
		method.onInterface(INTERFACE);

		return true;
	}
	catch (...) {
		return false;
	}
}

bool desktop_notification::has_permission() {
	return is_supported(); // On Linux, D-Bus access implies permission
}

void desktop_notification::cleanup() {
	if (!g_initialized)
		return;

	g_proxy.reset();
	g_connection.reset();
	g_initialized = false;
}
#endif
