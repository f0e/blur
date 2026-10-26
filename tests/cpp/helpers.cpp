#include "helpers.h"

#if defined(_WIN32)
#	include <process.h>
#else
#	include <unistd.h>
#endif

namespace {
	// files that were just written can be locked for a moment, like by a virus scanner
	void remove_dir(const std::filesystem::path& dir) {
		for (int attempt = 0; attempt < 20; attempt++) {
			std::error_code ec;
			std::filesystem::remove_all(dir, ec);
			if (!ec)
				return;

			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}

	class OutputDirEnvironment : public ::testing::Environment {
	public:
		void TearDown() override {
			remove_dir(test_utils::output_dir());
		}
	};

	[[maybe_unused]] const auto* output_dir_environment =
		::testing::AddGlobalTestEnvironment(new OutputDirEnvironment);
}

const std::filesystem::path& test_utils::output_dir() {
	static const std::filesystem::path dir = [] {
#if defined(_WIN32)
		auto pid = _getpid();
#else
		auto pid = getpid();
#endif
		return std::filesystem::temp_directory_path() / std::format("blur-tests-{}", pid);
	}();

	return dir;
}

const std::filesystem::path& test_utils::test_clip() {
	static const std::filesystem::path clip = [] {
		auto path = output_dir() / "clip.mp4";
		std::filesystem::create_directories(path.parent_path());

		if (!blur.initialised) {
			auto init_res = blur.initialise(false, false);
			if (!init_res)
				throw std::runtime_error("blur failed to initialise: " + init_res.error());
		}

		namespace bp = boost::process;

		auto c = u::run_command(
			blur.ffmpeg_path,
			{
				"-y",
				"-v",
				"error",
				"-f",
				"lavfi",
				"-i",
				"testsrc2=s=160x90:r=30:d=0.5",
				"-f",
				"lavfi",
				"-i",
				"sine=frequency=1000:d=0.5",
				"-c:v",
				"libx264",
				"-preset",
				"ultrafast",
				"-pix_fmt",
				"yuv420p",
				"-c:a",
				"aac",
				u::path_to_string(path),
			},
			bp::std_out.null(),
			bp::std_err.null()
		);

		if (!c)
			throw std::runtime_error(c.error());

		c->wait();
		if (c->exit_code() != 0 || !std::filesystem::exists(path))
			throw std::runtime_error("ffmpeg couldn't make the test clip");

		return path;
	}();

	return clip;
}

BlurSettings test_utils::fast_config() {
	BlurSettings settings;
	settings.blur = false;
	settings.interpolate = false;
	settings.scene_detection = false;
	settings.frame_timing_logs = false;
	settings.deduplicate = false;
	settings.preview = false;
	settings.optimise_gpu_settings = false;
	settings.svp_hud_mask = false;
	return settings;
}

std::vector<std::filesystem::path> test_utils::generate_output_paths(const std::filesystem::path& dir, size_t count) {
	std::vector<std::filesystem::path> outputs;
	outputs.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		outputs.push_back(dir / ("output" + std::to_string(i + 1) + ".mp4"));
	}
	return outputs;
}

void TempDirTest::SetUp() {
	const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
	m_test_dir = test_utils::output_dir() / std::format("{}.{}", info->test_suite_name(), info->name());
	std::filesystem::remove_all(m_test_dir);
	std::filesystem::create_directories(m_test_dir);
}

void TempDirTest::TearDown() {
	remove_dir(m_test_dir);
}

void SettingsTest::SetUp() {
	TempDirTest::SetUp();

	// not the env var, which gpu drivers keep caches under too
	blur.settings_path = m_test_dir / "settings";
	std::filesystem::create_directories(blur.settings_path);
}

void CLITest::SetUp() {
	SettingsTest::SetUp();

	// renders without a config use the default one, so it's made fast too
	config_blur::save(std::string(config_blur::DEFAULT_CONFIG_NAME), test_utils::fast_config());

	m_test_config = m_test_dir / "config.cfg";
	config_blur::create(m_test_config, test_utils::fast_config());
}

std::filesystem::path CLITest::copy_test_clip(const std::string& name) {
	auto path = m_test_dir / name;
	std::filesystem::copy_file(test_utils::test_clip(), path, std::filesystem::copy_options::overwrite_existing);
	return path;
}
