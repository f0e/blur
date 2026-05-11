#include "utils.h"

bool test_utils::copy_test_video(const std::filesystem::path& path) {
	assert(std::filesystem::exists(TEST_VIDEO_PATH));

	try {
		std::filesystem::copy_file(TEST_VIDEO_PATH, path, std::filesystem::copy_options::overwrite_existing);
		return true;
	}
	catch (...) {
		return false;
	}
}

bool test_utils::create_empty_config_file(const std::filesystem::path& path) {
	try {
		std::ofstream file(path);
		return true;
	}
	catch (...) {
		return false;
	}
}

bool test_utils::create_invalid_video_file(const std::filesystem::path& path) {
	try {
		std::ofstream file(path);
		file << "This is not a valid video file";
		return true;
	}
	catch (...) {
		return false;
	}
}

bool test_utils::create_nested_output_path(const std::filesystem::path& base_dir, std::filesystem::path& output_path) {
	output_path = base_dir / "nested" / "directories" / "output.mp4";
	return true;
}

// Creates a standard test environment with video and config files
std::tuple<std::filesystem::path, std::filesystem::path> test_utils::create_test_files(
	const std::filesystem::path& dir
) {
	auto video = dir / "test.mp4";
	auto config = dir / "config.cfg";
	copy_test_video(video);
	create_empty_config_file(config);
	return { video, config };
}

std::vector<std::filesystem::path> test_utils::generate_output_paths(const std::filesystem::path& dir, size_t count) {
	std::vector<std::filesystem::path> outputs;
	outputs.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		outputs.push_back(dir / ("output" + std::to_string(i + 1) + ".mp4"));
	}
	return outputs;
}
