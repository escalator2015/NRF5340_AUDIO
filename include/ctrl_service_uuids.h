/*
 * Audio Control Service (ACS) — UUIDs compartidos entre sink y source.
 *
 * Service:  7c4f5e00-1a2b-3c4d-5e6f-7a8b9c0d1e2f
 * CMD char: 7c4f5e01-1a2b-3c4d-5e6f-7a8b9c0d1e2f  (Write Without Response)
 * EVT char: 7c4f5e02-1a2b-3c4d-5e6f-7a8b9c0d1e2f  (Notify)
 */

#ifndef _CTRL_SERVICE_UUIDS_H_
#define _CTRL_SERVICE_UUIDS_H_

#include <zephyr/bluetooth/uuid.h>

/* Audio Control Service */
#define ACS_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0x7c4f5e00, 0x1a2b, 0x3c4d, 0x5e6f, 0x7a8b9c0d1e2f)

/* CMD characteristic: source → sink, Write Without Response */
#define ACS_CMD_CHAR_UUID_VAL \
	BT_UUID_128_ENCODE(0x7c4f5e01, 0x1a2b, 0x3c4d, 0x5e6f, 0x7a8b9c0d1e2f)

/* EVT characteristic: sink → source, Notify */
#define ACS_EVT_CHAR_UUID_VAL \
	BT_UUID_128_ENCODE(0x7c4f5e02, 0x1a2b, 0x3c4d, 0x5e6f, 0x7a8b9c0d1e2f)

#define ACS_SERVICE_UUID  BT_UUID_DECLARE_128(ACS_SERVICE_UUID_VAL)
#define ACS_CMD_CHAR_UUID BT_UUID_DECLARE_128(ACS_CMD_CHAR_UUID_VAL)
#define ACS_EVT_CHAR_UUID BT_UUID_DECLARE_128(ACS_EVT_CHAR_UUID_VAL)

/* Maximum JSON payload per characteristic */
#define ACS_JSON_MAX_LEN 200

#endif /* _CTRL_SERVICE_UUIDS_H_ */
