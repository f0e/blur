#include "s_colour.h"

uint32_t s_colour::convert() const {
	auto ret = *this;

	// colour override
	if (!drawing::alphas.empty())
		ret = ret.adjust_alpha(drawing::alphas.top());

	return ret.to_imgui();
}