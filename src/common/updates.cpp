#include "common/updates.h"

using json = nlohmann::json;
namespace bp = boost::process;

const std::string WINDOWS_INSTALLER_NAME = "blur-Windows-Installer-x64.exe";
const std::vector<std::string> WINDOWS_INSTALLER_ARGS = { "/UPDATE" };
const std::string MACOS_INSTALLER_NAME = "blur-macOS-Release-arm64.dmg";

namespace {
	bool is_version_newer(std::string current, std::string latest) {
		// Remove leading 'v' if present
		if (!current.empty() && current[0] == 'v') {
			current = current.substr(1);
		}
		if (!latest.empty() && latest[0] == 'v') {
			latest = latest.substr(1);
		}

		auto current_split = u::split_string(current, ".");
		auto latest_split = u::split_string(latest, ".");

		// Extract major version numbers
		int current_major = std::stoi(current_split[0]);
		int latest_major = std::stoi(latest_split[0]);

		// compare major versions
		if (current_major < latest_major)
			return true; // latest major version is newer. e.g. v2.x vs v1.x
		if (current_major > latest_major)
			return false; // ..

		// compare subversions
		if (current_split.size() == 1 && latest_split.size() > 1)
			return true; // latest version has a subversion where current does not. e.g. v2.1 vs v2
		if (latest_split.size() == 1 && current_split.size() > 1)
			return false; // ..

		size_t current_subversions = current_split[1].size();
		size_t latest_subversions = latest_split[1].size();
		size_t min_subversions = std::min(current_subversions, latest_subversions);
		for (size_t i = 0; i < min_subversions; i++) {
			char current_sub = current_split[1][i] - '0';
			char latest_sub = latest_split[1][i] - '0';

			if (current_sub < latest_sub)
				return true; // latest subversion is newer. e.g. v2.1 vs v2.2 or v2.1111 vs v2.1112
			if (current_sub > latest_sub)
				return false; // ..
		}

		// they're the same up to the point where one has more subversions
		return latest_subversions >
		       current_subversions; // latest is newer if more subversions. e.g. 2.111 > 2.11. note: yes, this will
		                            // happen for v2.0 vs v2 or v2.10 vs v2.1, but edge case, idc. will never happen.
	}
}

tl::expected<updates::UpdateCheckRes, std::string> updates::is_latest_version(bool include_beta) {
	std::string url = include_beta ? "https://api.github.com/repos/f0e/blur/releases"
	                               : "https://api.github.com/repos/f0e/blur/releases/latest";

	auto response = cpr::Get(cpr::Url{ url });

	if (response.status_code != 200) {
		u::log("Update check failed with status {}", response.status_code);
		return tl::unexpected("Update check failed");
	}

	try {
		std::string latest_tag;

		if (include_beta) {
			json releases = json::parse(response.text);

			if (releases.empty() || !releases.is_array()) {
				u::log("Update check failed: No releases found");
				return tl::unexpected("Update check failed");
			}

			// get most recent release (needs to have an installer, might make a release without one temporarily - don't
			// want anyone updating until i have)
			for (const auto& release : releases) {
				std::string release_tag = release["tag_name"];

				for (const auto& asset : release["assets"]) {
#if defined(_WIN32)
					if (asset["name"] == WINDOWS_INSTALLER_NAME) {
#elif defined(__linux__)
					// todo when there's an installer
					{
#elif defined(__APPLE__)
					if (asset["name"] == MACOS_INSTALLER_NAME) {
#endif
						// NOLINTBEGIN(readability-suspicious-call-argument) it's okay bro
						if (latest_tag.empty() || is_version_newer(latest_tag, release_tag)) {
							// NOLINTEND(readability-suspicious-call-argument)
							latest_tag = release_tag;
							break;
						}
					}
				}
			}
		}
		else {
			json release = json::parse(response.text);

			if (release.contains("tag_name")) {
				latest_tag = release["tag_name"];
			}
			else {
				u::log("Update check failed: Release information not found");
				return tl::unexpected("Update check failed");
			}
		}

		// remove 'v' prefix if it exists
		std::string latest_version_number = latest_tag;
		if (!latest_tag.empty() && latest_tag[0] == 'v') {
			latest_version_number = latest_tag.substr(1);
		}

		bool is_latest = !is_version_newer(BLUR_VERSION, latest_version_number);

		return updates::UpdateCheckRes{
			.is_latest = is_latest,
			.latest_tag = latest_tag,
			.latest_tag_url = "https://github.com/f0e/blur/releases/" + latest_tag,
		};
	}
	catch (const std::exception& e) {
		u::log("Failed to parse latest release JSON: {}", e.what());
		return tl::unexpected("Update check failed");
	}
}

bool updates::update_to_tag(
	const std::string& tag,
	const std::optional<std::function<void(const std::string& text, bool done)>>& progress_callback
) {
	try {
		u::log("Beginning update to tag: {}", tag);

		std::string installer_filename;
		std::filesystem::path installer_path;

#ifdef _WIN32
		installer_filename = WINDOWS_INSTALLER_NAME;
		installer_path = std::filesystem::temp_directory_path() / installer_filename;
#elif defined(__APPLE__)
		installer_filename = MACOS_INSTALLER_NAME;
		installer_path = std::filesystem::temp_directory_path() / installer_filename;
#else
		u::log("Unsupported platform for automatic updates");
		return false;
#endif

		// Create download URL
		std::string download_url = "https://github.com/f0e/blur/releases/download/" + tag + "/" + installer_filename;

		// Open file for writing
		std::ofstream installer_file(installer_path.string(), std::ios::binary);
		if (!installer_file.is_open()) {
			u::log("Failed to create installer file at {}", installer_path);
			return false;
		}

		// Setup download with progress tracking
		size_t total_bytes = 0;
		size_t downloaded_bytes = 0;
		float last_reported_progress = 0.0f;

		// Get file size first
		auto head_response = cpr::Head(cpr::Url{ download_url });
		if (head_response.status_code == 200 && head_response.header.contains("Content-Length")) {
			total_bytes = std::stoul(head_response.header["Content-Length"]);
			u::log("Total download size: {} bytes", total_bytes);
		}
		else {
			u::log("Unable to determine download size");
			total_bytes = static_cast<size_t>(80 * 1024 * 1024); // Fallback size 80mb
		}

		// Setup download session
		cpr::Session session;
		session.SetUrl(cpr::Url{ download_url });
		if (progress_callback) {
			session.SetWriteCallback(cpr::WriteCallback([&](const std::string_view& data, intptr_t userdata) -> bool {
				downloaded_bytes += data.size();
				installer_file.write(data.data(), data.size());

				float progress = static_cast<float>(downloaded_bytes) / static_cast<float>(total_bytes);
				if (progress - last_reported_progress >= 0.01f) {
					(*progress_callback)(std::format("Downloading update: {:.1f}%", progress * 100.f), false);
					last_reported_progress = progress;
				}

				return true;
			}));
		}

		// Execute download
		auto response = session.Get();
		installer_file.close();

		if (response.status_code != 200) {
			u::log("Download failed with status code: {}", response.status_code);
			return false;
		}

		// Complete progress
		if (progress_callback)
			(*progress_callback)("Update download complete", true);

		u::log("Download complete, launching installer");

#ifdef _WIN32
		bp::spawn(installer_path.string(), WINDOWS_INSTALLER_ARGS);
#elif defined(__APPLE__)
		bp::spawn("/usr/bin/open", installer_path.string());
#endif

		return true;
	}
	catch (const std::exception& e) {
		u::log("Update failed with exception: {}", e.what());
		return false;
	}
}

bool updates::update_to_latest(
	bool include_beta, const std::optional<std::function<void(const std::string& text, bool done)>>& progress_callback
) {
	auto check_result = is_latest_version(include_beta);
	if (!check_result || check_result->is_latest) {
		return false;
	}

	return update_to_tag(check_result->latest_tag, progress_callback);
}
