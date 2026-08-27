#include "drag.h"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>

// the session only holds onto its source weakly, so this lives for the rest of the process (see the static below)
@interface BlurFileDragSource : NSObject <NSDraggingSource>
@end

@implementation BlurFileDragSource

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
	sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
	// dropping a finished render back into blur would only queue it up as an input video, so the file is offered
	// to other apps and nothing else
	if (context == NSDraggingContextWithinApplication)
		return NSDragOperationNone;

	// it already exists on disk, so the only sensible thing another app can do with it is copy or link it
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
		// plain casts, not __bridge - the .mm files here are built without arc
		NSWindow* ns_window = (NSWindow*)SDL_GetPointerProperty(
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

		// appkit wants the event that started the drag. we're called from our own loop rather than out of an event
		// handler, so the event it has to hand is usually the mouse drag that got us here - if it's something else,
		// stand one in at the cursor
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

		// the file's icon, centred on the cursor
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

		static BlurFileDragSource* source = [[BlurFileDragSource alloc] init];

		NSDraggingSession* session = [view beginDraggingSessionWithItems:@[item] event:event source:source];

		[item release];

		return session != nil;
	}
}
