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

    \brief The usb driver

	This is the driver for devices attached via the usb. Multiple
	devices can be added/removed from the usb on the fly.
	<P>The UI calls
	usb_bb_open with a function to call when a device is found. A
	thread is created that periodically scans the usb for new/removed
	devices. For each device, a thread is created to read from the
	usb endpoint. All data read from the endpoint is written to a
	pipe that the UI
	must monitor. The UI then calls the read function in the 
	BlackBerry_t struct. The driver will then read from the pipe,
	in the UI thread, and deal with the data.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/time.h>

#include <netinet/in.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <usb.h>
#include <signal.h>
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

#include "XmBlackBerry.h"
#include "util.h"
#include "bb_proto.h"
#include "buffer.h"
#include "desktop.h"
#include "serdata.h"
#include "serctrl.h"
#include "timer.h"
#include "ip_modem.h"
#include <openssl/sha.h>

/**
	Driver private structure that holds info related
	to an individual device on the usb bus. This is hung off
	the driver_private member of the BlackBerry_t structuue.
*/
typedef struct bb_usb_rec {
    usb_dev_handle *dev; /**< the usb handle */
    int interface; /**< the device interface that we are using */
    int read; /**< the read endpoint */
    int write; /**< the write endpoint */
    BlackBerry_t *blackberry; /**< all communication with the UI goes through this structure */
    BufferStruct data; /**< this is where data from the device is assembled into packets */
    int pipes[2]; /**< the pipes used to communicate to the UI */
    pthread_t reader_thread; /**< the thread that is reading from the usb device */
    Timer desktop_timer; /**< I was gonna use this to automatically disconnect the device after inactivity, still might. */
    int disconnected; /**< flag to indicate whether the device is there or not */
    uint32_t seed; /**< password seed */
    uint32_t seq; /**< the number of failed password attempts ?? */
    char *password; /**< the plain text password :( */
    ip_modem *modem;
} bb_usb_t;

/**
	Holds onto info about the usb buss
*/
struct usb_control_rec {
    unsigned long delay; /**< How often to scan the bus for new devices */
    int done; /**< flag to signal the usb monitor thread to exit */
    int num_busses; /**< the number of usb's */
    int num_devices; /**< the change in the number of devices since last scan */
    struct usb_bus *busses; /**< the usb structures */
    ListStruct blackberry_list; /**< a list of devices that I know about */
    BlackBerryCallbackProc new_device_callback; /**< The UI function to call when a new device is found. Supplied by the UI in the usb_bb_open call */
    void *client_data; /**< the client data from the usb_bb_open call */
#ifdef HAVE_PTHREAD_H
    pthread_t monitor_thread;
#endif
    struct sigaction old_alarm_action;
    struct sigaction alarm_action;
    struct itimerval itimer_value;
} usb_control;

/**
	Handle the sequence number that is part of the higher level
	protocols. Each open socket sends a sequence packet, in
	response to each packet it receives, that contains a one-up
	counter.
*/
static int
handle_sequence(uint32_t sequence, uint32_t *old_sequence)
{
int ret = 0;

    if (*old_sequence != sequence)
    {
	debug(0, "%s:%s(%d) - out of sequence. Got 0x%08x, expecting 0x%08x\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    sequence, *old_sequence);
	*old_sequence = sequence + 1;
    }
    else
    {
	(*old_sequence)++;
	ret = 1;
    }
    return(ret);
}

/**
	This is a separate thread for each device found on the USB. 
	This is necessary because the usb_bulk_read is a blocking call.
	It issues a bulk read and writes the data to a pipe.
	The disconnect_callback will be called if the device is un-plugged.
*/
static void *
usb_reader(void *arg)
{
bb_usb_t *bb = (bb_usb_t *)arg;
int num_read = 1;
int done = 0;
char *data;
int max_packet = 0x800;

    data = malloc(max_packet);
    while (! done && bb->blackberry && bb->blackberry->connected)
    {
	if (bb->blackberry->max_packet + 4 > max_packet)
	{
	char *new_buffer;
	int new_size;

	    new_size = bb->blackberry->max_packet + 5;
	    new_buffer = realloc(data, new_size);
	    if (new_buffer)
	    {
		max_packet = new_size;
		data = new_buffer;
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - %08x realloc was %i need %i. %s\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
		    max_packet, new_size,
		    strerror(errno));
	    }
	}
	num_read = usb_bulk_read(bb->dev, bb->read, &data[0], max_packet, 0);
	if (num_read > 0)
	{
	    if (num_read <= max_packet)
	    {
	    ssize_t written;

		if (bb->pipes[1] >= 0)
		{
		    written = write(bb->pipes[1], &data[0], num_read);
		    if (written >= 0)
		    {
			if (written < num_read)
			{
			    debug(0, "%s:%s(%d) - %08x short write fd %i %i of %i. %s\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
				bb->pipes[1],
				written, num_read,
				strerror(errno));
			    done = 1;
			}
		    }
		    else
		    {
			debug(0, "%s:%s(%d) - %08x write fd %i. %s\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
			    bb->pipes[1], strerror(errno));
			done = 1;
		    }
		}
		else
		{
		    debug(0, "%s:%s(%d) - %08x someone closed my pipe fd %i.\n",
			__FILE__, __FUNCTION__, __LINE__,
			bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
			bb->pipes[1]);
		    done = 1;
		}
	    }
	    else
	    {
		dump_hex("<==", data, num_read);
		debug(0, "%s:%s(%d) - %08x Need a buffer bigger than %i\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
		    num_read);
		done = 1;
	    }
	}
	else if (num_read == 0)
	{
	    debug(0, "%s:%s(%d) - %08x read ep %i %i\n",
		__FILE__, __FUNCTION__, __LINE__,
		bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
		bb->read, num_read);
#if 0
	    done = 1;
#else
	    /* Seem to get zero length reads sometimes when the
	       another device gets plugged in.
	     */
	    sleep(1);
#endif
	}
	else
	{
	    debug(0, "%s:%s(%d) - %08x read ep 0x%02x. %i %i %s (%s)\n",
		__FILE__, __FUNCTION__, __LINE__,
		bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
		bb->read, 
		num_read, errno, strerror(errno), usb_strerror());
	    switch (num_read)
	    {
	    case -75:
		sleep(1);
		usb_clear_halt(bb->dev, bb->read);
		usb_resetep(bb->dev, bb->read);
	    	break;
	    default:
		done = 1;
	    	break;
	    }
	}
    }
    debug(8, "%s:%s(%d) - %08x thread exit %i %i %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
	done, num_read,
	bb->blackberry ? bb->blackberry->connected : 0);
    if (bb->blackberry)
    {
	bb->blackberry->connected = 0;
	if (bb->blackberry->disconnect_callback)
	{
	    (*bb->blackberry->disconnect_callback)(bb->blackberry, bb->blackberry->client_data, NULL);
	}
	bb->blackberry->desktop = FreeDeskTop(bb->blackberry->desktop);
	bb->blackberry->bypass = FreeBypass(bb->blackberry->bypass);
	bb->blackberry->serctrl = FreeSerCtrl(bb->blackberry->serctrl);
	bb->blackberry->serdata = FreeSerData(bb->blackberry->serdata);
    }
    if (bb->pipes[1] >= 0)
    {
	write(bb->pipes[1], "\0", 1);
	close(bb->pipes[1]);
	bb->pipes[1] = -1;
    }
    free(data);
    bb->reader_thread = (pthread_t)NULL;
    return(NULL);
}

/**
	Write data to the device, adding the socket and size.
*/
static int
usb_write(bb_usb_t *bb, uint16_t socket, void *input, int len)
{
char *data = (char *)input;
int written = 0;
unsigned char *packet;

    debug(9, "%s:%s(%d)\n", __FILE__, __FUNCTION__, __LINE__);
    if (bb && bb->dev)
    {
	packet = malloc(len + 5);
	packet[0] = (socket >> 0) & 0xff;
	packet[1] = (socket >> 8) & 0xff;
	packet[2] = ((len + 4) >> 0) & 0xff;
	packet[3] = ((len + 4) >> 8) & 0xff;
	memcpy(&packet[4], data, len);
	written = usb_bulk_write(bb->dev, bb->write, (char *)packet, len + 4, 10000);
	if (written >=0 )
	{
	    if (bb->blackberry)
	    {
		bb->blackberry->stats.tx += written;
		if (DebugLevel > 5 && written > 0)
		{
		    dump_hex("==>", packet, DebugLevel > 6 ? written : (written > 16 ? 16 : written));
		}
		if (written < len + 4)
		{
		    debug(0, "%s:%s(%d) - Short write, was %i should be %i\n",
			__FILE__, __FUNCTION__, __LINE__,
			written, len + 4);
		}
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - write ep 0x%02x. %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		bb->write, strerror(errno));
	    if (bb->blackberry)
	    {
		bb->blackberry->connected = 0;
		/*
		if (bb->blackberry->disconnect_callback)
		{
		    (*bb->blackberry->disconnect_callback)(bb->blackberry, bb->blackberry->client_data);
		}
		*/
	    }
	}
	free(packet);
    }
    else
    {
    	written = -1;
    	errno = ECONNRESET;
    }
    return(written);
}

static uint16_t
get_short(void *input)
{
unsigned char *data = (unsigned char *)input;
int i;
uint16_t ret = 0;

    for (i = 0; i < 2; i++)
    {
    	ret += (data[i] << (i * 8)) & 0xffff;
    }
    return(ret);
}

static uint32_t
get_long(void *input)
{
unsigned char *data = (unsigned char *)input;
int i;
uint32_t ret = 0;

    for (i = 0; i < 4; i++)
    {
	ret += (data[i] << (i * 8)) & 0xffffffff;
    }
    return(ret);
}

/**
	Close the given socket to the device. This is used
	to dis-connect from the Desktop, without closing the
	whole device.
*/
static int
close_socket(BlackBerry_t *blackberry, uint16_t socket)
{
bb_usb_t *bb = (bb_usb_t *)blackberry->driver_private;
int written;
char packet[3];

    debug(9, "%s:%s(%d) - %08x %04x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    blackberry->pin,
    socket);


    packet[0] = 0x0b;
    packet[1] = (socket >> 0) & 0xff;
    packet[2] = (socket >> 8) & 0xff;
    written = usb_write(bb, 0, packet, sizeof(packet));
    return(written);
}

/**
	Completely close the device. Called by the UI.
*/
static void
bb_usb_close(BlackBerry_t *device)
{
bb_usb_t *bb = (bb_usb_t *)device->driver_private;

    debug(9, "%s:%s(%d) - %08x %sconnected\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	device->pin,
    	device->connected ? "" : "not ");
    if (device->connected)
    {
	device->connected = 0;
	if (bb->dev)
	{
	    if (device->desktop && device->desktop->socket != 0 &&
	                           device->desktop->socket != (uint16_t)-1)
	    {
	    uint16_t socket;

		socket = device->desktop->socket;
		device->desktop->socket = 0;
		if (device->close_socket)
		{
		    device->close_socket(device, socket);
		}
	    }
	    usb_release_interface(bb->dev, bb->interface);
#if 1
	    /* rws 28 Nov 2007
	       This seems to cause the IP Modem on the 8800 from working
	       until it gets a three finger salute :( Not sure if any of
	       the other devices need it or not. Unfortunately, the 8700
	       using serdata needs the reset for the next connection :(
	     */
#if 0
	    /* rws 30 Nov 2007
	       Not needed since I found the magic sequence to keep modem
	       going after a close.
	     */
	    if (bb->modem)
	    {
	    }
	    else
#endif
	    {
		usb_reset(bb->dev);
	    }
#endif
	    usb_close(bb->dev);
	    bb->dev = NULL;
	}
    }
}

/**
	Try to say hello to the device. If it responds appropriately
	start the whole ball rolling .... If this fails :(
*/
static int
say_hello(bb_usb_t *bb)
{
char hello[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x00/*, 0x00, 0xa8, 0x18, 0xda, 0x8d, 0x6c, 0x02, 0x00, 0x00*/ };
ssize_t written;
int ret = 0;
int local_errno;

    debug(9, "%s:%s(%d) - read %i write %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	bb->read, bb->write);
    hello[2] = sizeof(hello);
#if 0
    written = usb_bulk_write(bb->dev, bb->write, hello, sizeof(hello), 1000);
#else
    written = usb_write(bb, 0, &hello[4], sizeof(hello) - 4);
#endif
    if (written == sizeof(hello))
    {
    char data[32];
    int num_read;

	memset(data, 0, sizeof(data));
	if (DebugLevel > 5)
	{
	    dump_hex("hello ==>", hello, DebugLevel > 6 ? written : (written > 16 ? 16 : written));
	}
	num_read = usb_bulk_read(bb->dev, bb->read, data, sizeof(data), 1000);
	if (num_read == sizeof(hello))
	{
	    if (DebugLevel > 5)
	    {
		dump_hex("hello <==", data, DebugLevel > 6 ? num_read : (num_read > 16 ? 16 : num_read));
	    }
	    if (memcmp(&hello[0], &data[0], 4) == 0 &&
	        memcmp(&hello[5], &data[5], sizeof(hello) - 5) == 0 &&
	        hello[4] + 1 == data[4])
	    {
		ret = 1;
	    }
	    else
	    {
		dump_hex("<==", data, num_read);
		debug(0, "%s:%s(%d) - Read %i byte%s of %i from ep %02x. Didn't understand the hello.\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    num_read < 0 ? 0 : num_read,
		    num_read == 1 ? "" : "s",
		    sizeof(hello),
		    bb->read);
	    }
	}
	else
	{
	    local_errno = errno;
	    if (num_read >= 0)
	    {
		dump_hex("<==", data, num_read);
	    }
	    debug(0, "%s:%s(%d) - Read %i byte%s of %i from ep %02x. Didn't hear hello. %s %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		num_read < 0 ? 0 : num_read,
		num_read == 1 ? "" : "s",
		sizeof(hello),
		bb->read,
		num_read < 0 ? strerror(local_errno) : "",
		num_read < 0 ? usb_strerror() : "");
	}
    }
    else
    {
	local_errno = errno;
	if (written >= 0)
	{
	    dump_hex("==>", hello, sizeof(hello));
	}
	debug(0, "%s:%s(%d) - Wrote %i byte%s of %i to ep %02x. Couldn't say hello. %s %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    written < 0 ? 0 : written,
	    written == 1 ? "" : "s",
	    sizeof(hello),
	    bb->write,
	    written < 0 ? strerror(local_errno) : "",
	    written < 0 ? usb_strerror() : "");
    }
    return(ret);
}

/**
	Send 1 queued packet to the device
*/
static void
process_queue(BlackBerry_t *blackberry)
{
bb_usb_t *bb = (bb_usb_t *)blackberry->driver_private;
BufferStruct *buffer;

    debug(7, "%s:%s(%d) - len = %i last = %p %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	ListLength(&blackberry->tx_queue),
	bb->blackberry->last_packet,
	BufferLen(&bb->data));
    buffer = ListDequeueHead(&blackberry->tx_queue);
    if (buffer)
    {
    int written;

	written = usb_bulk_write(bb->dev, bb->write, (char *)BufferData(buffer), BufferLen(buffer), 1000);
	bb->blackberry->stats.tx += written;
	if (written == BufferLen(buffer))
	{
	    if (DebugLevel > 5)
	    {
		dump_hex("==>", BufferData(buffer), DebugLevel > 6 ? written : (written > 16 ? 16 : written));
	    }
	    //usleep(100 * BufferLen(buffer));
	}
	else
	{
	    debug(0, "%s:%s(%d) - short write %i %i %s %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		written, BufferLen(buffer),
		written < 0 ? strerror(errno) : "",
		written < 0 ? usb_strerror() : "");
	}

	if(bb->blackberry->last_packet)
	{
	    bb->blackberry->last_packet = BufferFree(bb->blackberry->last_packet);
	}
	bb->blackberry->last_packet = buffer;
    }
}

/**
	Called by the Desktop to send a packet. Simply adds it
	to the tx_queue. The BlackBerry acknowledges every packet
	sent to it, so we have to wait for the ack before the
	next packet is sent.
*/
static int
bb_usb_send_packet(BlackBerry_t *device, uint16_t socket, void *input, int len)
{
int ret = 0;
BufferStruct *packet;
bb_usb_t *bb = (bb_usb_t *)device->driver_private;

    debug(9, "%s:%s(%d) - last %p\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	device->last_packet);
    packet = BufferNew(len, 2 + 2);
    if (packet)
    {
    unsigned char *skt;

	if (device->desktop && socket == device->desktop->socket)
	{
	    debug(8, "%s:%s(%d) - %08x\n",
		__FILE__, __FUNCTION__, __LINE__,
		device->pin);
	    TimerRemove(bb->desktop_timer);
	    if (bb->disconnected)
	    {
		ret = -1;
		errno = ENOTCONN;
	    }
	    else
	    {
//		bb->desktop_timer = TimerAdd(30000, close_desktop, bb);
	    }
	}
	if (ret == 0)
	{
	    BufferAdd(packet, input, len);
	    skt = BufferAddHead(packet, NULL, 4);
	    skt[0] = (socket >> 0) &0xff;
	    skt[1] = (socket >> 8) &0xff;
	    skt[2] = (BufferLen(packet) >> 0) &0xff;
	    skt[3] = (BufferLen(packet) >> 8) &0xff;
	    ListQueueTail(&device->tx_queue, packet);
	    ret = BufferLen(packet);
	    if (device->last_packet == NULL)
	    {
		process_queue(device);
	    }
	}
    }
    else
    {
	ret = -1;
	errno = ENOMEM;
    }
    return(ret);
}

static int
get_info(bb_usb_t *bb, unsigned char type, uint16_t arg1, uint16_t arg2)
{
int ret = 0;
char request[] = { 0x05, 0xff, 0x00, 0x01, 0x08, 0x00, 0x04, 0x00 };

#if 1
    /* rws 28 Sep 2008
       These don't seem to matter
     */
    arg1 = 0;
    type = 0;
#endif
    request[3] = type;
    request[4] = (arg1 >> 0) & 0xff;
    request[5] = (arg1 >> 8) & 0xff;
    request[6] = (arg2 >> 0) & 0xff;
    request[7] = (arg2 >> 8) & 0xff;
    if (bb_usb_send_packet(bb->blackberry, 0, request, sizeof(request)) >= sizeof(request))
    {
	ret = 1;
    }
    else
    {
	debug(0, "%s:%s(%d) - Error requesting the device info\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
    return(ret);
}

/**
	Get the PIN out of the device
*/
static int
get_pin(bb_usb_t *bb)
{
int ret = 0;
#if 0
char pin[] = { 0x05, 0xff, 0x00, 0x01, 0x08, 0x00, 0x04, 0x00 };

    if (bb_usb_send_packet(bb->blackberry, 0, pin, sizeof(pin)) >= sizeof(pin))
    {
	ret = 1;
    }
    else
    {
	debug(0, "%s:%s(%d) - Error requesting the PIN\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
#else
    ret = get_info(bb, 1, 8, 4);
#endif
    return(ret);
}

/**
	Get an extended description from the device
*/
static int
get_description(bb_usb_t *bb)
{
int ret = 0;
#if 0
char desc[] = { 0x05, 0xff, 0x00, 0x02, 0x08, 0x00, 0x02, 0x00 };

    if (bb_usb_send_packet(bb->blackberry, 0, desc, sizeof(desc)) >= sizeof(desc))
    {
	ret = 1;
    }
    else
    {
	debug(0, "%s:%s(%d) - Error requesting the description\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
#else
    ret = get_info(bb, 2, 8, 2);
#endif
    return(ret);
}

/**
	Select an operating mode. Each mode will have it's
	own socket.
*/
static int
select_mode(BlackBerry_t *blackberry, char *mode)
{
bb_usb_t *bb = (bb_usb_t *)blackberry->driver_private;
int ret = 0;
char packet[16 + 4];

    debug(9, "%s:%s(%d) - \"%s\"\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	mode);
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x07;
    packet[1] = 0xff;
    packet[2] = 0x00;
    /*packet[3] = 0x05;*/
    packet[3] = strlen(mode);
    memcpy(&packet[4], mode, strlen(mode) + 1 < sizeof(packet) - 4 ? strlen(mode) + 1 : sizeof(packet) - 4);
    if (bb_usb_send_packet(bb->blackberry, 0, packet, sizeof(packet)) >= sizeof(packet))
    {
	ret = 1;
    }
    else
    {
	debug(0, "%s:%s(%d) - Error selecting the mode\n",
	    __FILE__, __FUNCTION__, __LINE__);
    }
    return(ret);
}

#if 0
static void
close_desktop(Timer timer, void *client_data)
{
bb_usb_t *bb = (bb_usb_t *)client_data;
char packet[3];
int written;

    debug(9, "%s:%s(%d) - %08x\n",
	__FILE__, __FUNCTION__, __LINE__,
	bb->blackberry->pin);
    bb->desktop_timer = NULL;
    packet[0] = 0x0b;
    packet[1] = (bb->blackberry->desktop->socket >> 0) & 0xff;
    packet[2] = (bb->blackberry->desktop->socket >> 8) & 0xff;
    written = usb_write(bb, 0, packet, sizeof(packet));
}
#endif

/**
	open the given socket to the device.
*/
static int
open_socket(bb_usb_t *bb, int socket)
{
int ret = 0;
unsigned char cmd[] = {0x0a, 0x00, 0x00/*, 0x06*/};

    cmd[1] = (socket >> 0) & 0xff;
    cmd[2] = (socket >> 8) & 0xff;
    ret = bb_usb_send_packet(bb->blackberry, 0, cmd, sizeof(cmd));
    return(ret);
}

/**
	send a password to the device. This is called by the UI
	in response to a get_password call.
*/
static void
send_password(struct BlackBerry_rec *blackberry, char *password)
{
bb_usb_t *bb = (bb_usb_t *)blackberry->driver_private;
unsigned char pwdigest[SHA_DIGEST_LENGTH];
unsigned char prefixedhash[SHA_DIGEST_LENGTH + 4];
unsigned char challenge_response[SHA_DIGEST_LENGTH + 8];

    debug(9, "%s:%s(%d) - %08x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	blackberry ? blackberry->pin : (uint32_t)-1);

    bb->password = strdup(password);
    /* first, hash the password by itself */
    SHA1((unsigned char *) password, strlen(password), pwdigest);

    /* prefix the resulting hash with the provided seed */
    prefixedhash[0] = (bb->seed >> 0) & 0xff;
    prefixedhash[1] = (bb->seed >> 8) & 0xff;
    prefixedhash[2] = (bb->seed >> 16) & 0xff;
    prefixedhash[3] = (bb->seed >> 24) & 0xff;
    memcpy(&prefixedhash[4], pwdigest, SHA_DIGEST_LENGTH);

    /* hash again */
    SHA1((unsigned char *) prefixedhash, SHA_DIGEST_LENGTH + 4, pwdigest);

#if 0
    challenge_response[0] = (bb->seq >> 0) & 0xff;
#else
    challenge_response[0] = 0x0f;
#endif
    challenge_response[1] = (bb->seq >> 8) & 0xff;
    challenge_response[2] = (bb->seq >> 16) & 0xff;
    challenge_response[3] = (bb->seq >> 24) & 0xff;
    challenge_response[4] = 0;
    challenge_response[5] = 0;
    challenge_response[6] = 20;
    challenge_response[7] = 0;
    memcpy(&challenge_response[8], pwdigest, SHA_DIGEST_LENGTH);

#if 0
    dump_hex("==>", challenge_response, sizeof(challenge_response));
#endif
    bb_usb_send_packet(blackberry, 0, 
      challenge_response, sizeof(challenge_response)); 
}

/**
	Assemble the byte stream, read from the pipe, into
	packets to be dealt with. Data from socket 0 is
	dealt with here. Data from other sockets is passed on
	to the sub-system receive function.
*/
static int
process(bb_usb_t *bb, unsigned char byte)
{
int ret = 0;
unsigned char *data;

    debug(10, "%s:%s(%d) - %08x 0x%04x\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	bb->blackberry ? bb->blackberry->pin : (uint32_t)-1,
    	BufferLen(&bb->data));
    BufferAdd(&bb->data, &byte, 1);
    data = BufferData(&bb->data);
    if (BufferLen(&bb->data) > 4)
    {
    uint16_t size = get_short(&data[2]);

    	if (BufferLen(&bb->data) >= size)
    	{
	uint8_t type = data[4];
	uint16_t socket = get_short(&data[0]);

	    if (bb->blackberry->last_packet)
	    {
		bb->blackberry->last_packet = BufferFree(bb->blackberry->last_packet);
	    }
	    if (socket == 0) /* This is low level usb specific device stuff */
	    {
		debug(9, "%s:%s(%d) - %08x 0x%04x 0x%02x\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    bb->blackberry->pin,
		    BufferLen(&bb->data),
		    type);
		if (DebugLevel > 5)
		{
		    dump_hex("<==", data, DebugLevel > 6 ? BufferLen(&bb->data) : (BufferLen(&bb->data) < 16 ? BufferLen(&bb->data) : 16));
		}
		switch (type)
		{
		case 0x06: /* login/device-info */
		    switch ((data[11] << 8) + (data[10] << 0))
		    {
		    case 4: /* pin */
			if (size >= 16 + 4) /* login */
			{
			    bb->blackberry->pin = get_long(&data[16]);
			    bb->blackberry->max_packet = get_short(&data[9]);
			    //bb->blackberry->max_packet = (data[8] << 8) + data[9];
			    debug(6, "%s:%s(%d) - Got the PIN %08x max_packet 0x%04x\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->max_packet);
			    get_description(bb);
			    ret = 1;
			}
			else
			{
			    debug(0, "%s:%s(%d) - %08x \"%s\"\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->desc ? bb->blackberry->desc : "");
			    dump_hex("<==", data, BufferLen(&bb->data));
			}
			break;
		    case 2: /* description */
			if (size > 28) /* device-info response to get_description */
			{
			    if (bb->blackberry->desc)
			    {
				free(bb->blackberry->desc);
			    }
			    bb->blackberry->desc = strdup((char *)&data[28]);
			    bb->blackberry->series = data[26] << 8 | data[25];
			    debug(6, "%s:%s(%d) - %08x \"%s\" 0x%04x\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->desc,
				bb->blackberry->series);
			    bb->blackberry->select_mode = select_mode;
			    bb->blackberry->close_socket = close_socket;
			    bb->blackberry->desktop = NewDeskTop(bb->blackberry);
			    if (bb->blackberry->connect_to_desktop)
			    {
				select_mode(bb->blackberry, bb->blackberry->desktop->mode);
			    }
			    else
			    {
				if (bb->modem)
				{
				    (*bb->modem->initialize)(bb->modem);
				}
				else
				{
				    bb->blackberry->serdata = NewSerData(bb->blackberry);
				    if (bb->blackberry->serdata)
				    {
					select_mode(bb->blackberry, bb->blackberry->serdata->mode);
				    }
				}
			    }
			    ret = 1;
			}
			else
			{
			    debug(0, "%s:%s(%d) - %08x \"%s\"\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->desc ? bb->blackberry->desc : "");
			    dump_hex("<==", data, BufferLen(&bb->data));
			}
			break;
		    default:
			debug(0, "%s:%s(%d) - %08x \"%s\"\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    bb->blackberry->pin,
			    bb->blackberry->desc ? bb->blackberry->desc : "");
			dump_hex("<==", data, BufferLen(&bb->data));
			break;
		    }
		    break;
		case 0x08: /* mode selected */
		    if (bb->blackberry->desktop && size > (7 + strlen(bb->blackberry->desktop->mode) + 1) && memcmp(&BufferData(&bb->data)[8], bb->blackberry->desktop->mode, strlen(bb->blackberry->desktop->mode)) == 0)
		    {
			bb->blackberry->desktop->socket = get_short(&data[5]);
			bb->blackberry->desktop->sequence = 0;
			open_socket(bb, bb->blackberry->desktop->socket);
			ret = 1;
		    }
		    else if (bb->blackberry->bypass && size > (7 + strlen(bb->blackberry->bypass->mode) + 1) && memcmp(&BufferData(&bb->data)[8], bb->blackberry->bypass->mode, strlen(bb->blackberry->bypass->mode)) == 0)
		    {
			bb->blackberry->bypass->socket = get_short(&data[5]);
			bb->blackberry->bypass->sequence = 0;
			open_socket(bb, bb->blackberry->bypass->socket);
			ret = 1;
		    }
		    else if (bb->blackberry->serdata && size > (7 + strlen(bb->blackberry->serdata->mode) + 1) && memcmp(&BufferData(&bb->data)[8], bb->blackberry->serdata->mode, strlen(bb->blackberry->serdata->mode)) == 0)
		    {
			bb->blackberry->serdata->socket = get_short(&data[5]);
			bb->blackberry->serdata->sequence = 0;
			open_socket(bb, bb->blackberry->serdata->socket);
			ret = 1;
		    }
		    else if (bb->blackberry->serctrl && size > (7 + strlen(bb->blackberry->serctrl->mode) + 1) && memcmp(&BufferData(&bb->data)[8], bb->blackberry->serctrl->mode, strlen(bb->blackberry->serctrl->mode)) == 0)
		    {
			bb->blackberry->serctrl->socket = get_short(&data[5]);
			bb->blackberry->serctrl->sequence = 0;
			open_socket(bb, bb->blackberry->serctrl->socket);
			ret = 1;
		    }
		    else
		    {
			if (DebugLevel < 6)
			{
			    dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
			}
			debug(0, "%s:%s(%d) - %08x Unknown mode set \"%s\"\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    bb->blackberry->pin,
			    &BufferData(&bb->data)[8]);
		    }
		    break;
		case 0x09: /* failed mode select */
		    debug(0, "%s:%s(%d) - %08x set mode \"%s\" failed.\n",
			__FILE__, __FUNCTION__, __LINE__,
			bb->blackberry->pin,
			&BufferData(&bb->data)[8]);
		    break;
		case 0x0e: /* authentication challenge */
			if (BufferLen(&bb->data) > 15)
			{
			    bb->seed = get_long(&data[12]);
#if 1
			    /* rws 17 Sep 2006
			       This is not a 4 byte sequence number. The first
			       byte is the packet type. This is getting fixed
			       up in send_password for now.
			     */
			    bb->seq = get_long(&data[4]);
#endif
			    bb->seq += 0x1000001;
			    bb->blackberry->send_password = send_password;
			    if (bb->password)
			    {
			    char *tmp = bb->password;

			    	send_password(bb->blackberry, tmp);
			    	free(tmp);
			    }
			    else
			    {
				if (bb->blackberry->get_password)
				{
				    (*bb->blackberry->get_password)(bb->blackberry);
				}
			    }
			    ret = 1;
			}
			break;
		case 0x11: /* authentication failed */
			debug(0, "%s:%s(%d) - %08x Authentication failed\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    bb->blackberry->pin);
			if (DebugLevel < 6)
			{
			    dump_hex("<==", data, BufferLen(&bb->data));
			}
			if (BufferLen(&bb->data) > 15)
			{
			    bb->seed = get_long(&data[12]);
#if 1
			    /* rws 17 Sep 2006
			       This is not a 4 byte sequence number. The first
			       byte is the packet type. This is getting fixed
			       up in send_password for now. I suspect this
			       has something to do with the number of failed
			       attempts ...
			     */
			    bb->seq = get_long(&data[4]);
#endif
			    bb->seq += 0x1000001;
			    if (bb->password)
			    {
				free(bb->password);
				bb->password = NULL;
			    }
			    if (bb->blackberry->get_password)
			    {
				(*bb->blackberry->get_password)(bb->blackberry);
			    }
			}
			break;
		case 0x10: /* socket opened */
		    if (size > 6)
		    {
		    uint16_t socket;

			socket = get_short(&data[5]);
			if (bb->blackberry->desktop && socket == bb->blackberry->desktop->socket)
			{
			    debug(8, "%s:%s(%d) - %08x socket %04x opened for \"%s\"\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->desktop->socket,
				bb->blackberry->desktop->mode);
			    bb->blackberry->desktop->connected = 1;
			    bb->disconnected = 0;
			    bb->blackberry->desktop->send_packet = bb_usb_send_packet;
			    if (bb->blackberry->desktop->initialize)
			    {
				ret = (*bb->blackberry->desktop->initialize)(bb->blackberry->desktop);
			    }
#if 1
			    if (bb->modem)
			    {
			    	(*bb->modem->initialize)(bb->modem);
			    }
			    else
			    {
				if (!bb->blackberry->serdata)
				{
				    bb->blackberry->serdata = NewSerData(bb->blackberry);
				    if (bb->blackberry->serdata)
				    {
					select_mode(bb->blackberry, bb->blackberry->serdata->mode);
				    }
				}
			    }
#endif
			}
			else if (bb->blackberry->bypass && socket == bb->blackberry->bypass->socket)
			{
			    debug(8, "%s:%s(%d) - %08x socket %04x opened for \"%s\"\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->bypass->socket,
				bb->blackberry->bypass->mode);
			    bb->blackberry->bypass->send_packet = bb_usb_send_packet;
			    if (bb->blackberry->bypass->initialize)
			    {
				ret = (*bb->blackberry->bypass->initialize)(bb->blackberry->bypass);
			    }
			}
			else if (bb->blackberry->serdata && socket == bb->blackberry->serdata->socket)
			{
			    debug(8, "%s:%s(%d) - %08x socket %04x opened for \"%s\"\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->serdata->socket,
				bb->blackberry->serdata->mode);
#if 1
			    bb->blackberry->serctrl = NewSerCtrl(bb->blackberry);
			    select_mode(bb->blackberry, bb->blackberry->serctrl->mode);
#endif
			    bb->blackberry->serdata->send_packet = bb_usb_send_packet;
			    if (bb->blackberry->serdata->initialize)
			    {
				ret = (*bb->blackberry->serdata->initialize)(bb->blackberry->serdata);
			    }
			}
			else if (bb->blackberry->serctrl && socket == bb->blackberry->serctrl->socket)
			{
			    debug(8, "%s:%s(%d) - %08x socket %04x opened for \"%s\"\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				bb->blackberry->serctrl->socket,
				bb->blackberry->serctrl->mode);
			    bb->blackberry->serctrl->send_packet = bb_usb_send_packet;
			    if (bb->blackberry->serctrl->initialize)
			    {
				ret = (*bb->blackberry->serctrl->initialize)(bb->blackberry->serctrl);
			    }
			}
			else
			{
			    if (DebugLevel < 6)
			    {
				dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
			    }
			    debug(0, "%s:%s(%d) - %08x Don't know who requested socket %04x.\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				socket);
			}
			ret = 1;
		    }
		    else
		    {
			if (DebugLevel < 6)
			{
			    dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
			}
			debug(0, "%s:%s(%d) - %08x \"%s\"\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    bb->blackberry->pin,
			    bb->blackberry->desc ? bb->blackberry->desc : "");
		    }
		    break;
		case 0x0c: /* socket closed */
		    if (size > 6)
		    {
		    uint16_t socket;

			socket = get_short(&data[5]);
			if (bb->blackberry->desktop && socket == bb->blackberry->desktop->socket)
			{
			    debug(4, "%s:%s(%d) - %08x desktop closed\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin);

			    bb->blackberry->desktop->connected = 0;
			    bb->blackberry->desktop->socket = 0;
			    /*
			    bb->disconnected = 1;
			    */
			}
			else if (bb->blackberry->bypass && socket == bb->blackberry->bypass->socket)
			{
			    debug(0, "%s:%s(%d) - %08x bypass closed\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin);
			}
			else if (bb->blackberry->serdata && socket == bb->blackberry->serdata->socket)
			{
			    debug(0, "%s:%s(%d) - %08x serdata closed\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin);
			}
			else if (bb->blackberry->serctrl && socket == bb->blackberry->serctrl->socket)
			{
			    debug(0, "%s:%s(%d) - %08x serctrl closed\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin);
			}
			else
			{
			    if (DebugLevel < 6)
			    {
				dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
			    }
			    debug(0, "%s:%s(%d) - %08x Don't know who belongs to socket %04x.\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin,
				socket);
			}
		    }
		    else
		    {
			if (DebugLevel < 6)
			{
			    dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
			}
			debug(0, "%s:%s(%d) - %08x \"%s\"\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    bb->blackberry->pin,
			    bb->blackberry->desc ? bb->blackberry->desc : "");
		    }
		    break;
		case 0x13: /* sequence packet One for each open socket */
		    if (size > 8)
		    {
		    uint32_t sequence = get_short(&data[8]);
		    uint8_t socket = data[5];

			if (bb->blackberry->desktop && socket == bb->blackberry->desktop->socket)
			{
			    handle_sequence(sequence, &bb->blackberry->desktop->sequence);
			}
			else if (bb->blackberry->bypass && socket == bb->blackberry->bypass->socket)
			{
			    handle_sequence(sequence, &bb->blackberry->bypass->sequence);
			}
			else if (bb->blackberry->serdata && socket == bb->blackberry->serdata->socket)
			{
			    handle_sequence(sequence, &bb->blackberry->serdata->sequence);
			}
			else if (bb->blackberry->serctrl && socket == bb->blackberry->serctrl->socket)
			{
			    handle_sequence(sequence, &bb->blackberry->serctrl->sequence);
			}
			else
			{
			    debug(0, "%s:%s(%d) - %08x sequence for unknown subsystem %04x\n",
				__FILE__, __FUNCTION__, __LINE__,
				bb->blackberry->pin, socket);
			}
			ret = 1;
		    }
		    else
		    {
			if (DebugLevel < 6)
			{
			    dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
			}
			debug(0, "%s:%s(%d) - %08x out of sequence. Expecting 0x%08x\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    bb->blackberry->pin,
			    bb->blackberry->desktop->sequence);
		    }
		    break;
		case 0x02: /* hello response */
		default:
		    if (DebugLevel < 6)
		    {
			dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
		    }
		    debug(0, "%s:%s(%d) - %08x Unknown packet type 0x%02x\n",
			__FILE__, __FUNCTION__, __LINE__,
			bb->blackberry->pin, type);
		    ret = 1;
		    break;
		}
	    }
	    else if (bb->blackberry->desktop && socket == bb->blackberry->desktop->socket)
	    {
		if (DebugLevel > 5)
		{
		    dump_hex("<==", data, DebugLevel > 6 ? BufferLen(&bb->data) : (BufferLen(&bb->data) < 16 ? BufferLen(&bb->data) : 16));
		}
		if (bb->blackberry->desktop->receive)
		{
		    (*bb->blackberry->desktop->receive)(bb->blackberry->desktop, &data[4], BufferLen(&bb->data) - 4);
		}
		ret = 1;
	    }
	    else if (bb->blackberry->bypass && socket == bb->blackberry->bypass->socket)
	    {
		if (DebugLevel > 5)
		{
		    dump_hex("<==", data, DebugLevel > 6 ? BufferLen(&bb->data) : (BufferLen(&bb->data) < 16 ? BufferLen(&bb->data) : 16));
		}
		if (bb->blackberry->bypass->receive)
		{
		    (*bb->blackberry->bypass->receive)(bb->blackberry->bypass, &data[4], BufferLen(&bb->data) - 4);
		}
		ret = 1;
	    }
	    else if (bb->blackberry->serdata && socket == bb->blackberry->serdata->socket)
	    {
		if (DebugLevel > 5)
		{
		    dump_hex("<==", data, DebugLevel > 6 ? BufferLen(&bb->data) : (BufferLen(&bb->data) < 16 ? BufferLen(&bb->data) : 16));
		}
		if (bb->blackberry->serdata->receive)
		{
		    (*bb->blackberry->serdata->receive)(bb->blackberry->serdata, &data[4], BufferLen(&bb->data) - 4);
		}
		ret = 1;
	    }
	    else if (bb->blackberry->serctrl && socket == bb->blackberry->serctrl->socket)
	    {
		if (DebugLevel > 5)
		{
		    dump_hex("<==", data, DebugLevel > 6 ? BufferLen(&bb->data) : (BufferLen(&bb->data) < 16 ? BufferLen(&bb->data) : 16));
		}
		if (bb->blackberry->serctrl->receive)
		{
		    (*bb->blackberry->serctrl->receive)(bb->blackberry->serctrl, &data[4], BufferLen(&bb->data) - 4);
		}
		ret = 1;
	    }
	    else
	    {
		dump_hex("<==", BufferData(&bb->data), BufferLen(&bb->data));
		debug(0, "%s:%s(%d) - %08x Unknown socket %04x\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    bb->blackberry->pin, socket);
	    }
	    BufferEmpty(&bb->data);
	    if (bb->blackberry->last_packet == NULL)
	    {
		/*
		    Some of the stuff above may have sent out data, so don't
		    send anymore if that is the case.
		*/
		process_queue(bb->blackberry);
	    }
    	}
    }
    else
    {
    	ret = 1;
    }
    return(ret);
}

/**
	read from the device pipe. This is called from the UI when
	it determines that there is data on blackberry->fd.
*/
static ssize_t
bb_usb_read(BlackBerry_t *device)
{
char data[0x404]; /* This should be max_packet from the device */
ssize_t num_read;

    debug(9, "%s:%s(%d) - read fd=%i\n",
	__FILE__, __FUNCTION__, __LINE__,
	device->fd);
    num_read = read(device->fd, &data[0], sizeof(data));
    if (num_read >= 0)
    {
    int i;

	device->stats.rx += num_read;
	if (num_read > 0)
	{
#if 0
	    dump_hex("bb_usb_read", data, num_read);
#endif
	    for (i = 0; i < num_read; i++)
	    {
		process(device->driver_private, data[i]);
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - %08x eof %i. %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		device->pin,
		device->fd,
		strerror(errno));
	    close(device->fd);
	    num_read = ECONNABORTED;
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - %08x read %i. %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    device->pin,
	    device->fd,
	    strerror(errno));
    }
    return(num_read);
}

/**
	Create the pipes and the reader thread for a new device. The
	driver simply reads the device endpoint and writes the data
	to a pipe. It is the responsibility of the UI to monitor
	the blackberry->fd file descriptor and call the
	blackberry->read function when data is available. This means
	that the data read will be in the UI thread.
*/
static int
start_reader(bb_usb_t *bb)
{
int ret = 0;

    if (pipe(bb->pipes) == 0)
    {
	bb->blackberry->fd = bb->pipes[0];
	if (pthread_create(&bb->reader_thread, NULL, usb_reader, bb) == 0)
	{
	    BufferInit(&bb->data, 0x200, 0);
	    if (get_pin(bb))
	    {
		ret = 1;
	    }
	    else
	    {
		debug(0, "%s:%s(%d) - Error requesting the PIN\n",
		    __FILE__, __FUNCTION__, __LINE__);
	    }
	}
	else
	{
	    debug(0, "%s:%s(%d) - Couldn't create a thread %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		strerror(errno));
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - Couldn't create pipes %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    strerror(errno));
    }
    return(ret);
}

/**
	Say hello to the given endpoint pair. If a valid response
	is received, create a new device, start a thread to read
	the endpoint, and call the callback that was specified 
	in the bb_usb_open call from the UI. This will be from
	the monitor thread, which is _not_ the UI thread.
*/
static int
check_endpoint_pair(bb_usb_t *bb)
{
int ret = 0;

    debug(9, "%s:%s(%d) - check %i %i\n",
	__FILE__, __FUNCTION__, __LINE__,
	bb->read, bb->write);
    if (say_hello(bb))
    {
	debug(0, "%s:%s(%d) - found one %i %i\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    bb->read & USB_ENDPOINT_ADDRESS_MASK, bb->write & USB_ENDPOINT_ADDRESS_MASK);
    	bb->blackberry = NewBlackBerry();
    	bb->blackberry->driver_private = bb;
    	bb->blackberry->connected = 1;
    	bb->blackberry->close = bb_usb_close;
    	bb->blackberry->read = bb_usb_read;
    	bb->blackberry->client_data = usb_control.client_data;
    	if (start_reader(bb))
    	{
	    if (usb_control.new_device_callback)
	    {
		(*usb_control.new_device_callback)(bb->blackberry, usb_control.client_data, NULL);
	    }
	    ListQueueTail(&usb_control.blackberry_list, bb->blackberry);
	    ret = 1;
    	}
    	else
    	{
	    debug(0, "%s:%s(%d) - thread create failed %i %i\n",
		__FILE__, __FUNCTION__, __LINE__,
		bb->read, bb->write);
	    bb->blackberry = FreeBlackBerry(bb->blackberry);
    	}
    }
    else
    {
	debug(0, "%s:%s(%d) - Couldn't say hello on endpoint pair %i %i\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    bb->read & USB_ENDPOINT_ADDRESS_MASK, bb->write & USB_ENDPOINT_ADDRESS_MASK);
    }
    return(ret);
}

/**
	Check an endpoint on a device for a blackberry
	I need 2 bulk endpoints, one read, one write.
	There seems to be more than 2 on the devices, not sure
	what the others are for.
*/
static int
scan_endpoint(bb_usb_t *bb, struct usb_endpoint_descriptor *endpoint)
{
int endpoint_pair_found = 0;

    debug(9, "%s:%s(%d)\n", __FILE__, __FUNCTION__, __LINE__);
    if ((endpoint->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK)
    {
	if (endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
	{
	    if (bb->read == -1)
	    {
	    	//bb->read = endpoint->bEndpointAddress & USB_ENDPOINT_ADDRESS_MASK;
	    	bb->read = endpoint->bEndpointAddress /*& USB_ENDPOINT_ADDRESS_MASK*/;
	    	if (usb_clear_halt(bb->dev, endpoint->bEndpointAddress) != 0)
	    	{
		    debug(0, "%s:%s(%d) - clear_halt %i %s\n",
			__FILE__, __FUNCTION__, __LINE__,
			bb->read,
			strerror(errno));
	    	}
	    }
	    else
	    {
	    	bb->write = -1;
	    }
	}
	else
	{
	    if (bb->write == -1)
	    {
	    	//bb->write = endpoint->bEndpointAddress & USB_ENDPOINT_ADDRESS_MASK;
	    	bb->write = endpoint->bEndpointAddress /*& USB_ENDPOINT_ADDRESS_MASK*/;
	    	if (usb_clear_halt(bb->dev, endpoint->bEndpointAddress) != 0)
	    	{
		    debug(0, "%s:%s(%d) - clear_halt %i %s\n",
			__FILE__, __FUNCTION__, __LINE__,
			bb->read,
			strerror(errno));
	    	}
	    }
	    else
	    {
	    	bb->read = -1;
	    }
	}
	if (bb->read >= 0 && bb->write >= 0)
	{
	    if (check_endpoint_pair(bb))
	    {
		endpoint_pair_found++;
	    }
	    else
	    {
		bb->read = -1;
		bb->write = -1;
	    }
	    debug(8, "%s:%s(%d) %i endpoint pair%s found\n",
		__FILE__, __FUNCTION__, __LINE__,
		endpoint_pair_found,
		endpoint_pair_found == 1 ? "" : "s");
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - endpoint %i is not bulk\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    endpoint->bEndpointAddress & USB_ENDPOINT_ADDRESS_MASK);
	bb->read = -1;
	bb->write = -1;
    }
    return(endpoint_pair_found);
}

/**
	Claim the interface and look for a set of endpoints that
	I can use.
*/
static int
scan_setting(usb_dev_handle *dev_h, struct usb_interface_descriptor *altsetting)
{
int endpoint_found = 0;

    debug(4, "%s:%s(%d) - bInterfaceClass = 0x%02x bInterfaceSubClass = 0x%02x bInterfaceProtocol = 0x%02x\n",
	__FILE__, __FUNCTION__, __LINE__,
	altsetting->bInterfaceClass,
	altsetting->bInterfaceSubClass,
	altsetting->bInterfaceProtocol);
    if (altsetting->bInterfaceClass == 0xff)
    {
	if (0 == usb_claim_interface(dev_h, altsetting->bInterfaceNumber))
	{
	int i;
	bb_usb_t *bb;

	    bb = malloc(sizeof(bb_usb_t));
	    bb->dev = dev_h;
	    bb->interface = altsetting->bInterfaceNumber;
	    bb->read = -1;
	    bb->write = -1;
	    bb->blackberry = NULL;
	    bb->desktop_timer = NULL;
	    bb->disconnected = 1;
	    bb->password = NULL;
	    bb->modem = NULL;
#if 1
	    /* It seems to be the last pair of enpoints that are the correct ones.
	       So start from the end of the list;

	       rws 4 Jun 2007
	       After an OS upgrade the SerData stuff stopped working. It seems
	       that probing the endpoints backwards was messing that up
	       somehow.
	     */
	    for (i = 0; i < altsetting->bNumEndpoints; i++)
#else
	    for (i = altsetting->bNumEndpoints - 1; i >= 0; i--)
#endif
	    {
		if (scan_endpoint(bb, &altsetting->endpoint[i]))
		{
		    endpoint_found++;
		    break;
		}
	    }
	    if (!endpoint_found)
	    {
		usb_release_interface(dev_h, altsetting->bInterfaceNumber);
		free(bb);
	    }
	    else
	    {
	    	if (i + 1 < altsetting->bNumEndpoints)
	    	{
#if 0
		int num_read;
		char data[255];
		int local_errno;
#endif

		    /* There are enough endpoints that we may be able
		       to talk to the ip modem through a pair of these.
		     */
		    debug(4, "%s:%s(%d) i = %i bNumEndpoints = %i\n",
			__FILE__, __FUNCTION__, __LINE__,
			i,
			altsetting->bNumEndpoints);
#if 1
#if 0
		    num_read = usb_control_msg(bb->dev, 
		      /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			   /* bRequest */ 0xa5,
			     /* wValue */ 0,
			     /* wIndex */ 1,
			       /* data */ data,
			    /* wLength */ sizeof(data),
			    /* timeout */ 2000);
		    local_errno = errno;
		    if (DebugLevel > 3)
		    {
			dump_hex("==", data, num_read);
		    }
		    if (num_read > 1)
		    {
		    	if (data[0] == 0x02)
		    	{
#endif
			    bb->modem = NewIpModem(bb->blackberry, bb->dev, altsetting->endpoint[i + 1].bEndpointAddress, altsetting->endpoint[i + 2].bEndpointAddress);
#if 0
		    	}
		    	else
		    	{
			    debug(4, "%s:%s(%d)\n",
				__FILE__, __FUNCTION__, __LINE__);
		    	}
		    }
		    else
		    {
			debug(0, "%s:%s(%d) %i \"%s\" \"%s\"/n",
			    __FILE__, __FUNCTION__, __LINE__,
			    num_read,
			    num_read < 0 ? strerror(local_errno) : "",
			    num_read < 0 ? usb_strerror() : "");
		    }
#endif
#endif
		}
	    }
	    debug(8, "%s:%s(%d) %i endpoint%s found\n",
		__FILE__, __FUNCTION__, __LINE__,
		endpoint_found,
		endpoint_found == 1 ? "" : "s");
	}
	else
	{
	    debug(0, "%s:%s(%d) - claim interface failure. %s.\n",
		__FILE__, __FUNCTION__, __LINE__,
		strerror(errno));
	}
    }
    else
    {
	debug(0, "%s:%s(%d) - bInterfaceClass = 0x%02x, Looking for 0xff.\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    altsetting->bInterfaceClass);
    }
    return(endpoint_found);
}

/**
	Check the interface for a setting that is useable.
*/
static int
scan_interface(usb_dev_handle *dev_h, struct usb_interface *interface)
{
int i;
int setting_found = 0;

    debug(9, "%s:%s(%d)", __FILE__, __FUNCTION__, __LINE__);
    for (i = 0; i < interface->num_altsetting; i++)
    {
    	if (scan_setting(dev_h, &interface->altsetting[i]))
    	{
	    setting_found++;
	    break;
    	}
    }
    debug(8, "%s:%s(%d) %i setting%s found\n",
	__FILE__, __FUNCTION__, __LINE__,
	setting_found,
	setting_found == 1 ? "" : "s");
    return(setting_found);
}

/**
	For each interface in a configuration, check for an
	interface I can use.
*/
static int
scan_config(usb_dev_handle *dev_h, struct usb_config_descriptor *config)
{
int i;
int interface_found = 0;

    debug(4, "%s:%s(%d) - bConfiguration %i MaxPower = %imA\n",
    	__FILE__, __FUNCTION__, __LINE__,
	config->bConfigurationValue,
	config->MaxPower * 2);
    /*
    if (0 == usb_set_configuration(dev_h, config->bConfigurationValue))
    */
    {
	for (i = 0; i < config->bNumInterfaces; i++)
	{
	    if (scan_interface(dev_h, &config->interface[i]))
	    {
		interface_found++;
		break;
	    }
	}
	debug(8, "%s:%s(%d) %i interface%s found\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    interface_found,
	    interface_found == 1 ? "" : "s");
    }
    /*
    else
    {
	debug(0, "%s:%s(%d) - set config failure. %s.\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    strerror(errno));
    }
    */
    return(interface_found);
}

/**
	For each device that is a BlackBerry, open it and try to
	find a configuration that I know how to deal with.
*/
static int
scan_device(struct usb_device *dev)
{
int device_found = 0;

    debug(9, "%s:%s(%d)\n", __FILE__, __FUNCTION__, __LINE__);
    if (dev->descriptor.idVendor == 0x0fca &&
        (dev->descriptor.idProduct == 0x0001 ||
         dev->descriptor.idProduct == 0x0006 ||
         dev->descriptor.idProduct == 0x8004 ||
         dev->descriptor.idProduct == 0x0004))
    {
    int i;
    usb_dev_handle *dev_h;

	dev_h = usb_open(dev);
	if (dev_h)
	{
	    debug(0, "%s:%s(%d) - bcdDevice %04x, %i configurations bcdDevice 0x%04x\n",
	    	__FILE__, __FUNCTION__, __LINE__,
	    	dev->descriptor.bcdDevice,
	    	dev->descriptor.bNumConfigurations,
	    	dev->descriptor.bcdDevice);
	    for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
	    {
#if 1
		/* rws 30 Dec 2006
		   Thank you Barry http://sourceforge.net/projects/barry
		 */
		if (dev->descriptor.idProduct == 0x0006 || (dev->descriptor.bcdDevice >= 0x0100 && dev->config[i].MaxPower < 250))
		{
		int ret;

		    if (dev->descriptor.idProduct == 0x0006)
		    {
			debug(0, "%s:%s(%d) - Attempting magic to change to dual mode\n",
			    __FILE__, __FUNCTION__, __LINE__);
			ret = usb_control_msg(dev_h, 
			  /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       /* bRequest */ 0xa9,
				 /* wValue */ 1,
				 /* wIndex */ 1,
				   /* data */ NULL,
				/* wLength */ 0,
				/* timeout */ 10000);
		    }
		    if (dev->descriptor.bcdDevice >= 0x0100 && dev->config[i].MaxPower < 250)
		    {
			debug(0, "%s:%s(%d) - Attempting magic to increase power to %s/%s/%s, MaxPower %imA, config %i\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    "/proc/bus/usb",
			    dev->bus->dirname,
			    dev->filename,
			    dev->config[i].MaxPower * 2,
			    dev->config[i].bConfigurationValue);
			ret = usb_control_msg(dev_h, 
			  /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       /* bRequest */ 0xa2,
				 /* wValue */ 0,
				 /* wIndex */ 1,
				   /* data */ NULL,
				/* wLength */ 0,
				/* timeout */ 10000);
		    }
		    /* rws 30 Dec 2006
		       Unfortunately this resets the device, so we have
		       to just bail and pick up the new device on the next
		       scan of the bus :(
		     */
		    ret = usb_set_configuration(dev_h, dev->config[i].bConfigurationValue);
		    if (ret < 0)
		    {
			debug(0, "%s:%s(%d) - %s\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    usb_strerror());
		    }
		    break;
		}
		else
#endif
		{
		    if (scan_config(dev_h, &dev->config[i]))
		    {
			device_found++;
			break;
		    }
		}
	    }
	    if (!device_found)
	    {
		usb_close(dev_h);
	    }
	    debug(8, "%s:%s(%d) %i device%s found\n",
		__FILE__, __FUNCTION__, __LINE__,
		device_found,
		device_found == 1 ? "" : "s");
	}
	else
	{
	    debug(0, "%s:%s(%d) - open failure. %s.\n",
		__FILE__, __FUNCTION__, __LINE__,
		strerror(errno));
	}
    }
    return(device_found);
}

/**
	check a buss
*/
static int
scan_bus(struct usb_bus *bus)
{
struct usb_device *dev;
int devices_found = 0;

    debug(9, "%s:%s(%d)\n", __FILE__, __FUNCTION__, __LINE__);
    for (dev = bus->devices; dev; dev = dev->next)
    {
    	if (scan_device(dev))
    	{
	    devices_found++;
    	}
    }
    debug(8, "%s:%s(%d) %i device%s found\n",
	__FILE__, __FUNCTION__, __LINE__,
	devices_found,
	devices_found == 1 ? "" : "s");
    return(devices_found);
}

/**
	Check all busses for a new device
*/
static int
scan_busses(struct usb_bus *busses)
{
struct usb_bus *bus;
int devices_found = 0;

    debug(9, "%s:%s(%d)\n", __FILE__, __FUNCTION__, __LINE__);
    for (bus = busses; bus; bus = bus->next)
    {
    	if (scan_bus(bus))
    	{
	    devices_found++;
    	}
    }
    debug(8, "%s:%s(%d) %i device%s found\n",
	__FILE__, __FUNCTION__, __LINE__,
	devices_found,
	devices_found == 1 ? "" : "s");
    return(devices_found);
}

/**
	Thread that checks the USB for new or removed devices
	every 5 seconds. When the number of usb devices changes
	it call scan_busses
*/
static void *
monitor_usb(void *arg)
{
struct usb_control_rec *control = (struct usb_control_rec *)arg;

    debug(8, "%s:%s(%d) - done = %s\n",
	__FILE__, __FUNCTION__, __LINE__,
	control->done ? "True" : "False");
    do
    {
	if (!control->done)
	{
	    control->delay = 5000000;
	    usleep(control->delay);
	}
	debug(9, "%s:%s(%d) - Scanning USB busses ...\n",
	    __FILE__, __FUNCTION__, __LINE__);
	control->num_devices = usb_find_devices();
	if (control->num_devices > 0)
	{
	int new_devices;

	    debug(7, "%s:%s(%d) - Number of devices has changed by %i\n",
	    	__FILE__, __FUNCTION__, __LINE__,
	    	control->num_devices);
	    usleep(control->delay / 2);
	    control->num_busses = usb_find_busses();
	    if (control->num_busses > 0)
	    {
		debug(0, "%s:%s(%d) - Number of busses has changed by %i\n",
		    __FILE__, __FUNCTION__, __LINE__,
		    control->num_devices);
		control->busses = usb_get_busses();
	    }
	    new_devices = scan_busses(control->busses);
	    debug(8, "%s:%s(%d) - Number of new devices %i\n",
		__FILE__, __FUNCTION__, __LINE__,
		new_devices);
	}
    }
    while (!control->done);
    return(NULL);
}

#if 0
static void
fake_reply(BlackBerry_t *blackberry)
{
unsigned char reply[] = {0x41, 0x00, 0x00};

    if (blackberry->last_packet)
    {
	blackberry->last_packet = BufferFree(blackberry->last_packet);
    }
    desktop_packet(blackberry->desktop, reply, sizeof(reply));
}
#endif

static void
alarm_handler(int sig, siginfo_t *info, void *call_data)
{
//ListItem blackberry_list = usb_control.blackberry_list.head;

    debug(9, "%s:%s(%d) - signal = %i old sa_sigaction = %p\n",
    	__FILE__, __FUNCTION__, __LINE__,
    	sig,
    	usb_control.old_alarm_action.sa_sigaction);

#if 0
    while (blackberry_list)
    {
    BlackBerry_t *blackberry = blackberry_list->contents;

	debug(0, "%s:%s(%d) - %08x %sconnected tx_queue = %i last_packet = %p\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    blackberry->pin,
	    blackberry->connected ? "" : "not ",
	    ListLength(&blackberry->tx_queue),
	    blackberry->last_packet);

//    	if (ListLength(&blackberry->tx_queue) == 0 && blackberry->last_packet)
    	{
	    fake_reply(blackberry);
    	}
    	blackberry_list = blackberry_list->next;
    }
#endif
    if (usb_control.old_alarm_action.sa_sigaction)
    {
	(*usb_control.old_alarm_action.sa_sigaction)(sig, info, call_data);
    }
    else if (usb_control.old_alarm_action.sa_handler)
    {
	(*usb_control.old_alarm_action.sa_handler)(sig);
    }
}

/** \addtogroup Drivers Drivers
*/
/** \ingroup Drivers
    \addtogroup UsbDriver
    \brief Controls devices connected via the USB

and what about here??
@{
*/
/**
	Creates a thread to monitor all of the USB interfaces for devices.
	When a BlackBerry is discovered a BlackBerry_t is populated and
	the UI callback is called.
*/
BlackBerry_t *
usb_bb_open(BlackBerryCallbackProc new_device_callback,
            void *client_data)
{
    debug(9, "%s:%s(%d)\n", __FILE__, __FUNCTION__, __LINE__);
    ListInitialize(&usb_control.blackberry_list);
    usb_control.new_device_callback = new_device_callback;
    usb_control.client_data = client_data;
    usb_init();
    usb_control.num_busses = usb_find_busses();
    if (usb_control.num_busses > 0)
    {
    int ret;

	usb_control.busses = usb_get_busses();
	usb_control.done = 1;
	usb_control.delay = 0;
	monitor_usb(&usb_control);
#ifdef HAVE_PTHREAD_H
	usb_control.done = 0;
	usb_control.delay = 5000000;
	ret = pthread_create(&usb_control.monitor_thread,
	    NULL,
	    monitor_usb,
	    (void *)&usb_control);
	if (ret == 0)
	{
	}
	else
	{
	    debug(0, "%s:%s(%d) - Couldn't create a thread %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		strerror(errno));
	}
#endif
#if 1
    	/* this sends a bogus reply to a device to try and recover
    	   devices that are not responding. I don't think it's really
    	   used now that I have the protocol figured out better.
    	 */
    	usb_control.alarm_action.sa_sigaction = alarm_handler;
    	//sigemptyset(&usb_control.alarm_action.sa_mask);
    	sigprocmask(SIG_BLOCK, NULL, &usb_control.alarm_action.sa_mask);
    	usb_control.alarm_action.sa_flags = SA_SIGINFO;
    	if (sigaction(SIGALRM, &usb_control.alarm_action, &usb_control.old_alarm_action) == 0)
    	{
	    usb_control.itimer_value.it_interval.tv_sec = 30;
	    usb_control.itimer_value.it_interval.tv_usec = 0;
	    usb_control.itimer_value.it_value.tv_sec = 30;
	    usb_control.itimer_value.it_value.tv_usec = 0;
	    setitimer(ITIMER_REAL, &usb_control.itimer_value, NULL);
    	}
    	else
    	{
	    debug(0, "%s:%s(%d) - sigaction %s\n",
		__FILE__, __FUNCTION__, __LINE__,
		strerror(errno));
    	}
#endif
    }
    else
    {
    	debug(0, "%s:%s(%d) - No USB busses %s\n",
	    __FILE__, __FUNCTION__, __LINE__,
	    usb_control.num_busses < 0 ?  strerror(errno) : "");
    }
    return(NULL);
}
/**
@}
*/
