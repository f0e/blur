#include "configs.h"

#include "../../ui/ui.h"
#include "../../render/render.h"
#include "../../renderer.h"

namespace configs = gui::components::configs;

namespace {
	std::string make_unique_name(const std::vector<EncodingPresetSettings::Preset>& presets, const std::string& name) {
		auto taken = [&](const std::string& candidate) {
			return std::ranges::any_of(presets, [&](const EncodingPresetSettings::Preset& preset) {
				return u::to_lower(preset.name) == u::to_lower(candidate);
			});
		};

		if (!taken(name))
			return name;

		for (int i = 2;; i++) {
			std::string candidate = std::format("{} {}", name, i);
			if (!taken(candidate))
				return candidate;
		}
	}

	void add_preset(const std::string& gpu_type, const std::string& name, const std::string& args) {
		auto* presets = configs::encoding_preset_settings.find_preset_group(gpu_type);
		if (!presets)
			return;

		presets->emplace_back(
			EncodingPresetSettings::Preset{
				.name = make_unique_name(*presets, name),
				.args = args,
			}
		);
	}
}

void configs::encoding_preset_options(ui::Container& container) {
	std::vector<std::string> gpu_types;
	gpu_types.reserve(encoding_preset_settings.all_gpu_presets.size());
	for (const auto& gpu_presets : encoding_preset_settings.all_gpu_presets) {
		gpu_types.push_back(gpu_presets.gpu_type);
	}

	if (gpu_types.empty())
		return;

	if (!u::contains(gpu_types, selected_encoding_preset_gpu_type)) {
		// start on the device renders actually encode with
		std::string encoding_gpu_type = settings.gpu_encoding ? app_settings.gpu_type : "cpu";

		selected_encoding_preset_gpu_type =
			u::contains(gpu_types, encoding_gpu_type) ? encoding_gpu_type : gpu_types.front();
	}

	ui::add_text(
		"presets description",
		container,
		"encoding presets are the ffmpeg arguments used to encode renders. {quality} is replaced with the quality "
		"setting.",
		gfx::Color::white(renderer::MUTED_SHADE),
		fonts::dejavu
	);

	// devices without hardware encoding on this machine still get their presets shown (configs are shared between
	// machines), they're just greyed out
	auto available_gpu_types = u::get_available_gpu_types();
	std::vector<std::string> unavailable_gpu_types;
	for (const auto& gpu_type : gpu_types) {
		if (gpu_type != "cpu" && !u::contains(available_gpu_types, gpu_type))
			unavailable_gpu_types.push_back(gpu_type);
	}

	ui::add_dropdown(
		"preset device dropdown",
		container,
		"device",
		gpu_types,
		selected_encoding_preset_gpu_type,
		fonts::dejavu,
		{},
		unavailable_gpu_types
	);

	auto* presets = encoding_preset_settings.find_preset_group(selected_encoding_preset_gpu_type);
	if (!presets)
		return;

	bool first_section = true;
	auto section_component = [&](const std::string& id, const std::string& label) {
		section(container, first_section, id);
		ui::add_text(std::format("{} label", id), container, label, gfx::Color::white(), fonts::dejavu);
	};

	/*
	    Built-in presets
	*/
	section_component("built-in presets section", "built-in");

	bool any_defaults = false;

	for (size_t i = 0; i < presets->size(); i++) {
		const auto& preset = (*presets)[i];
		if (!preset.is_default)
			continue;

		any_defaults = true;

		std::string id = std::format("built-in preset {} {}", selected_encoding_preset_gpu_type, preset.name);

		// read only inputs rather than text so they can still be selected and copied
		container.push_element_gap(4);
		ui::add_text_input(
			std::format("{} name input", id),
			container,
			bind_read_only_input(std::format("{} name", id), preset.name),
			"",
			fonts::dejavu,
			"",
			{},
			true
		);
		container.pop_element_gap();

		ui::add_text_input(
			std::format("{} args input", id),
			container,
			bind_read_only_input(std::format("{} args", id), preset.args),
			"",
			fonts::dejavu,
			"",
			{},
			true
		);

		ui::add_spacing(container, 8);
	}

	if (!any_defaults) {
		ui::add_text(
			"no built-in presets text",
			container,
			std::format("no built-in presets for {}", selected_encoding_preset_gpu_type),
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu
		);
	}

	/*
	    Custom presets
	*/
	section_component("custom presets section", "custom");

	bool any_custom = false;

	for (size_t i = 0; i < presets->size(); i++) {
		auto& preset = (*presets)[i];
		if (preset.is_default)
			continue;

		std::string id = std::format("custom preset {} {}", selected_encoding_preset_gpu_type, i);

		if (any_custom)
			ui::add_separator(std::format("{} separator", id), container, ui::SeparatorStyle::FADE_RIGHT);

		any_custom = true;

		int delete_icon_size = ui::text_input_height(fonts::dejavu);

		container.push_element_gap(4);

		ui::add_text_input(
			std::format("{} name input", id),
			container,
			bind_input(std::format("{} name", id), preset.name),
			"",
			fonts::dejavu,
			"name",
			{},
			false,
			container.get_usable_rect().w - delete_icon_size - DELETE_ICON_GAP
		);

		ui::set_next_same_line(container);

		auto* delete_button = ui::add_icon_button(
			std::format("{} delete button", id),
			container,
			icons::CLOSE,
			fonts::icons,
			gfx::Size(delete_icon_size, delete_icon_size),
			DELETE_ICON_COLOR,
			DELETE_ICON_HOVER_COLOR,
			[gpu_type = selected_encoding_preset_gpu_type, i, name = preset.name, args = preset.args] {
				ui::dialog::confirm_destructive(
					"Remove preset?",
					name.empty() ? "This preset will be removed from the config."
								 : std::format("'{}' will be removed from the config.", name),
					"Remove",
					[gpu_type, i] {
						auto* group = encoding_preset_settings.find_preset_group(gpu_type);
						if (!group || i >= group->size() || (*group)[i].is_default)
							return;

						group->erase(group->begin() + i);
					},
					std::format("ffmpeg args: {}", args)
				);
			},
			"Remove preset"
		);

		ui::right_align_element(container, delete_button);

		container.pop_element_gap();

		std::optional<std::string> message;
		gfx::Color message_color = WARNING_COLOR;
		if (auto validation = config_encoding_presets::validate(*presets, i)) {
			message = validation->message;

			if (validation->is_error)
				message_color = ERROR_COLOR;
		}

		ui::add_with_message(container, std::format("{} message", id), message, message_color, [&] {
			ui::add_text_input(
				std::format("{} args input", id),
				container,
				bind_input(std::format("{} args", id), preset.args),
				"",
				fonts::dejavu,
				"ffmpeg arguments"
			);
		});

		ui::add_spacing(container, 8);
	}

	if (!any_custom) {
		ui::add_text(
			"no custom presets text",
			container,
			std::format("no custom presets for {} yet", selected_encoding_preset_gpu_type),
			gfx::Color::white(renderer::MUTED_SHADE),
			fonts::dejavu
		);
	}

	ui::add_button("add preset button", container, "Add preset", fonts::dejavu, [] {
		auto* group = encoding_preset_settings.find_preset_group(selected_encoding_preset_gpu_type);
		if (!group)
			return;

		// start from a built-in preset so it's a working command to edit rather than an empty box
		std::string args;
		for (const auto& preset : *group) {
			if (preset.is_default) {
				args = preset.args;
				break;
			}
		}

		add_preset(selected_encoding_preset_gpu_type, "new preset", args);
	});
}
