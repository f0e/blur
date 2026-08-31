#include "clipboard.h"

#import <AppKit/AppKit.h>

bool os::clipboard::copy_file(const std::filesystem::path& path) {
	NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
	if (!ns_path)
		return false;

	NSURL* url = [NSURL fileURLWithPath:ns_path];
	if (!url)
		return false;

	NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
	[pasteboard clearContents];

	return [pasteboard writeObjects:@[url]];
}
