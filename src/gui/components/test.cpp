#include "test.h"
#include "gui/renderer.h"

namespace test = gui::components::test;

void test::render_test_strings(const std::string& label, ui::Container& container, const render::Font& font) {
	static const gfx::Color section_color = gfx::Color::white(renderer::MUTED_SHADE);

	ui::add_text(std::format("{}-label", label), container, label, section_color, fonts::dejavu);

	ui::add_text(
		std::format("{}-test1", label),
		container,
		"Hello world! — Привет мир! — 你好，世界！— こんにちは世界！— 안녕하세요 세계!",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test2", label),
		container,
		"مرحبا بالعالم — שלום עולם — नमस्ते दुनिया — สวัสดีชาวโลก",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test3", label),
		container,
		"Γειά σου Κόσμε — Bonjour le monde — ¡Hola mundo! — Olá mundo!",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test4", label),
		container,
		"Zażółć gęślą jaźń — Český Krumlov — Straße — Göteborg",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test5", label),
		container,
		"Україна — България — Србија — საქართველო",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test6", label),
		container,
		"தமிழ் — తెలుగు — ಕನ್ನಡ — മലയാളം — සිංහල",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test7", label),
		container,
		"ᚠᛇᚻ (Runes) — ꦧꦱꦗꦮ (Javanese) — 한글 (Hangul Jamo)",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test8", label),
		container,
		"中文漢字 — 漢字仮名交じり文 — 한자",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test9", label),
		container,
		"😀 🚀 🍣 🧠 ❤️‍🔥 👩🏽‍💻 🏳️‍🌈",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(
		std::format("{}-test10", label),
		container,
		"Math & symbols: ∑ ∞ √ π ≈ ≠ ≤ ≥ → ⇄ ⌘ ⌥ ⌃",
		gfx::Color::white(),
		font,
		FONT_CENTERED_X
	);

	ui::add_text(std::format("{}-test11", label), container, "Zalgo: Z̷̛͌̎͋͊̈́̍̋͗̈́̅̐̐̄̑͂͑̾̈́̿̿͑̋̋̄̀̑̈́̅̇̇̑̄̈́̑̾́̍̎̾̓̈́̑̍̇̍̅̈́͗̈́̾̈́͌̄̿̿̅̇͗̿̍͊̾̎̓̑̍̄̀̄̑̽̈́̐̇̑̿̓̑͊̍̎̍̿̓̓̐̐̕̚̕̚̕̕̚͘̚̕̕̚̚̕̕̚̚̕̚̚̕̚̚͘͘̕̕̚̚̚͝͝͠͝͝͝͝͠", gfx::Color::white(), font, FONT_CENTERED_X);
}

void test::screen(ui::Container& container, float delta_time) {
	render_test_strings("dejavu", container, fonts::dejavu);
	render_test_strings("header", container, fonts::header_font);
	render_test_strings("smaller_header", container, fonts::smaller_header_font);
}
