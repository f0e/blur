#include "window.h"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

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
