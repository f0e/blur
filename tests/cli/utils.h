#pragma once

const std::filesystem::path CURRENT_DIR = std::filesystem::path(__FILE__).parent_path();
const std::filesystem::path TEST_OUTPUT_DIR = CURRENT_DIR / "test_outputs";
const std::filesystem::path TEST_VIDEO_PATH = CURRENT_DIR / "../assets/test_video.mp4";

namespace test_utils {
	bool copy_test_video(const std::filesystem::path& path);

	bool create_empty_config_file(const std::filesystem::path& path);

	bool create_invalid_video_file(const std::filesystem::path& path);

	bool create_nested_output_path(const std::filesystem::path& base_dir, std::filesystem::path& output_path);

	std::tuple<std::filesystem::path, std::filesystem::path> create_test_files(const std::filesystem::path& dir);

	std::vector<std::filesystem::path> generate_output_paths(const std::filesystem::path& dir, size_t count);
}
