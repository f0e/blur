#include "window.h"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

bool os::window::disable_live_resize_scaling(SDL_Window* window) {
	if (!window)
		return false;

	// no arc, so no __bridge
	auto* ns_window = (NSWindow*)SDL_GetPointerProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr
	);
	if (!ns_window)
		return false;

	NSView* view = [ns_window contentView];
	if (!view)
		return false;

	// appkit stretches the last drawn frame while resizing. pinning it to the top left stops the stretching, and the
	// undrawn strip is the layer's black background
	view.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;

	// same for the layer, which is made lazily so it might not exist yet
	CALayer* layer = view.layer;
	if (layer) {
		layer.contentsGravity = kCAGravityTopLeft;
		layer.backgroundColor = [[NSColor blackColor] CGColor];
	}

	return true;
}
