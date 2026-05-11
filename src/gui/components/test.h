#pragma once

#include "../ui/ui.h"

namespace gui::components::test {
	void render_test_strings(const std::string& label, ui::Container& container, const render::Font& font);
	void screen(ui::Container& container, float delta_time);
}
