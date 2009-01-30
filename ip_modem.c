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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pty.h>
#include <pthread.h>

#include "ip_modem.h"
#include "util.h"

#if 1
/* rws 30 Nov 2007
   I have no idea why this is needed, but it _must_ be written to the
   blackberry, and it get read from the blackberry _alot_.
 */
static char special_sauce[] = {0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12};
#endif

static void *
usb_reader(void *arg)
{
ip_modem *modem = (ip_modem *)arg;
int num_read;
char data[0x800];
int thread_done = (0 == 1);
static int max_read = 0;

    debug(9, "%s:%s(%d) - thread is alive\n",
	__FILE__, __FUNCTION__, __LINE__);
    while (! thread_done)
    {
	num_read = usb_bulk_read(modem->dev, modem->read_ep, data, sizeof(data), 0);
	if (num_read > 0)
	{
	    if (num_read >= sizeof(data))
	    {
	    	max_read = num_read;
		debug(0, "%s:%s(%d) - max_read %i\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    max_read);
	    }
	    modem->blackberry->stats.rx += num_read;
	    if (modem->slave == -1)
	    {
		if (num_read > (sizeof(special_sauce) - 4) && memcmp(&data[num_read - (sizeof(special_sauce) - 4)], &special_sauce[4], sizeof(special_sauce) - 4) == 0)
		{
		    //usb_bulk_write(modem->dev, modem->write_ep, data, num_read, 2000);
		    if (DebugLevel > 4)
		    {
			dump_hex("<--GPRS", data, num_read);
		    }
		}
		else
		{
		int written;

		    if (DebugLevel > 3)
		    {
			dump_hex("<--GPRS", data, num_read);
		    }
		    BufferAdd(&modem->pty_buffer, (unsigned char *)data, num_read);
		    do
		    {
			written = write(modem->master, BufferData(&modem->pty_buffer), BufferLen(&modem->pty_buffer));
			if (written > 0)
			{
			    /*
			    if (DebugLevel > 3)
			    {
				dump_hex("-->GPRS", BufferData(&modem->pty_buffer), written);
			    }
			    */
			    BufferPullHead(&modem->pty_buffer, written);
			    if (BufferLen(&modem->pty_buffer) > 0)
			    {
				debug(0, "%s:%s(%d) - written = %i left = %i\n",
				    __FILE__, __FUNCTION__, __LINE__,
				    written, BufferLen(&modem->pty_buffer));
			    }
			}
			else
			{
			    debug(0, "%s:%s(%d) - written = %i\n",
				__FILE__, __FUNCTION__, __LINE__,
				written);
			}
		    } while (written >= 0 && BufferLen(&modem->pty_buffer) > 0);
		}
	    }
	}
	else
	{
	    if (num_read < 0)
	    {
		debug(0, "%s:%s(%d) - num_read = %i \"%s\" \"%s\"\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    num_read,
		    strerror(errno), usb_strerror());
		thread_done = (1 == 1);
	    }
	    else
	    {
	    	//sleep(1);
	    }
	}
    }
    debug(0, "%s:%s(%d) - thread exit\n",
	__FILE__, __FUNCTION__, __LINE__);
    (*modem->blackberry->monitor_fd)(modem->blackberry, modem->master, NULL, modem);
    modem->usb_thread_id = (pthread_t)NULL;
    if (modem->slave >= 0)
    {
    	close(modem->slave);
    }
    if (modem->master >= 0)
    {
    	close(modem->master);
    }
    modem->blackberry->tty_name = NULL;
    BufferEmpty(&modem->pty_buffer);
    BufferEmpty(&modem->usb_buffer);
    free(modem);
    return(NULL);
}

static int
read_master(void *arg)
{
ip_modem *modem = (ip_modem *)arg;
int num_read;
char data[0x800];
static int max_read = 0;

    num_read = read(modem->master, data, sizeof(data));
    if (num_read > 0)
    {
    int written;

	if (num_read >= sizeof(data))
	{
	    max_read = num_read;
	    debug(0, "%s:%s(%d) - max_read %i\n",
		__FILE__, __FUNCTION__, __LINE__,
		max_read);
	}
	if (modem->slave != -1)
	{
	    /* rws 30 Nov 2007
	       Something must have opened the slave side of the pty.
	       Close our copy so that we can detect when they close
	       the slave side.
	     */
	    debug(4, "%s:%s(%d) - Slave opened\n",
		__FILE__, __FUNCTION__, __LINE__);
	    close(modem->slave);
	    modem->slave = -1;
#if 1
	    /* rws 30 Nov 2007
	       This is the magic to make the endpoint respond right after the device
	       is plugged in.
	     */
	    //BufferAdd(&modem->usb_buffer, (unsigned char *)special_sauce, sizeof(special_sauce));
	    usb_bulk_write(modem->dev, modem->write_ep, special_sauce, sizeof(special_sauce), 2000);
	    if (DebugLevel > 3)
	    {
		dump_hex("-->GPRS", special_sauce, sizeof(special_sauce));
	    }
#endif
	}
	BufferAdd(&modem->usb_buffer, (unsigned char *)data, num_read);
	do
	{
	    written = usb_bulk_write(modem->dev, modem->write_ep, (char *)BufferData(&modem->usb_buffer), BufferLen(&modem->usb_buffer), 2000);
	    if (written > 0)
	    {
		if (DebugLevel > 3)
		{
		    dump_hex("-->GPRS", BufferData(&modem->usb_buffer), written);
		}
		modem->blackberry->stats.tx += written;
		BufferPullHead(&modem->usb_buffer, written);
		if (BufferLen(&modem->usb_buffer) > 0)
		{
		    debug(0, "%s:%s(%d) - written = %i left = %i\n",
			__FILE__, __FUNCTION__, __LINE__,
			written, BufferLen(&modem->usb_buffer));
		}
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - written = %i\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    written);
	    }
	} while (written >= 0 && BufferLen(&modem->usb_buffer) > 0);
    }
    else
    {
	if (modem->slave == -1)
	{
	    /* rws 30 Nov 2007
	       Something must have closed the slave side of the pty.
	       Open our copy so that we can detect when they open
	       the slave side.
	     */
	    debug(4, "%s:%s(%d) - Slave closed\n",
		__FILE__, __FUNCTION__, __LINE__);
	    modem->slave = open(modem->blackberry->tty_name, O_RDWR | O_NOCTTY);
	    BufferEmpty(&modem->usb_buffer);
	    num_read = 0;
#if 0
	    /* rws 6 Dec 2007
	       This is a little drastic, but it is the only way I have been
	       able to make a second connection :(
	     */
	    usb_reset(modem->dev);
#else
	    /* rws 5 Jun 2008
	       This allows back to back ppp sessions without having to reset the usb!!!!
	       Thanks to a.herkey@comcast.net for pointing this out.
	     */
	    {
	    char to_command[] = {
		0x00, 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12};
	    /*
	    char disconnect[] = {
		0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12};
	    char unknown[] = {
		0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12};
	    char stop[] = {
		0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12};
		*/

		if (DebugLevel > 3)
		{
		    dump_hex("-->GPRS", to_command, sizeof(to_command));
		}
		usb_bulk_write(modem->dev, modem->write_ep, to_command, sizeof(to_command), 2000);
		/*
		dump_hex("-->GPRS", disconnect, sizeof(disconnect));
		usb_bulk_write(modem->dev, modem->write_ep, disconnect, sizeof(disconnect), 2000);
		dump_hex("-->GPRS", unknown, sizeof(unknown));
		usb_bulk_write(modem->dev, modem->write_ep, unknown, sizeof(unknown), 2000);
		dump_hex("-->GPRS", stop, sizeof(stop));
		usb_bulk_write(modem->dev, modem->write_ep, stop, sizeof(stop), 2000);
		*/
	    }
#endif
	}
	else
	{
	    debug(0, "%s:%s(%d) - num_read %i %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		num_read,
		num_read < 0 ? strerror(errno) : "");
	}
    }
    return(num_read);
}

static void
initialize(ip_modem *modem)
{
    debug(9, "%s:%s(%d)\n",
	__FILE__, __FUNCTION__, __LINE__);

    pthread_create(&modem->usb_thread_id, NULL, usb_reader, modem);
    (*modem->blackberry->monitor_fd)(modem->blackberry, modem->master, read_master, modem);
#if 0
    /* rws 30 Nov 2007
       This is the magic to make the endpoint respond right after the device
       is plugged in.
     */
    usb_bulk_write(modem->dev, modem->write_ep, special_sauce, sizeof(special_sauce), 2000);
#endif
}

ip_modem *
NewIpModem(BlackBerry_t *blackberry, usb_dev_handle *dev, int read_ep, int write_ep)
{
ip_modem *ret = NULL;

    debug(9, "%s:%s(%d) - read 0x%02x write 0x%02x %p\n",
	__FILE__, __FUNCTION__, __LINE__,
	read_ep, write_ep, blackberry->monitor_fd);

    if (usb_clear_halt(dev, read_ep) != 0)
    {
	debug(0, "%s:%s(%d) - clear_halt %i %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    read_ep,
	    strerror(errno));
    }
    if (usb_clear_halt(dev, write_ep) != 0)
    {
	debug(0, "%s:%s(%d) - clear_halt %i %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    write_ep,
	    strerror(errno));
    }
    ret = malloc(sizeof(ip_modem));
    if (ret)
    {
	if (openpty(&ret->master, &ret->slave, NULL, NULL, NULL) == 0)
	{
	    ret->initialize = initialize;
	    ret->dev = dev;
	    ret->read_ep = read_ep;
	    ret->write_ep = write_ep;
	    ret->blackberry = blackberry;
	    ret->blackberry->tty_name = ttyname(ret->slave);
	    ret->usb_thread_id = (pthread_t)NULL;
	    BufferInit(&ret->usb_buffer, 10, 0);
	    BufferInit(&ret->pty_buffer, 10, 0);
	    if (ret->blackberry->tty_name)
	    {
		debug(3, "%s:%s(%d) - pty name. \"%s\"\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    ret->blackberry->tty_name);
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - Getting pty name. %s\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    strerror(errno));
		free(ret);
		ret = NULL;
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - openpty %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		strerror(errno));
	    free(ret);
	    ret = NULL;
	}
    }
    else
    {
	errno = ENOMEM;
    }
    return(ret);
}
