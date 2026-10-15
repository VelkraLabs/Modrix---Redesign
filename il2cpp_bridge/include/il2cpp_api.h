// Camada mínima sobre a API C exportada pela libil2cpp.so.
//
// Nada aqui depende de headers da Unity: os tipos são opacos e as funções são
// resolvidas em tempo de execução via dlopen/dlsym. Todos os símbolos listados
// em IL2CPP_API_LIST foram conferidos na tabela de exports (readelf --dyn-syms)
// da libil2cpp.so do Terraria Mobile (Unity 2021.3.56f2, arm64-v8a).
#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppThread;
struct Il2CppArray;
struct FieldInfo;
struct Il2CppObject;
typedef Il2CppObject Il2CppException;

// Só o primeiro campo de MethodInfo é usado pela ponte. O layout completo está
// no il2cpp.h gerado pelo Il2CppDumper (struct MethodInfo, linha ~117).
typedef void (*Il2CppMethodPointer)();
struct MethodInfo {
    Il2CppMethodPointer methodPointer;
    // ... demais campos omitidos de propósito (virtualMethodPointer, invoker_method, name, klass ...)
};

}  // extern "C"

// X-macro: (tipo de retorno, nome do export, lista de parâmetros)
#define IL2CPP_API_LIST(X)                                                                  \
    X(const Il2CppImage*, il2cpp_get_corlib, (void))                                        \
    X(Il2CppDomain*, il2cpp_domain_get, (void))                                             \
    X(const Il2CppAssembly**, il2cpp_domain_get_assemblies, (const Il2CppDomain*, size_t*)) \
    X(const Il2CppImage*, il2cpp_assembly_get_image, (const Il2CppAssembly*))               \
    X(const char*, il2cpp_image_get_name, (const Il2CppImage*))                             \
    X(Il2CppClass*, il2cpp_class_from_name, (const Il2CppImage*, const char*, const char*)) \
    X(FieldInfo*, il2cpp_class_get_field_from_name, (Il2CppClass*, const char*))            \
    X(size_t, il2cpp_field_get_offset, (FieldInfo*))                                        \
    X(void, il2cpp_field_static_get_value, (FieldInfo*, void*))                             \
    X(const MethodInfo*, il2cpp_class_get_method_from_name, (Il2CppClass*, const char*, int)) \
    X(Il2CppObject*, il2cpp_runtime_invoke,                                                 \
      (const MethodInfo*, void*, void**, Il2CppException**))                                \
    X(void*, il2cpp_object_unbox, (Il2CppObject*))                                          \
    X(uint32_t, il2cpp_array_length, (Il2CppArray*))                                        \
    X(Il2CppThread*, il2cpp_thread_attach, (Il2CppDomain*))                                 \
    X(Il2CppThread*, il2cpp_thread_current, (void))                                         \
    X(void, il2cpp_thread_detach, (Il2CppThread*))                                          \
    X(void, il2cpp_format_exception, (const Il2CppException*, char*, int))

namespace il2cpp {

struct Api {
#define IL2CPP_DECLARE_PTR(ret, name, args) ret(*name) args = nullptr;
    IL2CPP_API_LIST(IL2CPP_DECLARE_PTR)
#undef IL2CPP_DECLARE_PTR
};

// Tabela global preenchida por LoadApi().
extern Api api;

// Endereço base onde a libil2cpp.so foi mapeada (base + RVA = endereço real).
extern uintptr_t g_base;

// Abre a libil2cpp.so que JÁ está carregada no processo (RTLD_NOLOAD) e resolve
// todos os símbolos. Retorna false se a lib ainda não subiu ou falta algum export.
bool LoadApi(const char* libName);

// Em arm64, um Il2CppArray tem cabeçalho de 4 ponteiros
// (klass, monitor, bounds, max_length) antes dos elementos: 0x20 bytes.
constexpr size_t kArrayDataOffset = 4 * sizeof(void*);

template <typename T>
inline T* ArrayData(Il2CppArray* arr) {
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(arr) + kArrayDataOffset);
}

// Leitura/escrita de campo de instância por offset. O offset é relativo ao
// início do objeto (já inclui klass + monitor), igual ao dump.cs.
template <typename T>
inline T& FieldAt(void* obj, size_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + offset);
}

}  // namespace il2cpp
