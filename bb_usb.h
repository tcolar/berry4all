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
#ifndef _BB_USB_H
#define _BB_USB_H

#include "XmBlackBerry.h"

#ifdef __cplusplus
extern "C" {
#endif

BlackBerry_t *usb_bb_open(BlackBerryCallbackProc new_device_callback, void *client_data);

#ifdef __cplusplus
}
#endif
#endif
