#include "file_browser.h"

#import <AppKit/AppKit.h>

bool os::file_browser::reveal_file(const std::filesystem::path& path) {
	NSString* ns_path = [NSString stringWithUTF8String:path.c_str()];
	if (!ns_path)
		return false;

	NSURL* url = [NSURL fileURLWithPath:ns_path];
	if (!url)
		return false;

	[[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[url]];
	return true;
}
