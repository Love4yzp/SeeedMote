#include "bthome.h"

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0]          |
           ((uint32_t)p[1] <<  8)  |
           ((uint32_t)p[2] << 16)  |
           ((uint32_t)p[3] << 24);
}

static bool parse_objects(const uint8_t *p, size_t len,
                           struct bthome_motion_event *ev)
{
    const uint8_t *end = p + len;

    while (p < end) {
        uint8_t id  = p[0];
        size_t  rem = (size_t)(end - p);

        switch (id) {
        case BTHOME_OBJ_PACKET_ID:
            if (rem < 2) return false;
            ev->has_pid = true; ev->pid = p[1];
            p += 2; break;
        case BTHOME_OBJ_BATTERY:
            if (rem < 2) return false;
            p += 2; break;
        case BTHOME_OBJ_COUNT_U8:
            if (rem < 2) return false;
            ev->has_ctr = true; ev->ctr = p[1];
            p += 2; break;
        case BTHOME_OBJ_MOTION:
            if (rem < 2) return false;
            ev->has_motion = true; ev->motion = p[1] != 0;
            p += 2; break;
        case BTHOME_OBJ_MOVING:
            if (rem < 2) return false;
            ev->has_moving = true; ev->moving = p[1] != 0;
            p += 2; break;
        case BTHOME_OBJ_VIBRATION:
            if (rem < 2) return false;
            ev->has_vibration = true; ev->vibration = p[1] != 0;
            p += 2; break;
        case BTHOME_OBJ_COUNT_U16:
            if (rem < 3) return false;
            ev->has_ctr = true; ev->ctr = get_le16(&p[1]);
            p += 3; break;
        case BTHOME_OBJ_COUNT_U32:
            if (rem < 5) return false;
            ev->has_ctr = true; ev->ctr = get_le32(&p[1]);
            p += 5; break;
        case BTHOME_OBJ_DEVICE_TYPE_ID:
            if (rem < 3) return false;
            p += 3; break;
        case BTHOME_OBJ_FW_VERSION_U32:
            if (rem < 5) return false;
            p += 5; break;
        case BTHOME_OBJ_FW_VERSION_U24:
            if (rem < 4) return false;
            p += 4; break;
        default:
            /* BTHome receivers stop at unknown object ids. */
            return ev->has_motion || ev->has_moving ||
                   ev->has_vibration || ev->has_ctr;
        }
    }

    return ev->has_motion || ev->has_moving ||
           ev->has_vibration || ev->has_ctr;
}

bool parse_bthome_adv(const uint8_t *data, size_t len,
                      struct bthome_motion_event *out_event)
{
    const uint8_t *p   = data;
    const uint8_t *end = data + len;
    bool matched = false;

    while (p < end) {
        uint8_t ad_len = p[0];
        if (ad_len == 0 || p + 1 + ad_len > end) return matched;

        if (p[1] == BLE_AD_TYPE_SERVICE_DATA16) {
            const uint8_t *svc     = p + 2;
            size_t         svc_len = (size_t)(ad_len - 1);

            if (svc_len >= 3 &&
                svc[0] == BTHOME_UUID_LSB &&
                svc[1] == BTHOME_UUID_MSB) {
                uint8_t device_info = svc[2];
                uint8_t version     = (device_info >> 5) & 0x07;
                if ((device_info & BTHOME_ENCRYPTED_FLAG) || version != BTHOME_VERSION_V2)
                    return matched;

                struct bthome_motion_event ev = {0};
                if (parse_objects(svc + 3, svc_len - 3, &ev)) {
                    if (out_event) *out_event = ev;
                    matched = true;
                }
            }
        }
        p += 1 + ad_len;
    }
    return matched;
}
