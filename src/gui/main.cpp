#include "gui.h"

#include "console.h"

#include <common/preview.h>

#include <shellapi.h>

void queue_arg_renders() {
	int num_args;
	LPWSTR* arg_list = CommandLineToArgvW(GetCommandLineW(), &num_args);
	if (arg_list) {
		for (int i = 1; i < num_args; i++) {
			std::wstring path = arg_list[i];
			if (!std::filesystem::exists(path))
				continue;

			rendering.queue_render(std::make_shared<c_render>(path));
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
#ifdef _DEBUG
	console::init();
#endif

	queue_arg_renders();

	// run gui in another thread
	std::thread(gui::run).detach();

	// main code (todo: separate)
	blur.initialise(true, true);

	while (gui::open) {
		rendering.render_videos();
	}

	preview.stop();

	return 0;
}