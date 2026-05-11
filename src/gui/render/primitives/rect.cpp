#include "point.h"
#include "size.h"
#include "rect.h"
#include "../render.h"
#include "../../ui/keys.h"

namespace gfx {
	bool Rect::hovered() const {
		return keys::mouse_pos.in_rect(*this);
	}

	float Rect::mouse_percent_x(bool uncapped) const {
		float x_offset = keys::mouse_pos.x - x;

		float percent = x_offset / w;
		if (!uncapped)
			percent = std::clamp(percent, 0.f, 1.f);

		return percent;
	}

	float Rect::mouse_percent_y(bool uncapped) const {
		float y_offset = keys::mouse_pos.y - y;

		float percent = y_offset / h;
		if (!uncapped)
			percent = std::clamp(percent, 0.f, 1.f);

		return percent;
	}

	bool Rect::on_screen() const {
		if (x2() < 0 || x > render::window_size.w)
			return false;

		if (y2() < 0 || y > render::window_size.h)
			return false;

		return true;
	}

	void Rect::clamp_to(const Rect& boundary) {
		// Clamp position
		x = std::max(x, boundary.x);
		y = std::max(y, boundary.y);

		// Adjust to fit within right/bottom boundaries
		if (x + w > boundary.x + boundary.w) {
			x = boundary.x + boundary.w - w;
		}

		if (y + h > boundary.y + boundary.h) {
			y = boundary.y + boundary.h - h;
		}

		// If still too large, reduce size to fit
		if (x < boundary.x) {
			w -= (boundary.x - x);
			x = boundary.x;
		}

		if (y < boundary.y) {
			h -= (boundary.y - y);
			y = boundary.y;
		}
	}
}
