#include "render_errors.h"

namespace {
	// where the json object starting at `start` ends, or npos if it never does. strings are tracked so that a
	// brace inside one - blur's exception blobs carry whole python tracebacks - doesn't throw the count off
	size_t find_object_end(const std::string& text, size_t start) {
		int depth = 0;
		bool in_string = false;
		bool escaped = false;

		for (size_t i = start; i < text.size(); i++) {
			char c = text[i];

			if (in_string) {
				if (escaped)
					escaped = false;
				else if (c == '\\')
					escaped = true;
				else if (c == '"')
					in_string = false;
				continue;
			}

			if (c == '"')
				in_string = true;
			else if (c == '{')
				depth++;
			else if (c == '}' && --depth == 0)
				return i;
		}

		return std::string::npos;
	}
}

std::string rendering::detail::without_error_objects(const std::string& stderr_output) {
	std::string kept;
	kept.reserve(stderr_output.size());

	size_t copied = 0;
	for (size_t start = stderr_output.find('{'); start != std::string::npos; start = stderr_output.find('{', start + 1))
	{
		if (start < copied) // inside a blob that's already been skipped
			continue;

		size_t end = find_object_end(stderr_output, start);
		if (end == std::string::npos)
			break;

		auto json = nlohmann::json::parse(
			stderr_output.begin() + static_cast<std::ptrdiff_t>(start),
			stderr_output.begin() + static_cast<std::ptrdiff_t>(end) + 1,
			nullptr,
			false
		);

		if (json.is_discarded() || !json.is_object() || !json.contains("error_type"))
			continue;

		kept.append(stderr_output, copied, start - copied);
		copied = end + 1;
	}

	kept.append(stderr_output, copied, std::string::npos);

	return kept;
}

tl::expected<rendering::RenderError, std::string> rendering::detail::parse_error_output(
	const std::string& stderr_output
) {
	RenderError result;
	result.is_blur_exception = false;

	// blur's scripts print one of these json blobs per failure, and a failure raised inside a frame callback
	// gets printed once for every frame vapoursynth had in flight - so there can be several of them in here,
	// back to back, surrounded by vspipe's own output. each is taken on its own: handing the whole span from
	// the first brace to the last to the parser is a syntax error, and the first blur exception in it is the
	// one that started everything anyway.
	// nothing here throws - a render failing is not the moment to risk taking the app down with it
	for (size_t start = stderr_output.find('{'); start != std::string::npos; start = stderr_output.find('{', start + 1))
	{
		size_t end = find_object_end(stderr_output, start);
		if (end == std::string::npos)
			break;

		auto json = nlohmann::json::parse(
			stderr_output.begin() + static_cast<std::ptrdiff_t>(start),
			stderr_output.begin() + static_cast<std::ptrdiff_t>(end) + 1,
			nullptr,
			false
		);

		if (json.is_discarded() || !json.is_object())
			continue;

		if (json.contains("error_type") && json["error_type"] == "BlurException") {
			result.is_blur_exception = true;
			result.user_message = json.value("user_message", "An error occurred during processing");
			result.technical_details = json.value("technical_details", stderr_output);
			return result;
		}
	}

	return tl::unexpected(stderr_output);
}
