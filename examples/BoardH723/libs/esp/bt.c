#include "bt.h"
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sysinit/sysinit.h>
#include <syscfg/syscfg.h>
#include "os/os_mbuf.h"
#include "nimble/transport.h"
#include "nimble/transport/hci_h4.h"
#include "nimble/hci_common.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "app.h"

#include "os/os.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

/* BLE */
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_l2cap.h"
#include "host/ble_sm.h"

/* Mandatory services. */
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "bleuart/bleuart.h"

static int bleuart_gap_event(struct ble_gap_event *event, void *arg);

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
bleuart_advertise(void)
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

    fields.uuids128 = BLE_UUID128(&gatt_svr_svc_uart_uuid.u);
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
                           &adv_params, bleuart_gap_event, NULL);
    if (rc != 0)
    {
        return;
    }
}

static int
bleuart_gap_event(struct ble_gap_event *event, void *arg)
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
            bleuart_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated; resume advertising. */
        bleuart_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        /* Advertising terminated; resume advertising. */
        bleuart_advertise();
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

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // This is the passkey to be entered on peer
            WRN("Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            WRN("ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            WRN("Passkey on device's display: %" PRIu32 , event->passkey.params.numcmp);
            WRN("Accept or reject the passkey through console in this format -> key Y or key N");
            pkey.action = event->passkey.params.action;

                pkey.numcmp_accept = 1;
            
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            WRN("ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            static uint8_t tem_oob[16] = {0};
            pkey.action = event->passkey.params.action;
            for (int i = 0; i < 16; i++) {
                pkey.oob[i] = tem_oob[i];
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            WRN("ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            WRN("Enter the passkey through console in this format-> key 123456");
            pkey.action = event->passkey.params.action;

                pkey.numcmp_accept = 1;

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

static void
bleuart_on_sync(void)
{
    /* Begin advertising. */
    bleuart_advertise();
}

void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    
    switch (ctxt->op) {
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
        assert(0);
        break;
    }
}

static void ble_run(void *)
{
    struct ble_npl_event *ev;
    int rc;
    ble_hs_cfg.sync_cb = bleuart_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_sc = true;
    ble_hs_cfg.sm_bonding = true;
    ble_hs_cfg.sm_mitm = true;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;

    rc = bleuart_gatt_svr_init();
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("Mynewt_BLEuart");
    assert(rc == 0);

    ble_store_config_init();
    INF("BLE AP Started...\n");
    for (;;)
    {
        ev = ble_npl_eventq_get(nimble_port_get_dflt_eventq(), BLE_NPL_TIME_FOREVER);
        ble_npl_event_run(ev);
    }
}

void ble_app_start(void)
{
    nimble_port_freertos_init(ble_run);
}

bool bleStackInit(void)
{
    nimble_port_init();
    return true;
}

#define ACL_DATA_MBUF_LEADINGSPACE 4

bool bt_rx_process(uint8_t *data, uint16_t len)
{
    int rc;
    uint8_t type = data[0];
    uint8_t *ptr = data + 1;

    if (type == HCI_H4_ACL)
    {
        struct os_mbuf *om = os_msys_get_pkthdr(len - 1, ACL_DATA_MBUF_LEADINGSPACE);
        os_mbuf_append(om, ptr, len - 1);
        // ble_hci_trans_hs_acl_tx(om);
        rc = ble_transport_to_hs_acl(om);
    }
    else if (type == HCI_H4_EVT)
    {
        void *evt = ble_transport_alloc_evt(1);
        memcpy(evt, ptr, len);
        rc = ble_transport_to_hs_evt(evt);
    }
    return rc == 0;
}

int ble_transport_to_ll_cmd_impl(void *data)
{
    int rc;

    struct ble_hci_cmd *cmd = (struct ble_hci_cmd *)data;

    p_spi_buf buf;
    buf.type = ESP_BT;
    buf.len = cmd->length + sizeof(struct ble_hci_cmd) + 1;
    buf.data = (uint8_t *)pvPortMalloc(buf.len);
    buf.data[0] = HCI_H4_CMD;
    memcpy(&buf.data[1], data, buf.len - 1);
    buf.is_released = true;
    buf.free_data_fn = vPortFree;
    rc = spiDriverWrite(&buf) ? 0 : -1;
    return rc;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    uint8_t rc = 0;
    /* If this packet is zero length, just free it */
    if (OS_MBUF_PKTLEN(om) == 0)
    {
        os_mbuf_free_chain(om);
        return 0;
    }

    p_spi_buf buf;
    buf.type = ESP_BT;
    buf.len = 0;
    buf.data = (uint8_t *)pvPortMalloc(MYNEWT_VAL(BLE_TRANSPORT_ACL_SIZE) + 1);
    buf.data[0] = HCI_H4_ACL;
    buf.len++;

    os_mbuf_copydata(om, 0, OS_MBUF_PKTLEN(om), &buf.data[1]);
    buf.len += OS_MBUF_PKTLEN(om);
    buf.is_released = true;
    buf.free_data_fn = vPortFree;
    rc = spiDriverWrite(&buf) ? 0 : 1;

    os_mbuf_free_chain(om);

    return rc;
}

void ble_transport_ll_init(void)
{
}

void ble_transport_ll_deinit(void)
{
}
