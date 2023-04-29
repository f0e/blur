#include "s_point.h"
#include "s_rect.h"

bool s_point::in_rect(const s_rect& r) const {
	return x >= r.x && y >= r.y && x <= r.x + r.w && y <= r.y + r.h;
}

s_size s_point::to_size() const {
    return { x, y };
}

s_point& s_point::operator+=(const s_size& v) {
	x += v.w;
	y += v.h;
	return *this;
}

s_point& s_point::operator-=(const s_size& v) {
	x -= v.w;
	y -= v.h;
	return *this;
}
