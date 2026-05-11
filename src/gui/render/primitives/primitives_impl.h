#pragma once

#include "point.h"
#include "size.h"
#include "rect.h"

constexpr int abs_constexpr(int x) { // todo: c++23 fixes this
	return x < 0 ? -x : x;
}

namespace gfx {
	// Point implementations that depend on other classes
	constexpr Size Point::to_size() const {
		return { x, y };
	}

	constexpr Point& Point::operator+=(const Size& size) {
		x += size.w;
		y += size.h;
		return *this;
	}

	constexpr Point Point::operator+(const Size& size) const {
		Point result = *this;
		result += size;
		return result;
	}

	constexpr Point& Point::operator-=(const Size& size) {
		x -= size.w;
		y -= size.h;
		return *this;
	}

	constexpr Point Point::operator-(const Size& size) const {
		Point result = *this;
		result -= size;
		return result;
	}

	constexpr bool Point::in_rect(const Rect& rect) const {
		return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
	}

	// Size implementations that depend on other classes
	constexpr Point Size::to_point() const {
		return { w, h };
	}

	// Rect implementations that depend on other classes
	constexpr Rect::Rect(const Point& position, const Size& size)
		: x(position.x), y(position.y), w(size.w), h(size.h) {}

	constexpr Rect::Rect(const Point& p1, const Point& p2)
		: x(std::min(p1.x, p2.x)), y(std::min(p1.y, p2.y)), w(abs_constexpr(p2.x - p1.x)),
		  h(abs_constexpr(p2.y - p1.y)) {}

	constexpr Point Rect::position() const {
		return { x, y };
	}

	constexpr void Rect::set_position(const Point& pos) {
		x = pos.x;
		y = pos.y;
	}

	constexpr void Rect::add_position(const Point& offset) {
		x += offset.x;
		y += offset.y;
	}

	constexpr Size Rect::size() const {
		return { w, h };
	}

	constexpr void Rect::set_size(const Size& sz) {
		w = sz.w;
		h = sz.h;
	}

	constexpr void Rect::add_size(const Size& sz) {
		w += sz.w;
		h += sz.h;
	}

	constexpr Point Rect::top_left() const {
		return { x, y };
	}

	constexpr Point Rect::top_right() const {
		return { x + w, y };
	}

	constexpr Point Rect::bottom_left() const {
		return { x, y + h };
	}

	constexpr Point Rect::bottom_right() const {
		return { x + w, y + h };
	}

	constexpr Point Rect::top_center() const {
		return { x + (w / 2), y };
	}

	constexpr Point Rect::bottom_center() const {
		return { x + (w / 2), y + h };
	}

	constexpr Point Rect::left_center() const {
		return { x, y + (h / 2) };
	}

	constexpr Point Rect::right_center() const {
		return { x + w, y + (h / 2) };
	}

	constexpr Point Rect::center() const {
		return { x + (w / 2), y + (h / 2) };
	}

	constexpr bool Rect::contains(const Point& pt) const {
		return pt.x >= x && pt.x < x + w && pt.y >= y && pt.y < y + h;
	}

	constexpr Rect Rect::expand(const Size& amount, bool centered) const {
		if (centered) {
			return { x - (amount.w / 2), y - (amount.h / 2), w + amount.w, h + amount.h };
		}
		return { x, y, w + amount.w, h + amount.h };
	}

	constexpr Rect Rect::shrink(const Size& amount, bool centered) const {
		return expand(Size{ -amount.w, -amount.h }, centered);
	}

	constexpr Rect& Rect::operator+=(const Point& offset) {
		x += offset.x;
		y += offset.y;
		return *this;
	}

	constexpr Rect& Rect::operator-=(const Point& offset) {
		x -= offset.x;
		y -= offset.y;
		return *this;
	}

	constexpr Rect Rect::operator+(const Point& offset) const {
		Rect result = *this;
		result += offset;
		return result;
	}

	constexpr Rect Rect::operator-(const Point& offset) const {
		Rect result = *this;
		result -= offset;
		return result;
	}
}
