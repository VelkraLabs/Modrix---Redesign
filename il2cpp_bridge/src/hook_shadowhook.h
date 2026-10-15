#pragma once

// Instalador de inline hook baseado no ShadowHook (bytedance/android-inline-hook).
// Só existe no build Android (arm64-v8a / armeabi-v7a).
bool Modrix_ShadowHookInstaller(void* target, void* replacement, void** original);
