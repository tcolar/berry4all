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
    \brief Implements the Serial Data protocol (GPRS modem)
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pty.h>
#include <fcntl.h>

#include "serdata.h"
#include "serctrl.h"
#include "util.h"
#include "buffer.h"

/**
	Sends data to the driver.
*/
static int
send_packet(struct serdata_rec *serdata, void *packet, int len)
{
int ret = 0;

    if (serdata->send_packet)
    {
	ret = (*serdata->send_packet)(serdata->blackberry, serdata->socket, packet, len);
    }
    return(ret);
}

/**
	Receives data from the driver and writes it to the pty
*/
static int 
receive(struct serdata_rec *serdata, void *packet, int len)
{
int ret = 0;

    debug(9, "%s:%s(%d) - %08x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	serdata->blackberry->pin);
    ret = write(serdata->master, packet, len);
    if (ret != len)
    {
	debug(0, "%s:%s(%d) - %08x short write %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    serdata->blackberry->pin,
	    ret < 0 ? strerror(errno) : "");
    }
    return(ret);
}

/**
	Reads data from the pty and sends it to the device.
	Fixes up the protocol on the way by, since the BlackBerry
	doesn't seem to like the HDLC protocol :(
*/
static int
master_reader(void *client_data)
{
struct serdata_rec *serdata = (struct serdata_rec *)client_data;
unsigned char buf[0x400];
int num_read;
int i;

    num_read = read(serdata->master, buf, sizeof(buf));
    debug(9, "%s:%s(%d) - %08x %i %i\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	serdata->blackberry->pin,
    	num_read,
    	BufferLen(&serdata->data));

    if (num_read >= 0)
    {
    	if (num_read > 0)
    	{
	    if (serdata->slave >= 0)
	    {
		/* rws 7 Jun 2007
		   We know that something has now opened the slave tty,
		   so close our copy so that we will know when they 
		   close it.
		 */
		close(serdata->slave);
		serdata->slave = -1;
	    }
#if 1
	    /*
	       This is pretty ugly, but I have to put the HDLC flags into
	       the packets. RIM seems to need flags around every frame, and
	       a flag _cannot_ be an end and a start flag.
	     */
	    for (i = 0; i < num_read; i++)
	    {
		BufferAdd(&serdata->data, &buf[i], 1);
		if (BufferData(&serdata->data)[0] == 0x7e && buf[i] == 0x7e)
		{
		    if (BufferLen(&serdata->data) > 1 &&
			BufferData(&serdata->data)[0] == 0x7e && 
			BufferData(&serdata->data)[1] == 0x7e)
		    {
			BufferPullHead(&serdata->data, 1);
		    }
		    else
		    {
		    }
		    if (BufferLen(&serdata->data) > 2)
		    {
			if ((BufferLen(&serdata->data) + 4) % 16 == 0)
			{
				BufferAdd(&serdata->data, (unsigned char *)"\0", 1);
			}
			send_packet(serdata, BufferData(&serdata->data), BufferLen(&serdata->data));
			BufferEmpty(&serdata->data);
			BufferAdd(&serdata->data, (unsigned char *)"\176", 1);
		    }
		    if (BufferLen(&serdata->data) == 2)
		    {
			BufferPullTail(&serdata->data, 1);
		    }
		    else
		    {
		    }
		}
		else
		{
		}
	    }
	    if (BufferData(&serdata->data)[0] == 0x7e &&
		memcmp(&BufferData(&serdata->data)[1], "AT", 2) == 0)
	    {
		BufferPullHead(&serdata->data, 1);
	    }
	    if (BufferData(&serdata->data)[0] != 0x7e)
	    {
		debug(9, "%s:%s(%d) - %i\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    BufferLen(&serdata->data));
		send_packet(serdata, BufferData(&serdata->data), BufferLen(&serdata->data));
		BufferEmpty(&serdata->data);
	    }
#else
	    /* rws 4 Jun 2007
	       I can use this iff you use the pty pppd option with 
	       gprs_protocol_fix.
	     */
	    send_packet(serdata, buf, num_read);
#endif
	}
	else
	{
	    debug(0, "%s:%s(%d) - %08x read %i\n",
		__FILE__, __FUNCTION__, __LINE__,
		serdata->blackberry->pin,
		num_read);
	}
    }
    else
    {
	if (serdata->slave == -1)
	{
	    /* rws 7 Jun 2007
	       If our copy of the slave is closed, this may mean that the
	       program that had it open just closed it. So, open our copy
	       and get the modem ready for the next connection.
	     */
	    debug(7, "%s:%s(%d) - %08x read error %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		serdata->blackberry->pin,
		strerror(errno));
	    if (serdata->blackberry->tty_name)
	    {
		serdata->slave = open(serdata->blackberry->tty_name, O_RDWR | O_NOCTTY);
		if (serdata->slave >= 0)
		{
		    if (serdata->blackberry->serctrl && serdata->blackberry->serctrl->initialize)
		    {
			(*serdata->blackberry->serctrl->initialize)(serdata->blackberry->serctrl);
		    }
		    num_read = 0;
		}
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - %08x read error %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		serdata->blackberry->pin,
		strerror(errno));
	}
    }
    return(num_read);
}

/**
	Initialize the Serial Data protocol. Creates the pty and
	starts the UI monitoring the file descriptor.
*/
static int
initialize(struct serdata_rec *serdata)
{
int ret = 0;

    debug(9, "%s:%s(%d) - %08x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	serdata->blackberry->pin);

    if (serdata->blackberry->monitor_fd)
    {
	ret = openpty(&serdata->master, &serdata->slave, NULL, NULL, NULL);
	if (ret >= 0)
	{
	    serdata->blackberry->tty_name = ttyname(serdata->slave);
	    if (serdata->blackberry->tty_name)
	    {

		debug(0, "%s:%s(%d) - pty name. \"%s\"\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    serdata->blackberry->tty_name);
		(*serdata->blackberry->monitor_fd)(serdata->blackberry, serdata->master, master_reader, serdata);
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - Getting pty name. %s\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    strerror(errno));
		ret = -1;
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - openpty failed. %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		strerror(errno));
	}
    }
    else
    {
    	ret = -1;
    	errno = ENOTSUP;
    }
    return(ret);
}

/**
	Create a new Serial Data instance
*/
serdata_t *
NewSerData(struct BlackBerry_rec *blackberry)
{
    debug(9, "%s:%s(%d) - %08x %p\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	blackberry->pin,
    	blackberry->monitor_fd);
    if (blackberry->monitor_fd)
    {
	blackberry->serdata = malloc(sizeof(serdata_t));
	if (blackberry->serdata)
	{
	    blackberry->serdata->blackberry = blackberry;
	    blackberry->serdata->mode = strdup("RIM_UsbSerData");
	    blackberry->serdata->socket = (uint16_t)-1;
	    blackberry->serdata->sequence = 0;
	    blackberry->serdata->initialize = initialize;
	    blackberry->serdata->send_packet = NULL;
	    blackberry->serdata->receive = receive;
	    BufferInit(&blackberry->serdata->data, blackberry->max_packet, 0);
	    blackberry->serdata->master = -1;
	    blackberry->serdata->slave = -1;
	    blackberry->tty_name = NULL;
	}
    }
    else
    {
	blackberry->serdata = NULL;
    }
    return(blackberry->serdata);
}

/**
	Free a Serial Data instance
*/
serdata_t *
FreeSerData(struct serdata_rec *serdata)
{
    if (serdata)
    {
    	if (serdata->mode)
    	{
	    free(serdata->mode);
    	}
	if (*serdata->blackberry->monitor_fd)
	{
	    (*serdata->blackberry->monitor_fd)(serdata->blackberry, serdata->master, NULL, serdata);
	}
	close(serdata->master);
	close(serdata->slave);
    	if (serdata->blackberry->tty_name)
    	{
#if 0
	    /* I don't think this is my memory to screw with */
	    free(serdata->blackberry->tty_name);
#else
	    serdata->blackberry->tty_name = NULL;
#endif
    	}
	serdata->blackberry->serctrl = FreeSerCtrl(serdata->blackberry->serctrl);
	BufferFreeData(&serdata->data);
    }
    free(serdata);
    return(NULL);
}
