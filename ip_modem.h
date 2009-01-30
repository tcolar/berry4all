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
#ifndef _IP_MODEM_H
#define _IP_MODEM_H

#include <usb.h>

#include "XmBlackBerry.h"
#include "buffer.h"

typedef struct ip_modem_rec {
    usb_dev_handle *dev;
    int read_ep;
    int write_ep;
    int master;
    int slave;
    pthread_t usb_thread_id;
    BlackBerry_t *blackberry;
    void (*initialize)(struct ip_modem_rec *modem);
    BufferStruct usb_buffer;
    BufferStruct pty_buffer;
} ip_modem;

ip_modem *NewIpModem(BlackBerry_t *blackberry, usb_dev_handle *dev, int read_ep, int write_ep);
#endif
