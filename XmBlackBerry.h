/*
	XmBlackBerry, Copyright (C) 2006  Rick Scott <rwscott@users.sourceforge.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/** \file
*/
#ifndef _XMBLACKBERRY_H
#define _XMBLACKBERRY_H

#include <stdint.h>
#include <unistd.h>

#include "buffer.h"
#include "list.h"
#include "desktop.h"
#include "database.h"
#include "bypass.h"

typedef void (*BlackBerryCallbackProc)(struct BlackBerry_rec *device, void *client_data, void *call_data);

/**
	Various statistics about the device
*/
typedef struct BlackBerryStats_rec {
    unsigned int tx; /**< total bytes sent to the device. Updated by the driver. */
    unsigned int rx; /**< total bytes received from device. Updated by the driver. */
} BlackBerryStats_t;

/**
	The main device descriptor
*/
typedef struct BlackBerry_rec {
    struct BlackBerry_rec *next;

    ListStruct tx_queue; /**< A list of packets queued for the device */
    BufferStruct *last_packet; /**< The last packet sent */

#if 1 /* Things that describe the handheld */
    uint32_t pin; /**< device pin */
    uint32_t version; /**< device version */
    uint32_t baud; /**< baud rate to switch to after the device
                        is opened
                   */
    uint16_t max_packet; /**< maximum packet size that will come from
                              the device, or should be sent to the device.
                              USB packets are actually 4 bytes larger than this
                         */
    char *desc; /**< Description from the device */
    uint16_t series;
#if 0
    struct command_rec *command_table; /* should be in desktop_t */
#endif
#if 0
    struct dbdb_rec *database_table; /* should be in database_t */
#endif
#endif
#if 1 /* Used to communicate to the driver */
    int fd; /**< file descriptor to monitor for input. Call the
                 read function when data is available. This is set by
                 the driver.
            */
    int connected; /**< Set by the driver */
    void (*close)(struct BlackBerry_rec *device); /**< Set by the driver */
    ssize_t (*read)(struct BlackBerry_rec *device); /**< Set by the driver */
    BlackBerryCallbackProc disconnect_callback; /**< Set by the UI */
    int (*select_mode)(struct BlackBerry_rec *blackberry, char *mode); /**< Set by the driver */
    int (*close_socket)(struct BlackBerry_rec *blackberry, uint16_t socket); /**< Set by the driver */
    void *client_data; /**< Set by the driver, but supplied by the UI in the open call */
    void (*monitor_fd)(struct BlackBerry_rec *blackberry, int fd, int (*function)(void *), void *client_data); /**< Set by the UI */
    void (*send_password)(struct BlackBerry_rec *blackberry, char *passwd); /**< Set by the driver */
#endif
#if 1 /* Used to communicate to the higher protocols */
    struct desktop_rec *desktop;
    struct bypass_rec *bypass;
    struct serdata_rec *serdata;
    struct serctrl_rec *serctrl;
#endif
#if 1 /* Used by the driver to talk to the UI */
    void (*get_password)(struct BlackBerry_rec *blackberry); /**< Set by the UI */
    int connect_to_desktop; /**< Set by the UI */
    BlackBerryStats_t stats; /**< Set by the driver */
    char *tty_name; /**< The device used to talk to the GPRS modem. Set by the driver. */
#endif
    void *driver_private; /**< For use of the driver */
    void *ui_private; /**< For use of the UI */
} BlackBerry_t;

#endif
