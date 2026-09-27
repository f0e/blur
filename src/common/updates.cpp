#include "common/updates.h"

using json = nlohmann::json;
namespace bp = boost::process;

const std::string WINDOWS_INSTALLER_NAME = "blur-Windows-Installer-x64.exe";
const std::vector<std::string> WINDOWS_INSTALLER_ARGS = { "/UPDATE" };
const std::string MACOS_INSTALLER_NAME = "blur-macOS-Release-arm64.dmg";
const std::string LINUX_APPIMAGE_NAME = "blur-Linux-x64.AppImage";

namespace {
	// semver
	std::optional<std::vector<int>> parse_version(std::string_view version) {
		if (version.starts_with('v'))
			version.remove_prefix(1);

		std::vector<int> parts;

		try {
			for (const auto& part : u::split_string(std::string(version), "."))
				parts.push_back(std::stoi(part));
		}
		catch (const std::exception&) {
			return {};
		}

		return parts;
	}
}

bool updates::is_version_newer(std::string_view current, std::string_view latest) {
	auto current_version = parse_version(current);
	auto latest_version = parse_version(latest);
	if (!current_version || !latest_version)
		return false;

	return *current_version < *latest_version;
}

tl::expected<updates::UpdateCheckRes, std::string> updates::is_latest_version(bool include_beta) {
	std::string url = "https://api.github.com/repos/f0e/blur/releases";

	auto response = cpr::Get(cpr::Url{ url });

	if (response.status_code != 200) {
		u::log("Update check failed with status {}", response.status_code);
		return tl::unexpected("Update check failed");
	}

	try {
		std::string latest_tag;

		json releases = json::parse(response.text);

		if (releases.empty() || !releases.is_array()) {
			u::log("Update check failed: No releases found");
			return tl::unexpected("Update check failed");
		}

		// get most recent release (needs to have an installer, might make a release without one temporarily - don't
		// want anyone updating until i have)
		for (const auto& release : releases) {
			bool is_prerelease = release["prerelease"];
			if (!include_beta && is_prerelease)
				continue;

			std::string release_tag = release["tag_name"];

			for (const auto& asset : release["assets"]) {
#if defined(_WIN32)
				if (asset["name"] == WINDOWS_INSTALLER_NAME) {
#elif defined(__linux__)
				if (asset["name"] == LINUX_APPIMAGE_NAME) {
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

		if (latest_tag.empty()) {
			u::log("Update check failed: No suitable release found");
			return tl::unexpected("Update check failed");
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
			.latest_tag_url = "https://github.com/f0e/blur/releases/tag/" + latest_tag,
		};
	}
	catch (const std::exception& e) {
		u::log("Failed to parse latest release JSON: {}", e.what());
		return tl::unexpected("Update check failed");
	}
}

bool updates::update_to_tag(
	const std::string& tag,
	const std::optional<ProgressCallback>& progress_callback,
	const std::optional<CancelCallback>& cancel_callback
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
		std::ofstream installer_file(installer_path, std::ios::binary);
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
		bool cancelled = false;

		cpr::Session session;
		session.SetUrl(cpr::Url{ download_url });
		session.SetWriteCallback(cpr::WriteCallback([&](const std::string_view& data, intptr_t userdata) -> bool {
			if (cancel_callback && (*cancel_callback)()) {
				cancelled = true;
				return false; // returning false aborts the transfer
			}

			downloaded_bytes += data.size();
			installer_file.write(data.data(), data.size());

			if (progress_callback) {
				float progress = static_cast<float>(downloaded_bytes) / static_cast<float>(total_bytes);
				if (progress - last_reported_progress >= 0.01f) {
					(*progress_callback)(std::format("Downloading update: {:.1f}%", progress * 100.f), progress, false);
					last_reported_progress = progress;
				}
			}

			return true;
		}));

		// Execute download
		auto response = session.Get();
		installer_file.close();

		if (cancelled) {
			u::log("Update download cancelled");

			std::error_code ec;
			std::filesystem::remove(installer_path, ec); // don't leave the half-downloaded installer lying around

			return false;
		}

		if (response.status_code != 200) {
			u::log("Download failed with status code: {}", response.status_code);
			return false;
		}

		// Complete progress
		if (progress_callback)
			(*progress_callback)("Update download complete", 1.f, true);

		u::log("Download complete, launching installer");

#ifdef _WIN32
		bp::spawn(installer_path.native(), WINDOWS_INSTALLER_ARGS);
#elif defined(__APPLE__)
		bp::spawn("/usr/bin/open", installer_path.native());
#endif

		return true;
	}
	catch (const std::exception& e) {
		u::log("Update failed with exception: {}", e.what());
		return false;
	}
}

bool updates::update_to_latest(
	bool include_beta,
	const std::optional<ProgressCallback>& progress_callback,
	const std::optional<CancelCallback>& cancel_callback
) {
	auto check_result = is_latest_version(include_beta);
	if (!check_result || check_result->is_latest) {
		return false;
	}

	return update_to_tag(check_result->latest_tag, progress_callback, cancel_callback);
}
