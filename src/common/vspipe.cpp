#include "vspipe.h"
#include "config_app.h"

boost::process::environment vspipe::setup_environment() {
	auto env = boost::this_process::environment();

#ifdef _WIN32
	if (blur.used_installer)
		env["VAPOURSYNTH_EXTRA_PLUGIN_PATH"] = u::path_to_string(blur.resources_path / "lib/vapoursynth/vs-plugins");
#endif

#ifdef __APPLE__
	if (blur.used_installer) {
		env["PYTHONHOME"] = (blur.resources_path / "python").native();
		env["PYTHONPATH"] = (blur.resources_path / "python/lib/python3.12/site-packages").native();
	}
#endif

#ifdef __linux__
	auto app_config = config_app::get_app_config();
	if (!app_config.vapoursynth_lib_path.empty()) {
		env["LD_LIBRARY_PATH"] = app_config.vapoursynth_lib_path;
		env["PYTHONPATH"] = app_config.vapoursynth_lib_path + "/python3.12/site-packages";
	}
#endif

	return env;
}

std::vector<std::string> vspipe::get_args(
	const std::vector<std::string>& vspipe_flags,
	const std::string& script,
	const std::vector<std::string>& script_args,
	const std::string& output
) {
	std::vector<std::string> args = vspipe_flags;

	auto add_script_arg = [&](const std::string& arg) {
		args.emplace_back("-a");
		args.push_back(arg);
	};

	for (const auto& arg : script_args)
		add_script_arg(arg);

#if defined(__APPLE__)
	add_script_arg(std::format("macos_bundled={}", blur.used_installer ? "true" : "false"));
#elif defined(__linux__)
	bool bundled = std::filesystem::exists(blur.resources_path / "vapoursynth-plugins");
	add_script_arg(std::format("linux_bundled={}", bundled ? "true" : "false"));
#endif

	args.push_back(u::path_to_string(blur.resources_path / "lib" / script));
	args.push_back(output);

	return args;
}
