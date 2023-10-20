#pragma once

struct s_rect {
public:
	int x, y, w, h;

public:
	constexpr s_rect()
			: x{ 0 }, y{ 0 }, w{ 0 }, h{ 0 } {}

	constexpr s_rect(int x, int y, int w, int h)
			: x(x), y(y), w(w), h(h) {}

	constexpr s_rect(s_point pos)
			: x(pos.x), y(pos.y), w(0), h(0) {}

	constexpr s_rect(s_point pos, s_size size)
			: x(pos.x), y(pos.y), w(size.w), h(size.h) {}

	s_rect(s_point pos1, s_point pos2) {
		x = std::min(pos1.x, pos2.x);
		y = std::min(pos1.y, pos2.y);

		w = abs(pos2.x - pos1.x);
		h = abs(pos2.y - pos1.y);
	}

public:
	constexpr inline bool is_zero() const {
		return !x && !y && !w && !h;
	}

	s_point pos() const {
		return { x, y };
	}

	void set_pos(const s_point& p) {
		x = p.x;
		y = p.y;
	}

	void add_pos(const s_point& p) {
		x += p.x;
		y += p.y;
	}

	s_size size() const {
		return { w, h };
	}

	void set_size(const s_size& s) {
		w = s.w;
		h = s.h;
	}

	void set_size(int s) {
		w = s;
		h = s;
	}

	void add_size(const s_size& s) {
		w += s.w;
		h += s.h;
	}

	int x2() const {
		return x + w;
	}

	int y2() const {
		return y + h;
	}

	s_point max() const {
		return { x2(), y2() };
	}

	s_point center() const {
		return { x + w / 2, y + h / 2 };
	}

	void vertical_cut(int v, bool centered = false) {
		y += v;
		if (centered)
			h -= v * 2;
		else
			h -= v;
	}

	void horizontal_cut(int v, bool centered = false) {
		x += v;
		if (centered)
			w -= v * 2;
		else
			w -= v;
	}

	s_rect expand(s_size size, bool centered = true) const {
		if (centered) {
			return {
				x - size.w / 2,
				y - size.h / 2,
				w + size.w,
				h + size.h
			};
		} else {
			return {
				x,
				y,
				w + size.w,
				h + size.h
			};
		}
	}

	s_rect shrink(s_size size, bool centered = true) const {
		return expand({ -size.w, -size.h }, centered);
	}

	s_rect expand(int amount, bool centered = true) const {
		if (centered) {
			return {
				x - amount,
				y - amount,
				w + amount * 2,
				h + amount * 2
			};
		} else {
			return {
				x,
				y,
				w + amount,
				h + amount
			};
		}
	}

	s_rect shrink(int amount, bool centered = true) const {
		return expand(-amount, centered);
	}

	s_rect offset(int amt) {
		s_rect tmp = *this;
		tmp.x += amt;
		tmp.y += amt;
		return tmp;
	}
};

#define IM_VEC4_CLASS_EXTRA                                                                 \
        ImVec4(const s_rect& f) : x(f.x), y(f.y), z(f.max().x), w(f.max().y) {}   \
        operator s_rect() const { return s_rect((int)x, (int)y, (int)z, (int)w); }
