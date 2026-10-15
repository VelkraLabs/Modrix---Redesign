// Carregamento de bibliotecas nativas: dlopen (POSIX/Android) ou LoadLibrary (Windows).
#pragma once

#include <filesystem>
#include <string>

namespace tlmod::rt::dynlib {

void* Open(const std::filesystem::path& path, std::string* error);
void* Symbol(void* handle, const char* name);
void Close(void* handle);

}  // namespace tlmod::rt::dynlib
