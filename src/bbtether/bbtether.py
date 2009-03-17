#!/usr/bin/python
'''
See: http://wiki.colar.net/bbtether for more infos.

Script to use a USB BlackBerry device as a modem (Tethering), and enable charging.
On My Pearl (Edge) in Seattle, I get speeds of 8K/s-29K/s, avering about 14K/s.

This script requires python, pppd, libusb and the python usb module installed:
	You probably already have python, pppd and libusb installed
	Ex: sudo apt-get install python libusb pppd python-pyusb
		or yum install python libusb pppd pyusb

-----------------------------------		
Thibaut Colar - 2009+
tcolar AT colar DOT net
http://wiki.colar.net/bbtether
Released Under GPL2, COMES WITH ABSOLUTELY NO WARRANTIES OF ANY KIND, USE AT YOUR OWN RISK.
If you make fixes or find issues please EMAIL: tcolar AT colar Dot NET
'''
import sys

import bb_tether

# MAIN
(options, args) = bb_tether.parse_cmd(sys.argv[1:])
bb_tether.BBTether(options, args)

'''
Protocol References: (Used to figure out BBerry protocol)
	http://www.off.net/cassis/protocol-description.html
	http://xmblackberry.cvs.sourceforge.net/viewvc/xmblackberry/XmBlackBerry/	
	http://barry.cvs.sourceforge.net/viewvc/barry/barry/
	http://libusb.sourceforge.net/doc/
	http://bazaar.launchpad.net/~pygarmin-dev/pygarmin/trunk/annotate/91?file_id=garmin.py-20070323161514-arelz0uc976re3e4-1
	http://www.fibble.org/archives/000508.html
	http://www.wxpython.org/download.php

If usb_claim_interface() returns -EBUSY, this means there's already
another (possibly kernel) driver bound to that interface. On Linux you
can try usb_get_driver_np() to figure out which driver it is, and
usb_detach_kernel_driver_np() to detach it from this interface (and
retry claiming). You could also try to disable/remove the offending
driver manually.
http://osdir.com/ml/lib.libusb.devel.general/2004-12/msg00013.html
http://osdir.com/ml/lib.libusb.devel.general/2004-12/msg00014.html

You need to do a usb_detach_kernel_driver_np( udev, interface); before the usb_claim_interface. The reason, I have read, is that the kernel has claimed the interface, and it has to be detached before the process claims it.


'''
