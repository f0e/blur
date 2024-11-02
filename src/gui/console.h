#pragma once

namespace console {
	inline bool initialised = false;
	inline FILE* stream = nullptr;

	bool init();
	bool close();

#ifdef _WIN32
	void cleanup();
#endif
}
