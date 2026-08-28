#include "taskbar.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>

// the dock has no progress bar of its own, so the icon's badge carries the percentage instead
void os::taskbar::initialise(SDL_Window* /*window*/) {}

void os::taskbar::set_progress(ProgressState state, float progress) {
	NSDockTile* tile = [[NSApplication sharedApplication] dockTile];
	if (!tile)
		return;

	NSString* badge = nil;

	switch (state) {
		case ProgressState::NONE:
			break;

		case ProgressState::INDETERMINATE:
			badge = @"…";
			break;

		case ProgressState::PAUSED:
		case ProgressState::NORMAL:
		case ProgressState::ERRORED: {
			int percent = (int)std::lround(std::clamp(progress, 0.f, 1.f) * 100.f);
			badge = [NSString stringWithFormat:@"%d%%", percent];
			break;
		}
	}

	static NSString* last_badge = nil;
	if (badge == last_badge || (badge && last_badge && [badge isEqualToString:last_badge]))
		return;

	[last_badge release];
	last_badge = [badge retain];

	[tile setBadgeLabel:badge];
	[tile display];
}

void os::taskbar::cleanup() {
	set_progress(ProgressState::NONE);
}
