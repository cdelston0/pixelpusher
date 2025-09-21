/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
 extern "C" {
#endif

#define CFG_TUD_ENABLED         (1)
#define CFG_TUD_VENDOR          (1)

// Legacy RHPORT configuration
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        (0)
#endif
// end legacy RHPORT
//

//------------------------
// DEVICE CONFIGURATION //
//------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE  (64)
#endif

#define CFG_TUD_VENDOR_EPSIZE     8192
#define CFG_TUD_VENDOR_RX_BUFSIZE 0
#define CFG_TUD_VENDOR_TX_BUFSIZE 0

#ifdef __cplusplus
 }
#endif

#endif /* _TUSB_CONFIG_H_ */
