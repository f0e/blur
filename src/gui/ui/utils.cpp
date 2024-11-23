#include "utils.h"

SkFont utils::create_font_from_data(const unsigned char* font_data, size_t data_size, float font_height) {
	sk_sp<SkData> skData = SkData::MakeWithCopy(font_data, data_size);

	// Create a typeface from SkData
	sk_sp<SkTypeface> typeface = SkTypeface::MakeFromData(skData);

	if (!typeface) {
		printf("failed to create font\n");
		return SkFont();
	}

	return SkFont(typeface, SkIntToScalar(font_height));
}
