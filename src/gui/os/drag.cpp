#include "drag.h"

#ifndef __APPLE__

#	ifdef _WIN32

#		include <SDL3/SDL.h>
#		include <shlobj_core.h> // SHDoDragDrop and the drag image helper
#		include <shlguid.h>     // CLSID_DragDropHelper

#		include "common/utils.h"

namespace {
	// DoDragDrop needs ole on the calling thread. it's refcounted and we only ever want the one, so it's left
	// initialised for the rest of the process
	bool initialise_ole() {
		static bool initialised = [] {
			HRESULT hr = OleInitialize(nullptr);
			if (FAILED(hr)) {
				// the thread is already in the multithreaded apartment, which ole drag and drop can't use
				u::log_error("file drag: failed to initialise ole ({:#x})", (uint32_t)hr);
				return false;
			}
			return true;
		}();

		return initialised;
	}

	// the window we came from still accepts dropped files (that's how videos get added), and dropping a finished
	// render back into blur is never what anyone means by it. the default drop source has no say in where the
	// file can land, so we bring our own
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
				// let go: over our own window this is a drag that went nowhere, anywhere else takes the file
				return over_source_window() ? DRAGDROP_S_CANCEL : DRAGDROP_S_DROP;

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*effect*/) override {
			if (over_source_window()) {
				// blur's own drop target says yes to anything, so the no is ours to draw
				SetCursor(LoadCursor(nullptr, IDC_NO));
				return S_OK;
			}

			return DRAGDROP_S_USEDEFAULTCURSORS;
		}

	private:
		// whether the cursor is over the window the drag started from, rather than something in front of it
		bool over_source_window() const {
			POINT cursor;
			if (!GetCursorPos(&cursor))
				return false;

			// the drag image is click-through, so this is the window the drop would actually go to
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

	// let the shell build the data object for us - dropping it somewhere then behaves exactly like dropping a file
	// out of explorer, whatever format the target asked for
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

	// the file's icon/thumbnail under the cursor while it's being dragged. nothing breaks without it, the cursor
	// just carries the drop effect on its own
	IDragSourceHelper* drag_helper = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&drag_helper)))) {
		drag_helper->InitializeFromWindow(nullptr, nullptr, data_object); // null window: image comes from the file
		drag_helper->Release();
	}

	DWORD effect = DROPEFFECT_COPY;

	auto* drop_source = new DropSource(hwnd);

	// runs its own event loop until the file is dropped or the drag is cancelled - we're frozen until then
	hr = SHDoDragDrop(hwnd, data_object, drop_source, DROPEFFECT_COPY | DROPEFFECT_LINK, &effect);

	drop_source->Release();
	data_object->Release();

	// blur's own drop target saw the drag pass over the window and queued up events for it. the drop itself never
	// happens (see DropSource), but the rest of it is still ours to throw away
	SDL_FlushEvents(SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_POSITION);

	// cancelling is a normal way to end a drag, not a failure
	return hr == DRAGDROP_S_DROP || hr == DRAGDROP_S_CANCEL;
}

#	else

bool os::drag::supported() {
	return false;
}

bool os::drag::begin_file_drag(SDL_Window* /*window*/, const std::filesystem::path& /*path*/) {
	// xdnd needs the protocol implementing by hand against the native window, and sdl only does the receiving end
	return false;
}

#	endif

#endif
