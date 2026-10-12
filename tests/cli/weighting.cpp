#include "tests.h"

#include "common/weighting.h"

// the render weights frames in python (src/vapoursynth/blur/weighting.py) and the gui previews them in c++, so this
// runs both over every weighting and sampling method and checks they agree

namespace {
	namespace bp = boost::process;

	const std::filesystem::path PYTHON_WEIGHTING_PATH = CURRENT_DIR / "../../src/vapoursynth/blur/weighting.py";

	// exposures in quarter frames up to 40 frames, which floats hold exactly
	constexpr int MAX_QUARTERS = 160;

	const std::vector<std::string> WEIGHTINGS = {
		"equal",     "cosine",     "gaussian_sym",     "pyramid", "gaussian",
		"ascending", "descending", "gaussian_reverse", "vegas",   "1,2,3",
	};

	// weighting.py imports blur.utils for its exception type, which pulls in vapoursynth, so it's stubbed out
	constexpr std::string_view PYTHON_SCRIPT = R"(
import importlib.util, json, sys, types

utils = types.ModuleType("blur.utils")
utils.BlurException = Exception
blur = types.ModuleType("blur")
blur.utils = utils
sys.modules["blur"] = blur
sys.modules["blur.utils"] = utils

spec = importlib.util.spec_from_file_location("weighting", sys.argv[1])
weighting = importlib.util.module_from_spec(spec)
spec.loader.exec_module(weighting)

with open(sys.argv[2]) as f:
    max_quarters, weightings, samplings, std_dev, mean, bound = json.load(f)
result = {}
for quarters in range(1, max_quarters + 1):
    for weighting_type in weightings:
        for sampling in samplings:
            weights = weighting.parse(1, quarters / 4, weighting_type, std_dev, mean, bound, sampling)
            result[f"{quarters}|{weighting_type}|{sampling}"] = list(weights)

print(json.dumps(result))
)";

	std::optional<std::filesystem::path> find_python() {
		if (const char* env = std::getenv("BLUR_TEST_PYTHON"))
			return env;

		for (const auto* name : { "python3", "python" }) {
			auto path = bp::search_path(name);
			if (path.empty())
				continue;

			// skip stubs that exist but can't run, like the windows store alias
			auto c = u::run_command(path.native(), { "--version" }, bp::std_out > bp::null, bp::std_err > bp::null);
			if (c) {
				c->wait();
				if (c->exit_code() == 0)
					return path.native();
			}
		}

		return {};
	}

	std::vector<double> get_cpp_weights(int quarters, const std::string& weighting_type, const std::string& sampling) {
		BlurSettings settings;
		settings.blur = true;
		settings.blur_amount = static_cast<float>(quarters) / 4;
		settings.blur_output_fps = 1;
		settings.blur_weighting = weighting_type;
		settings.blur_sampling = sampling;

		// one output fps and one video fps make the exposure the blur amount
		auto res = weighting::get_weights(settings, 1);
		EXPECT_TRUE(res.error.empty()) << res.error;

		return res.weights;
	}
}

TEST(Weighting, PythonMatchesCpp) {
	auto python = find_python();
	if (!python)
		GTEST_SKIP() << "no python found, set BLUR_TEST_PYTHON";

	const BlurSettings defaults;

	nlohmann::json args = {
		MAX_QUARTERS,
		WEIGHTINGS,
		config_blur::BLUR_SAMPLING_METHODS,
		defaults.advanced.blur_weighting_gaussian_std_dev,
		defaults.advanced.blur_weighting_gaussian_mean,
		nlohmann::json::parse(defaults.advanced.blur_weighting_gaussian_bound),
	};

	// passed as files since windows command line quoting mangles the quotes in them
	auto temp_dir = std::filesystem::temp_directory_path() / "blur-weighting-test";
	std::filesystem::create_directories(temp_dir);
	auto script_path = temp_dir / "weighting_dump.py";
	auto args_path = temp_dir / "args.json";
	std::ofstream(script_path) << PYTHON_SCRIPT;
	std::ofstream(args_path) << args.dump();

	bp::ipstream out_stream;
	bp::ipstream err_stream;

	auto c = u::run_command(
		*python,
		{ "-I",
	      u::path_to_string(script_path),
	      u::path_to_string(PYTHON_WEIGHTING_PATH),
	      u::path_to_string(args_path) },
		bp::std_out > out_stream,
		bp::std_err > err_stream
	);
	ASSERT_TRUE(c) << c.error();

	std::string output(std::istreambuf_iterator<char>(out_stream), {});
	std::string errors(std::istreambuf_iterator<char>(err_stream), {});
	c->wait();
	std::filesystem::remove_all(temp_dir);
	ASSERT_EQ(c->exit_code(), 0) << errors;

	auto python_weights = nlohmann::json::parse(output);

	for (int quarters = 1; quarters <= MAX_QUARTERS; quarters++) {
		for (const auto& weighting_type : WEIGHTINGS) {
			for (const auto& sampling : config_blur::BLUR_SAMPLING_METHODS) {
				auto key = std::format("{}|{}|{}", quarters, weighting_type, sampling);
				auto expected = python_weights.at(key).get<std::vector<double>>();
				auto actual = get_cpp_weights(quarters, weighting_type, sampling);

				ASSERT_EQ(actual.size(), expected.size()) << key;
				for (size_t i = 0; i < expected.size(); i++)
					EXPECT_NEAR(actual[i], expected[i], 1e-9) << key << " weight " << i;
			}
		}
	}
}
