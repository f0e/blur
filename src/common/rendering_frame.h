#pragma once

#include "config.h"
#include "rendering.h"

class FrameRender {
	std::filesystem::path m_temp_path;
	bool m_to_kill = false;
	bool m_can_delete = false;

public:
	[[nodiscard]] bool can_delete() const {
		return m_can_delete;
	}

	void set_can_delete() {
		m_can_delete = true;
	}

	void stop();

	bool do_render(RenderCommands render_commands, const BlurSettings& settings);

	struct RenderResponse {
		bool success;
		std::filesystem::path output_path;
	};

	bool create_temp_path();
	bool remove_temp_path();

	RenderResponse render(const std::filesystem::path& input_path, const BlurSettings& settings);

	RenderCommands build_render_commands(
		const std::filesystem::path& input_path, const std::filesystem::path& output_path, const BlurSettings& settings
	);
};
