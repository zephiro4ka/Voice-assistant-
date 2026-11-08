#include "ggml-backend-dl.h"
#include "ggml-cpu.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

extern "C" {

float ggml_table_f32_f16[1 << 16];
float ggml_table_f32_e8m0_half[1 << 8];
float ggml_table_f32_ue4m3[1 << 8];

#ifdef _WIN32
dl_handle * dl_load_library(const std::filesystem::path & path) {
	return (dl_handle *) LoadLibraryW(path.wstring().c_str());
}

void * dl_get_sym(dl_handle * handle, const char * name) {
	return (void *) GetProcAddress((HMODULE) handle, name);
}

const char * dl_error(void) {
	static thread_local char buffer[512];
	DWORD err = GetLastError();
	if (err == 0) {
		return "no error";
	}
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, err, 0, buffer, sizeof(buffer), nullptr);
	return buffer;
}
#else
dl_handle * dl_load_library(const std::filesystem::path & path) {
	return (dl_handle *) dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
}

void * dl_get_sym(dl_handle * handle, const char * name) {
	return dlsym(handle, name);
}

const char * dl_error(void) {
	return dlerror();
}
#endif

int ggml_compute_forward_mul_mat_tiled(void * params, void * dst) {
	(void) params;
	(void) dst;
	return 0;
}

size_t ggml_tiled_wdata_size(size_t n_threads, const void * node) {
	(void) n_threads;
	(void) node;
	return 0;
}

} // extern "C"
