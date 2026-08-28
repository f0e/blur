#include "window.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <deque>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

namespace {
id scroll_event_monitor = nil;
std::deque<os::window::ScrollEvent> native_scroll_events;
Uint32 native_scroll_event_type = 0;

bool phase_active(NSEventPhase phase) {
	return (phase & (NSEventPhaseBegan | NSEventPhaseChanged | NSEventPhaseStationary | NSEventPhaseMayBegin)) != 0;
}

NSEvent* track_scroll_event(NSEvent* event) {
	os::window::ScrollEvent scroll;
	scroll.precise = [event hasPreciseScrollingDeltas];
	if (scroll.precise) {
		// AppKit specifies these values in points, already adjusted for the user's natural-scroll preference.
		scroll.x = (float)-[event scrollingDeltaX];
		scroll.y = (float)[event scrollingDeltaY];
	}
	else {
		scroll.x = (float)-[event deltaX];
		scroll.y = (float)[event deltaY];
		scroll.x = scroll.x > 0.f ? std::ceil(scroll.x) : std::floor(scroll.x);
		scroll.y = scroll.y > 0.f ? std::ceil(scroll.y) : std::floor(scroll.y);
	}

	NSEventPhase phase = scroll.precise ? [event phase] : NSEventPhaseNone;
	NSEventPhase momentum_phase = scroll.precise ? [event momentumPhase] : NSEventPhaseNone;
	scroll.physical = phase_active(phase);
	scroll.momentum = phase_active(momentum_phase);

	// Queue phase-only packets too. Applying phase state when this SDL event is consumed keeps it paired with the
	// corresponding delta instead of allowing a later Cocoa event to change the meaning of an earlier queued one.
	bool phase_changed = phase != NSEventPhaseNone || momentum_phase != NSEventPhaseNone;
	if (scroll.x != 0.f || scroll.y != 0.f || phase_changed) {
		native_scroll_events.push_back(scroll);

		SDL_Event scroll_event{};
		scroll_event.type = native_scroll_event_type;
		scroll_event.user.timestamp = SDL_GetTicksNS();
		if (!SDL_PushEvent(&scroll_event))
			native_scroll_events.pop_back();
	}

	return event;
}
} // namespace

bool os::window::initialise_scroll_tracking() {
	if (scroll_event_monitor)
		return true;

	native_scroll_event_type = SDL_RegisterEvents(1);
	if (native_scroll_event_type == 0)
		return false;

	scroll_event_monitor =
		[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
											  handler:^NSEvent*(NSEvent* event) { return track_scroll_event(event); }];
	return scroll_event_monitor != nil;
}

void os::window::cleanup_scroll_tracking() {
	if (scroll_event_monitor) {
		[NSEvent removeMonitor:scroll_event_monitor];
		scroll_event_monitor = nil;
	}

	native_scroll_events.clear();
	native_scroll_event_type = 0;
}

bool os::window::consume_native_scroll_event(const SDL_Event& event, ScrollEvent& scroll) {
	if (native_scroll_event_type == 0 || event.type != native_scroll_event_type)
		return false;

	if (native_scroll_events.empty())
		return true;

	scroll = native_scroll_events.front();
	native_scroll_events.pop_front();
	return true;
}

bool os::window::disable_live_resize_scaling(SDL_Window* window) {
	if (!window)
		return false;

	// plain cast, not __bridge - the .mm files here are built without arc
	NSWindow* ns_window = (NSWindow*)SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr
	);
	if (!ns_window)
		return false;

	NSView* view = [ns_window contentView];
	if (!view)
		return false;

	// appkit keeps showing the last frame we drew while the window's being dragged, and NSViewLayerContents-
	// PlacementScaleAxesIndependently (the default) stretches it to the window's new size - that's the jelly.
	// pinning it to the top left means it's never distorted; the strip we haven't drawn into yet just stays
	// the layer's background colour, which is the same black the ui draws on anyway
	view.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;

	// same thing a level down, for when the layer is the one being scaled. it's made lazily, so it isn't
	// necessarily here yet - the placement above is the part that matters and it sticks either way
	CALayer* layer = view.layer;
	if (layer) {
		layer.contentsGravity = kCAGravityTopLeft;
		layer.backgroundColor = [[NSColor blackColor] CGColor];
	}

	return true;
}
