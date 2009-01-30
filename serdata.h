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
#ifndef _SERDATA_H
#define _SERDATA_H

#include "XmBlackBerry.h"
#include "buffer.h"

struct BlackBerry_rec;

typedef struct serdata_rec {
    struct BlackBerry_rec *blackberry;
    uint32_t sequence;
    char *mode;
    uint16_t socket;
    int (*initialize)(struct serdata_rec *serdata);
    int (*send_packet)(struct BlackBerry_rec *device, uint16_t socket, void *packet, int len);
    int (*receive)(struct serdata_rec *serdata, void *packet, int len);

    BufferStruct data;
    int master;
    int slave;
} serdata_t;

#ifdef __cplusplus
extern "C" {
#endif

struct serdata_rec *NewSerData(struct BlackBerry_rec *blackberry);
struct serdata_rec *FreeSerData(struct serdata_rec *serdata);

#ifdef __cplusplus
}
#endif
#endif
