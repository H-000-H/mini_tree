/* IDE-only stub — real header lives in board/tusb/ on a platform tree */
#ifndef USB_TUSB_PORT_H
#define USB_TUSB_PORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

bool     usb_tusb_init(uint8_t rhport);
void     usb_tusb_task(void);
void     usb_tusb_int_handler(uint8_t rhport);

bool     usb_tusb_cdc_connected(void);
uint32_t usb_tusb_cdc_write(const void* buf, uint32_t len);
void     usb_tusb_cdc_write_flush(void);
uint32_t usb_tusb_cdc_available(void);
uint32_t usb_tusb_cdc_read(void* buf, uint32_t len);

bool     usb_tusb_hid_ready(void);
bool     usb_tusb_hid_report(uint8_t report_id, const void* report, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* USB_TUSB_PORT_H */
