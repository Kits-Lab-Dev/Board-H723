#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "sdram.h"
#include "bt.h"
#include "control.h"

#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_l2cap.h"
#include "host/ble_sm.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

lua_State *srv_L = NULL;
lua_State *srv_thread_L = NULL;
typedef enum
{
    BLE_SRV_NOINIT,
    BLE_SRV_INIT,
    BLE_SRV_RUN
} ble_server_state;
static ble_server_state srv_state = BLE_SRV_NOINIT;
static struct ble_hs_cfg *srv_cfg = &ble_hs_cfg;
struct ble_gatt_svc_def *srv_svc = NULL;

static int srv_connect_cb = LUA_NOREF;
static int srv_disconnect_cb = LUA_NOREF;

typedef struct
{
    lua_State *L;
    int callback;
} ble_chr_callback;

static TaskHandle_t srv_task_h;

static void ble_srv_on_sync(void);

LUA_LIB_FUNC static int l_ble_enable(lua_State *L)
{
    if (lua_type(L, 1) != LUA_TBOOLEAN)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid argument type");
        return 2;
    }
    bool en = lua_toboolean(L, 1);
    vTaskDelay(100);
    if (!espFeatureEnable(ftBluetooth, en))
    {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to enable BLE feature");
        return 2;
    }
    vTaskDelay(100);
    lua_pushboolean(L, true);
    return 1;
}

static int l_ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    char addr_str[18];
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            // Успешное подключение
            lua_rawgeti(srv_L, LUA_REGISTRYINDEX, srv_connect_cb);
            // ble_addr_to_str(&event->connect.conn.remote_id_addr.val, addr_str, sizeof(addr_str));
            lua_pushstring(srv_L, addr_str);
            lua_call(srv_L, 1, 0);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        // Отключение
        lua_rawgeti(srv_L, LUA_REGISTRYINDEX, srv_disconnect_cb);
        ble_addr_to_str(&event->disconnect.conn.peer_id_addr.val, addr_str, sizeof(addr_str));
        lua_pushstring(srv_L, addr_str);
        lua_call(srv_L, 1, 0);
        break;

    default:
        break;
    }
    return 0;
}

static int
gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct os_mbuf *om = ctxt->om;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        while (om)
        {
            DUMP(om->om_data, om->om_len, "gatt_svr_chr_access:\n");
            om = SLIST_NEXT(om, om_next);
            if (arg != NULL)
            {
                int cb = (int)arg;
                // выполнить lua функцию обратного вызова
                lua_rawgeti(srv_L, LUA_REGISTRYINDEX, cb);
                lua_settop(srv_L, 1);
                lua_xmove(srv_L, srv_thread_L, 1);
                if (lua_isfunction(srv_thread_L, -1))
                {
                    lua_pushlstring(srv_thread_L, (const char *)ctxt->om->om_data, ctxt->om->om_len);
                    if (lua_pcall(srv_thread_L, 1, 0, 0) != LUA_OK)
                    {
                        ERR("%s\n", lua_tostring(srv_thread_L, -1));
                        lua_pop(srv_thread_L, 1);
                    }
                }
            }
        }
        return 0;

    default:
        ERR("gatt_svr_chr_access: unknown operation\n");
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static void l_ble_free_svc(struct ble_gatt_svc_def *svc)
{
    if(!svc) 
        return;

    for(int i = 0; svc[i].type != 0; i++) {
        if(svc[i].characteristics != NULL)
        {
            sdramFree(svc[i].characteristics);
        }
    }
    sdramFree(svc);
}

static int l_ble_set_services(lua_State *L)
{
    if (lua_type(L, 2) != LUA_TTABLE)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid argument type: expected table");
        return 2;
    }

    // Count the number of services
    int svc_count = luaL_len(L, 2);
    // Allocate memory for services
    struct ble_gatt_svc_def *services = sdramMalloc((svc_count + 1) * sizeof(struct ble_gatt_svc_def));
    memset(services, 0, (svc_count + 1) * sizeof(struct ble_gatt_svc_def));
    if (services == NULL)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Memory allocation failed");
        return 2;
    }

    // Parse each service
    for (int svc_idx = 0; svc_idx < svc_count; svc_idx++)
    {
        lua_rawgeti(L, 2, svc_idx + 1); // Push the i-th element onto the stack
        lua_getfield(L, -1, "uuid");
        const char *service_uuid_str = luaL_checkstring(L, -1);
        ble_uuid_any_t service_uuid;
        int status = ble_uuid_from_str(&service_uuid, service_uuid_str);
        if (status != 0)
        {
            l_ble_free_svc(services);
            lua_pushnil(L);
            lua_pushfstring(L, "Invalid UUID format: %s", service_uuid_str);
            return 2;
        }

        lua_getfield(L, -1, "characteristics");
        int char_count = luaL_len(L, -1);

        // Check if characteristics is a table or a function
        if (lua_type(L, -1) != LUA_TTABLE)
        {
            l_ble_free_svc(services);
            lua_pushnil(L);
            lua_pushfstring(L, "Invalid characteristics type: expected table or function, got %s", lua_typename(L, -1));
            return 2;
        }

        // Allocate memory for characteristics
        struct ble_gatt_chr_def *chars = sdramMalloc((char_count + 1) * sizeof(struct ble_gatt_chr_def));
        if (chars == NULL)
        {
            l_ble_free_svc(services);
            lua_pushnil(L);
            lua_pushstring(L, "Memory allocation failed");
            return 2;
        }

        // Parse each characteristic
        for (int char_idx = 0; char_idx < char_count; char_idx++)
        {
            lua_rawgeti(L, -1, char_idx + 1); // Push the i-th element onto the stack
            lua_getfield(L, -1, "uuid");
            const char *char_uuid_str = luaL_checkstring(L, -1);
            ble_uuid_any_t char_uuid;
            status = ble_uuid_from_str(&char_uuid, char_uuid_str);
            if (status != 0)
            {
                l_ble_free_svc(services);
                lua_pushnil(L);
                lua_pushfstring(L, "Invalid UUID format for characteristic: %s", char_uuid_str);
                return 2;
            }

            lua_getfield(L, -1, "flags");
            int flags = luaL_checkinteger(L, -1);

            // Check if callback exists and is either a table or a function
            lua_getfield(L, -1, "callback");
            if (lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TTABLE)
            {
                // Store the callback function in a way that can be accessed later
                int callback = luaL_ref(L, LUA_REGISTRYINDEX);
                chars[char_idx] = (struct ble_gatt_chr_def){
                    .uuid = &char_uuid.u,
                    .access_cb = gatt_svr_chr_access,
                    .arg = (void *)callback,
                    .flags = flags,
                    .descriptors = NULL,
                };
            }
            else
            {
                l_ble_free_svc(services);
                lua_pushnil(L);
                lua_pushfstring(L, "Invalid callback type: expected function or table, got %s", lua_typename(L, -1));
                return 2;
            }

            lua_pop(L, 1); // Pop the characteristic table from the stack
        }

        chars[char_count] = (struct ble_gatt_chr_def){0}; // Null terminator
        services[svc_idx] = (struct ble_gatt_svc_def){
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = &service_uuid.u,
            .characteristics = chars, // Add array of characteristics
        };

        lua_pop(L, 1); // Pop the service table from the stack
    }

    services[svc_count] = (struct ble_gatt_svc_def){0}; // Null terminator
    if (srv_svc != NULL)
    {
        // Free old services and update context with new ones
        for (int i = 0; srv_svc[i].type != 0; i++)
        {
            if (srv_svc[i].characteristics != NULL)
                sdramFree(srv_svc[i].characteristics);
        }
        sdramFree(srv_svc);
    }
    srv_svc = services;

    int rc = ble_gatts_count_cfg(srv_svc);
    if (rc != 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to count GATT services");
        return 2;
    }

    rc = ble_gatts_add_svcs(srv_svc);
    if (rc != 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to add GATT services");
        return 2;
    }

    lua_pushboolean(L, true);
    return 1;
}

static void ble_srv_run(void *arg)
{
    struct ble_npl_event *ev;
    INF("BRE AP Started...\n");
    for (;;)
    {
        ev = ble_npl_eventq_get(nimble_port_get_dflt_eventq(), BLE_NPL_TIME_FOREVER);
        ble_npl_event_run(ev);
    }
}

static int ble_srv_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0)
        {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            bleuart_set_conn_handle(event->connect.conn_handle);
        }

        if (event->connect.status != 0)
        {
            /* Connection failed; resume advertising. */
            ble_srv_on_sync();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated; resume advertising. */
        ble_srv_on_sync();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* Advertising terminated; resume advertising. */
        ble_srv_on_sync();
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        WRN("PASSKEY_ACTION_EVENT started");
        struct ble_sm_io pkey = {0};
        int key = 0;

        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // This is the passkey to be entered on peer
            WRN("Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            WRN("ble_sm_inject_io result: %d", rc);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP)
        {
            WRN("Passkey on device's display: %" PRIu32, event->passkey.params.numcmp);
            WRN("Accept or reject the passkey through console in this format -> key Y or key N");
            pkey.action = event->passkey.params.action;

            pkey.numcmp_accept = 1;

            // pkey.numcmp_accept = 0;

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            WRN("ble_sm_inject_io result: %d", rc);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_OOB)
        {
            static uint8_t tem_oob[16] = {0};
            pkey.action = event->passkey.params.action;
            for (int i = 0; i < 16; i++)
            {
                pkey.oob[i] = tem_oob[i];
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            WRN("ble_sm_inject_io result: %d", rc);
        }
        else if (event->passkey.params.action == BLE_SM_IOACT_INPUT)
        {
            WRN("Enter the passkey through console in this format-> key 123456");
            pkey.action = event->passkey.params.action;

            pkey.numcmp_accept = 1;

            // pkey.numcmp_accept = 0;

            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            WRN("ble_sm_inject_io result: %d", rc);
        }
        return 0;

    case 32:
        // DBG("authorize event: conn_handle=%d attr_handle=%d is_read=%d",
        //             event->authorize.conn_handle,
        //             event->authorize.attr_handle,
        //             event->authorize.is_read);

        // /* The default behaviour for the event is to reject authorize request */
        // event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
        return 0;
    }

    return 0;
}

static void ble_srv_on_sync(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    /*
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o 128 bit UUID
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assiging the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    fields.uuids128 = BLE_UUID128(&srv_svc->uuid->type);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        return;
    }

    memset(&fields, 0, sizeof fields);
    fields.name = (uint8_t *)ble_svc_gap_device_name();
    fields.name_len = strlen((char *)fields.name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0)
    {
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_srv_gap_event, NULL);
    if (rc != 0)
    {
        return;
    }
}

void ble_srv_gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        WRN("registered service %s with handle=%d\n",
            ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
            ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        WRN("registering characteristic %s with "
            "def_handle=%d val_handle=%d\n",
            ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
            ctxt->chr.def_handle,
            ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        WRN("registering descriptor %s with handle=%d\n",
            ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
            ctxt->dsc.handle);
        break;

    default:

        break;
    }
}

static int l_ble_server_start(lua_State *L)
{
    srv_thread_L = lua_newthread(L);
    xTaskCreate(ble_srv_run, "ble", configMINIMAL_STACK_SIZE + 400,
                NULL, tskIDLE_PRIORITY + 1, &srv_task_h);
    lua_pushboolean(L, true);
    return 1;
}

static int l_ble_on_connect(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    srv_connect_cb = luaL_ref(L, LUA_REGISTRYINDEX);

    return 0;
}

static int l_ble_on_disconnect(lua_State *L)
{
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    srv_disconnect_cb = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}



static int l_ble_server_gc(lua_State *L)
{
    // Unregister connect/disconnect callbacks if they exist
    lua_rawgeti(L, LUA_REGISTRYINDEX, srv_connect_cb);
    if (lua_isfunction(L, -1))
        luaL_unref(L, LUA_REGISTRYINDEX, srv_connect_cb); // Free the reference to the callback

    lua_pop(L, 1);

    lua_rawgeti(L, LUA_REGISTRYINDEX, srv_disconnect_cb);
    if (lua_isfunction(L, -1))
        luaL_unref(L, LUA_REGISTRYINDEX, srv_disconnect_cb); // Free the reference to the callback

    lua_pop(L, 1);

    // Clean up any other resources here
    // For example, free memory allocated for services and characteristics

    if (srv_svc != NULL)
    {
        for(int i = 0; srv_svc[i].type != 0; i++)
        {
            if (srv_svc[i].characteristics != NULL)
                sdramFree(srv_svc[i].characteristics);
        }
        sdramFree(srv_svc);
    }

    // Reset global state
    srv_L = NULL;
    srv_thread_L = NULL;
    srv_connect_cb = LUA_NOREF;
    srv_disconnect_cb = LUA_NOREF;

    return 0; // Return 0 to indicate no value is pushed onto the stack
}

static const luaL_Reg server_methods[] = {
    {"setServices", l_ble_set_services},
    {"start", l_ble_server_start},
    {"onConnect", l_ble_on_connect},
    {"onDisconnect", l_ble_on_disconnect},
    {NULL, NULL}};

// Функция создания сервера
static int l_ble_server(lua_State *L)
{
    srv_L = L;
    ble_hs_cfg.sync_cb = ble_srv_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.gatts_register_cb = ble_srv_gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_sc = true;
    ble_hs_cfg.sm_bonding = true;
    ble_hs_cfg.sm_mitm = true;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_store_config_init();

    const char *device_name = luaL_checkstring(L, 1);
    // Установка имени устройства
    if (ble_svc_gap_device_name_set(device_name) != 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Error setting device name");
        return 2;
    }
    
    lua_newtable(L);
    luaL_setmetatable(L, "__ble_server_gc");
    luaL_setfuncs(L, server_methods, 0);
    return 1;
}

static const luaL_Reg tab_funcs[] = {
    {"enable", l_ble_enable},
    {"server", l_ble_server},
    {NULL, NULL}};

LUAMOD_API int luaopen_ble(lua_State *L)
{
    luaL_newmetatable(L, "__ble_server_gc");
    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, l_ble_server_gc);
    lua_settable(L, -3);
    luaL_newlib(L, tab_funcs);
    return 1;
}