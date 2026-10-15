/* Garante que modrix_api.h é C99 puro: compilado com -std=c99 -Wpedantic -Werror. */
#include <modrix/modrix_api.h>

static bool OnHurt(void* user, ModrixPlayerHurt* ev, bool after) {
    (void)user;
    if (!after) ev->damage /= 2;
    return true;
}

MODRIX_MOD_EXPORT int modrix_mod_init(const ModrixHostApi* host, ModrixContext* ctx);

MODRIX_MOD_EXPORT int modrix_mod_init(const ModrixHostApi* host, ModrixContext* ctx) {
    ModrixItemDef def = {0};
    if (!host || host->api_version != MODRIX_API_VERSION) return -1;
    def.struct_size = (uint32_t)sizeof(def);
    def.name = "c_item";
    def.display_name = "Item em C";
    def.width = def.height = 16;
    def.max_stack = 1;
    if (host->register_item(ctx, &def) == MODRIX_INVALID_ID) return 1;
    return host->subscribe_player_hurt(ctx, OnHurt, NULL) ? 0 : 1;
}
