#pragma once

namespace rendering {
	struct RenderError {
		std::string user_message;
		std::string technical_details;
		bool is_blur_exception = false;
		std::string vspipe_errors;
		std::string ffmpeg_errors;

		[[nodiscard]] std::string to_string() const {
			std::string result = user_message;

			if (!technical_details.empty())
				result += "\n\n" + technical_details;
			if (!vspipe_errors.empty())
				result += "\n\n--- [vspipe] ---\n" + vspipe_errors;
			if (!ffmpeg_errors.empty())
				result += "\n\n--- [ffmpeg] ---\n" + ffmpeg_errors;

			return result;
		}
	};

	namespace detail {
		tl::expected<RenderError, std::string> parse_error_output(const std::string& stderr_output);

		std::string without_error_objects(const std::string& stderr_output);
	}
}
