#include "helpers.h"

#include "common/rendering/render_errors.h"

namespace {
	// with braces in its traceback, like real ones have
	std::string blur_exception(const std::string& message) {
		nlohmann::json error = {
			{ "error_type", "BlurException" },
			{ "user_message", message },
			{ "technical_details", "Traceback {\n  File \"blur.py\"\n}" },
		};
		return error.dump();
	}
}

TEST(RenderErrors, FindsTheBlurException) {
	auto output = "vspipe: script evaluation failed\n" + blur_exception("no frames") + "\nmore output";

	auto error = rendering::parse_error_output(output);

	ASSERT_TRUE(error);
	EXPECT_TRUE(error->is_blur_exception);
	EXPECT_EQ(error->user_message, "no frames");
	EXPECT_EQ(error->technical_details, "Traceback {\n  File \"blur.py\"\n}");
}

TEST(RenderErrors, TakesTheFirstOfSeveral) {
	// a failure in a frame callback is printed once for each frame in flight
	auto output = blur_exception("first") + blur_exception("second") + "\n" + blur_exception("third");

	auto error = rendering::parse_error_output(output);

	ASSERT_TRUE(error);
	EXPECT_EQ(error->user_message, "first");
}

TEST(RenderErrors, SkipsOtherJson) {
	auto output = R"({"frame": 1} {not json} )" + blur_exception("real");

	auto error = rendering::parse_error_output(output);

	ASSERT_TRUE(error);
	EXPECT_EQ(error->user_message, "real");
}

TEST(RenderErrors, GivesBackTheOutputWithoutABlurException) {
	std::string output = R"(Python exception: {"error_type": "ValueError"} {unterminated)";

	auto error = rendering::parse_error_output(output);

	ASSERT_FALSE(error);
	EXPECT_EQ(error.error(), output);
}

TEST(RenderErrors, StripsOnlyErrorObjects) {
	auto output = R"(before {"frame": 1} )" + blur_exception("gone") + " after";

	EXPECT_EQ(rendering::detail::without_error_objects(output), R"(before {"frame": 1}  after)");
}
