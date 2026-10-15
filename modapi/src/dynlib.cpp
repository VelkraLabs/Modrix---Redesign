#include "dynlib.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tlmod::rt::dynlib {

#if defined(_WIN32)

void* Open(const std::filesystem::path& path, std::string* error) {
    HMODULE h = LoadLibraryW(path.wstring().c_str());
    if (!h && error) *error = "LoadLibrary falhou (código " + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(h);
}
void* Symbol(void* handle, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}
void Close(void* handle) { FreeLibrary(static_cast<HMODULE>(handle)); }

#else

void* Open(const std::filesystem::path& path, std::string* error) {
    // RTLD_LOCAL: símbolos de um mod não vazam para outros mods.
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h && error) {
        const char* e = dlerror();
        *error = e ? e : "dlopen falhou";
    }
    return h;
}
void* Symbol(void* handle, const char* name) { return dlsym(handle, name); }
void Close(void* handle) { dlclose(handle); }

#endif

}  // namespace tlmod::rt::dynlib
