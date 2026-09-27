#include "drag.h"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>

@interface BlurFileDragSource : NSObject <NSDraggingSource>
@end

@implementation BlurFileDragSource

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
	sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
	// don't allow dropping renders back into blur
	if (context == NSDraggingContextWithinApplication)
		return NSDragOperationNone;

	return NSDragOperationCopy | NSDragOperationLink;
}

@end

bool os::drag::supported() {
	return true;
}

bool os::drag::begin_file_drag(SDL_Window* window, const std::filesystem::path& path) {
	if (!window)
		return false;

	@autoreleasepool {
		// no arc, so no __bridge
		auto* ns_window = (NSWindow*)SDL_GetPointerProperty(
			SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr
		);
		if (!ns_window)
			return false;

		NSView* view = [ns_window contentView];
		if (!view)
			return false;

		NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
		if (!ns_path)
			return false;

		NSURL* url = [NSURL fileURLWithPath:ns_path];
		if (!url)
			return false;

		NSPoint location = [ns_window mouseLocationOutsideOfEventStream];

		// appkit wants the event that started the drag, so fake one if the current event isn't a mouse drag
		NSEvent* event = [NSApp currentEvent];
		if (!event || (event.type != NSEventTypeLeftMouseDragged && event.type != NSEventTypeLeftMouseDown)) {
			event = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
									   location:location
								  modifierFlags:0
									  timestamp:[[NSProcessInfo processInfo] systemUptime]
								   windowNumber:[ns_window windowNumber]
										context:nil
									eventNumber:0
									 clickCount:1
									   pressure:1.f];

			if (!event)
				return false;
		}

		NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];

		NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:ns_path];
		NSSize size = icon ? [icon size] : NSMakeSize(64.f, 64.f);
		NSPoint view_location = [view convertPoint:location fromView:nil];

		[item setDraggingFrame:NSMakeRect(
								   view_location.x - (size.width / 2.f),
								   view_location.y - (size.height / 2.f),
								   size.width,
								   size.height
							   )
					  contents:icon];

		// the session only holds a weak reference to its source
		static BlurFileDragSource* source = [[BlurFileDragSource alloc] init];

		NSDraggingSession* session = [view beginDraggingSessionWithItems:@[item] event:event source:source];

		[item release];

		return session != nil;
	}
}
