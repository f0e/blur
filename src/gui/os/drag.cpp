#include "drag.h"

#ifndef __APPLE__

#	ifdef _WIN32

#		include <SDL3/SDL.h>
#		include <shlobj_core.h>
#		include <shlguid.h>

#		include "common/utils.h"

namespace {
	bool initialise_ole() {
		static bool initialised = [] {
			HRESULT hr = OleInitialize(nullptr);
			if (FAILED(hr)) {
				u::log_error("file drag: failed to initialise ole ({:#x})", (uint32_t)hr);
				return false;
			}
			return true;
		}();

		return initialised;
	}

	// custom drop source so renders can't be dropped back into blur
	class DropSource : public IDropSource {
	public:
		explicit DropSource(HWND window) : window(window) {}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
			if (!out)
				return E_POINTER;

			if (riid == IID_IUnknown || riid == IID_IDropSource) {
				*out = static_cast<IDropSource*>(this);
				AddRef();
				return S_OK;
			}

			*out = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override {
			return ++references;
		}

		ULONG STDMETHODCALLTYPE Release() override {
			ULONG remaining = --references;
			if (remaining == 0)
				delete this;

			return remaining;
		}

		HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape_pressed, DWORD key_state) override {
			if (escape_pressed || (key_state & MK_RBUTTON))
				return DRAGDROP_S_CANCEL;

			if (!(key_state & MK_LBUTTON))
				return over_source_window() ? DRAGDROP_S_CANCEL : DRAGDROP_S_DROP;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*effect*/) override {
			if (over_source_window()) {
				SetCursor(LoadCursor(nullptr, IDC_NO));
				return S_OK;
			}

			return DRAGDROP_S_USEDEFAULTCURSORS;
		}

	private:
		bool over_source_window() const {
			POINT cursor;
			if (!GetCursorPos(&cursor))
				return false;

			HWND under_cursor = WindowFromPoint(cursor);

			return under_cursor && GetAncestor(under_cursor, GA_ROOT) == window;
		}

		HWND window;
		std::atomic<ULONG> references = 1;
	};
}

bool os::drag::supported() {
	return true;
}

bool os::drag::begin_file_drag(SDL_Window* window, const std::filesystem::path& path) {
	if (!window)
		return false;

	if (!initialise_ole())
		return false;

	auto hwnd = static_cast<HWND>(
		SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)
	);
	if (!hwnd) {
		u::log_error("file drag: couldn't get the window handle");
		return false;
	}

	// let the shell build the data object so it behaves the same as dragging from explorer
	IShellItem* item = nullptr;
	HRESULT hr = SHCreateItemFromParsingName(path.wstring().c_str(), nullptr, IID_PPV_ARGS(&item));
	if (FAILED(hr)) {
		u::log_error("file drag: couldn't find '{}' ({:#x})", u::path_to_string(path), (uint32_t)hr);
		return false;
	}

	IDataObject* data_object = nullptr;
	hr = item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data_object));
	item->Release();

	if (FAILED(hr)) {
		u::log_error("file drag: couldn't build a data object for '{}' ({:#x})", u::path_to_string(path), (uint32_t)hr);
		return false;
	}

	// drag image, optional
	IDragSourceHelper* drag_helper = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&drag_helper)))) {
		drag_helper->InitializeFromWindow(nullptr, nullptr, data_object);
		drag_helper->Release();
	}

	DWORD effect = DROPEFFECT_COPY;

	auto* drop_source = new DropSource(hwnd);

	// blocks until the drag ends
	hr = SHDoDragDrop(hwnd, data_object, drop_source, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);

	drop_source->Release();
	data_object->Release();

	// sdl's drop target still queued events while the drag was over the window
	SDL_FlushEvents(SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_POSITION);

	return hr == DRAGDROP_S_DROP || hr == DRAGDROP_S_CANCEL;
}

#	else

bool os::drag::supported() {
	return false;
}

bool os::drag::begin_file_drag(SDL_Window* /*window*/, const std::filesystem::path& /*path*/) {
	// todo: xdnd
	return false;
}

#	endif

#endif
