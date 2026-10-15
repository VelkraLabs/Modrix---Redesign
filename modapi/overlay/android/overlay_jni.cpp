// Cola JNI do overlay: acha a VM e a Activity do jogo, carrega o DEX do
// renderizador embutido nesta .so, registra os métodos nativos e chama
// ModrixOverlay.install(activity, handle).
#include "overlay_android.h"

#include <dlfcn.h>
#include <jni.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <time.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "modrix/overlay/panel.h"
#include "modrix/runtime/log.h"

extern "C" const unsigned char modrix_overlay_dex[];      // overlay_dex.S
extern "C" const unsigned char modrix_overlay_dex_end[];

namespace modrix::overlay {
namespace {

JavaVM* g_vm = nullptr;
constexpr const char* kOverlayClass = "com.modrix.overlay.ModrixOverlay";

// --- VM ------------------------------------------------------------------------
// Se a .so veio por System.loadLibrary, o JNI_OnLoad já guardou a VM. Se veio
// por outro caminho (wrap.sh, loader), pergunta à ART quais VMs existem.
JavaVM* FindVm() {
    if (g_vm) return g_vm;
    using GetVms = jint (*)(JavaVM**, jsize, jsize*);
    for (const char* lib : {"libnativehelper.so", "libart.so"}) {
        void* h = dlopen(lib, RTLD_NOW | RTLD_NOLOAD);
        auto fn = h ? reinterpret_cast<GetVms>(dlsym(h, "JNI_GetCreatedJavaVMs")) : nullptr;
        JavaVM* vm = nullptr;
        jsize n = 0;
        if (fn && fn(&vm, 1, &n) == JNI_OK && n > 0) return g_vm = vm;
    }
    return nullptr;
}

int SdkInt() {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.sdk", value);
    return std::atoi(value);
}

void SleepMs(int ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

// --- Texto: UTF-8 <-> UTF-16 (NewStringUTF usa "UTF-8 modificado"; evitamos) --
std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + static_cast<size_t>(len) > s.size()) { out.push_back(0xFFFD); break; }
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0x3F);
        i += static_cast<size_t>(len);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

std::string ToUtf8(JNIEnv* env, jstring js) {
    if (!js) return {};
    const jchar* chars = env->GetStringChars(js, nullptr);
    jsize n = env->GetStringLength(js);
    std::string out;
    for (jsize i = 0; i < n; ++i) {
        uint32_t cp = chars[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) cp = 0x10000 + ((cp - 0xD800) << 10) + (chars[++i] - 0xDC00);
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    env->ReleaseStringChars(js, chars);
    return out;
}

// --- Métodos nativos do renderizador (thread de UI) ---------------------------
PanelController* Ctl(jlong h) { return reinterpret_cast<PanelController*>(h); }

jstring JNICALL NativeSnapshot(JNIEnv* env, jclass, jlong h) {
    std::u16string json = Utf8ToUtf16(Ctl(h)->SnapshotJson());
    return env->NewString(reinterpret_cast<const jchar*>(json.data()), static_cast<jsize>(json.size()));
}

jboolean JNICALL NativeAction(JNIEnv* env, jclass, jlong h, jstring action, jstring arg) {
    return Ctl(h)->HandleAction(ToUtf8(env, action), ToUtf8(env, arg)) ? JNI_TRUE : JNI_FALSE;
}

jboolean JNICALL NativeTick(JNIEnv*, jclass, jlong h, jlong nowMs) {
    return Ctl(h)->Tick(static_cast<int64_t>(nowMs)) ? JNI_TRUE : JNI_FALSE;
}

// --- Helpers JNI ---------------------------------------------------------------
bool Failed(JNIEnv* env, const char* what, std::string* error) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();  // vai para o logcat
    env->ExceptionClear();
    if (error) *error = std::string("exceção Java em ") + what;
    return true;
}

jclass LoadClass(JNIEnv* env, jobject loader, const char* name) {
    jclass cl = env->FindClass("java/lang/ClassLoader");
    jmethodID loadClass = env->GetMethodID(cl, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jname = env->NewStringUTF(name);  // nomes de classe são ASCII
    auto cls = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, jname));
    env->DeleteLocalRef(jname);
    return cls;
}

// ClassLoader do app (FindClass numa thread nativa só enxerga o do sistema).
jobject AppClassLoader(JNIEnv* env, jobject* appOut) {
    jclass at = env->FindClass("android/app/ActivityThread");
    jmethodID current = at ? env->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;") : nullptr;
    jobject app = current ? env->CallStaticObjectMethod(at, current) : nullptr;
    if (!app || env->ExceptionCheck()) return nullptr;
    jclass ctx = env->FindClass("android/content/Context");
    jmethodID getLoader = env->GetMethodID(ctx, "getClassLoader", "()Ljava/lang/ClassLoader;");
    *appOut = app;
    return env->CallObjectMethod(app, getLoader);
}

// Carrega o DEX embutido. API 26+: direto da memória. Antes disso: arquivo
// somente-leitura no codeCacheDir (o Android 14 recusa DEX gravável).
jobject DexClassLoader(JNIEnv* env, jobject app, jobject parent, std::string* error) {
    const void* data = modrix_overlay_dex;
    auto size = static_cast<jlong>(modrix_overlay_dex_end - modrix_overlay_dex);
    if (SdkInt() >= 26) {
        jobject buffer = env->NewDirectByteBuffer(const_cast<void*>(data), size);
        jclass cls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
        jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        jobject loader = env->NewObject(cls, ctor, buffer, parent);
        return Failed(env, "InMemoryDexClassLoader", error) ? nullptr : loader;
    }
    jclass ctx = env->FindClass("android/content/Context");
    jmethodID getDir = env->GetMethodID(ctx, "getCodeCacheDir", "()Ljava/io/File;");
    jobject dir = env->CallObjectMethod(app, getDir);
    jclass file = env->FindClass("java/io/File");
    jmethodID getPath = env->GetMethodID(file, "getAbsolutePath", "()Ljava/lang/String;");
    std::string dirPath = ToUtf8(env, static_cast<jstring>(env->CallObjectMethod(dir, getPath)));
    std::string dexPath = dirPath + "/modrix_overlay.dex";
    std::remove(dexPath.c_str());
    {
        std::ofstream out(dexPath, std::ios::binary);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    chmod(dexPath.c_str(), 0444);
    jclass cls = env->FindClass("dalvik/system/DexClassLoader");
    jmethodID ctor = env->GetMethodID(cls, "<init>",
                                      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    jobject loader = env->NewObject(cls, ctor, env->NewStringUTF(dexPath.c_str()), env->NewStringUTF(dirPath.c_str()),
                                    nullptr, parent);
    return Failed(env, "DexClassLoader", error) ? nullptr : loader;
}

bool Start(JNIEnv* env, PanelController* panel, int timeoutMs, std::string* error) {
    jobject app = nullptr;
    jobject appLoader = AppClassLoader(env, &app);
    if (!appLoader || Failed(env, "ActivityThread.currentApplication", error)) {
        if (error && error->empty()) *error = "Application ainda não existe";
        return false;
    }

    // A Activity do Unity. O próprio código do jogo lê esse campo (o literal
    // "currentActivity" está no stringliteral.json do dump).
    jclass unity = LoadClass(env, appLoader, "com.unity3d.player.UnityPlayer");
    if (!unity || Failed(env, "UnityPlayer", error)) return false;
    jfieldID currentActivity = env->GetStaticFieldID(unity, "currentActivity", "Landroid/app/Activity;");
    if (!currentActivity || Failed(env, "UnityPlayer.currentActivity", error)) return false;
    jobject activity = nullptr;
    for (int waited = 0; !(activity = env->GetStaticObjectField(unity, currentActivity)) && waited < timeoutMs;
         waited += 250)
        SleepMs(250);
    if (!activity) {
        if (error) *error = "UnityPlayer.currentActivity continuou nulo";
        return false;
    }

    jobject dexLoader = DexClassLoader(env, app, appLoader, error);
    if (!dexLoader) return false;
    jclass overlay = LoadClass(env, dexLoader, kOverlayClass);
    if (!overlay || Failed(env, kOverlayClass, error)) return false;

    const JNINativeMethod methods[] = {
        {"nativeSnapshot", "(J)Ljava/lang/String;", reinterpret_cast<void*>(&NativeSnapshot)},
        {"nativeAction", "(JLjava/lang/String;Ljava/lang/String;)Z", reinterpret_cast<void*>(&NativeAction)},
        {"nativeTick", "(JJ)Z", reinterpret_cast<void*>(&NativeTick)},
    };
    if (env->RegisterNatives(overlay, methods, 3) != JNI_OK || Failed(env, "RegisterNatives", error)) return false;

    // O controlador é entregue à thread de UI; daqui para frente só ela o usa.
    jmethodID install = env->GetStaticMethodID(overlay, "install", "(Landroid/app/Activity;J)V");
    env->CallStaticVoidMethod(overlay, install, activity, reinterpret_cast<jlong>(panel));
    return !Failed(env, "ModrixOverlay.install", error);
}

}  // namespace

bool StartOverlay(PanelController* panel, int timeoutMs, std::string* error) {
    JavaVM* vm = FindVm();
    if (!vm) {
        if (error) *error = "JavaVM não encontrada";
        return false;
    }
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "ModrixOverlay", nullptr};
        if (vm->AttachCurrentThread(&env, &args) != JNI_OK) {
            if (error) *error = "AttachCurrentThread falhou";
            return false;
        }
        attached = true;
    }
    env->PushLocalFrame(64);
    bool ok = Start(env, panel, timeoutMs, error);
    env->PopLocalFrame(nullptr);
    if (attached) vm->DetachCurrentThread();
    if (ok) rt::Log(MODRIX_LOG_INFO, "overlay", "painel instalado na Activity do jogo");
    return ok;
}

}  // namespace modrix::overlay

// Quando a .so é carregada por System.loadLibrary: guarda a VM.
extern "C" __attribute__((visibility("default"))) jint JNI_OnLoad(JavaVM* vm, void*) {
    modrix::overlay::g_vm = vm;
    return JNI_VERSION_1_6;
}
