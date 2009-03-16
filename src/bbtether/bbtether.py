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
import time

import bb_modem
import bb_osx
import bb_usb
import bb_util
from optparse import OptionGroup
from optparse import OptionParser
import os

VERSION = "0.2l"

''' Main Class '''
class BBTether:

	def parse_cmd(self):
		usage = usage = "usage: %prog [options] [pppscript]\n	If [pppscript] is there (ppp script in conf/ folder, ex: tmobile) then will start modem and connect using that ppp script. Otherwise just opens the modem and you will have to start pppd manually."
		parser = OptionParser(usage)
		parser.add_option("-P", "--password", dest="password", help="Blackberry password (if passowrd protected) ex: -P abc123")
		parser.add_option("-l", "--list", action="store_true", dest="listonly", help="Only detect and list Device, do nothing more")
		parser.add_option("-p", "--pppd", dest="pppd", help="Path To pppd binary (default: /usr/sbin/pppd)")
		parser.add_option("-v", "--verbose", action="store_true", dest="verbose", help="Verbose: Show I/O data and other infos")
		parser.add_option("-c", "--charge", action="store_true", dest="charge", help="Put the device in Charging mode (ex: Pearl) and reset it.")
		parser.add_option("-m", "--dmode", action="store_true", dest="dmode", help="Put the device in data mode, might help on some devices.")
		group = OptionGroup(parser, "Advanced Options", "Don't use unless you know what you are doing.")
		group.add_option("-w", "--drp", dest="drp", help="Force Data read endpoint - Hex(ex -w 0x84)")
		group.add_option("-x", "--dwp", dest="dwp", help="Force Data write endpoint - Hex (ex -x 0x6)")
		group.add_option("-y", "--mrp", dest="mrp", help="Force Modem read endpoint - Hex (ex -y 0x85)")
		group.add_option("-z", "--mwp", dest="mwp", help="Force Modem write endpoint - Hex (ex -z 0x8)")
		group.add_option("-d", "--device", dest="device", help="Force to use a specific device ID (use together with -b)")
		group.add_option("-b", "--bus", dest="bus", help="Force to use a specific bus ID(use together with -d)")
		group.add_option("-i", "--int", dest="interface", help="Force to use a specific interface number (should not be needed)")
		parser.add_option_group(group)
		return parser.parse_args()

	def __init__(self):
		print "--------------------------------"
		print "BBTether ", VERSION
		print "Thibaut Colar - 2009"
		print "More infos: http://wiki.colar.net/bbtether"
		print "Use '-h' flag for more informations : 'python bbtether.py -h'."
		print "--------------------------------\n"

		(options, args) = self.parse_cmd()

		pppConfig = None
		if len(args) > 0:
			pppConfig = args[0]

		if(options.verbose):
			bb_util.verbose = True

		# Need to be root
		if os.getuid() != 0:
			print "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\nThis probably will only work as root!\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
			#sys.exit(0)

		bb_util.remove_berry_charge()
		bb_osx.prepare_osx()

		berry = None
		
		berry = bb_usb.find_berry(options.device, options.bus)

		if berry != None:

			# open the connection
			berry.open_handle()

			#bb_usbfs.find_kernel_driver(berry)

			# lookup endpoints
			# IMPORTANT: We need to do this BEFORE RESET, otherwise modem will be screwed
			# folowing "test" hello packet (fail on Pearl, ok on storm)
			# all right on the Bold the hello packet also causes problem, but reset won't fix it :-(
			if not (options.drp and options.dwp and options.mrp and options.mwp):
				berry.read_endpoints(options.interface)

			if options.listonly:
				print "Listing only requested, stopping here."
				bb_osx.terminate_osx()
				os._exit(0)

			bb_util.remove_berry_charge()

			# set power & reset (only if '-c' requested)
			# Only needed with BB os < 4.5 ?
			if options.charge:
				bb_usb.set_bb_power(berry)
				print ("Waiting few seconds, for mode to change")
				time.sleep(1.5)

			# set to datamode (ony if requested)
			if options.dmode:
				bb_usb.set_data_mode(berry)

			# overwrite found endpoints with user endpoints if specified
			if options.drp:
				berry.readpt = int(options.drp, 16)
			if options.dwp:
				berry.writept = int(options.dwp, 16)
			if options.mrp:
				berry.modem_readpt = int(options.mrp, 16)
			if options.mwp:
				berry.modem_writept = int(options.mwp, 16)
			if options.interface:
				berry.interface = int(options.interface)

			if berry.readpt == -1:
				print "\nNo good Data Endpoint pair, bailing out !";
			else:
				print "\nUsing Data Endpoint Pair:", hex(berry.readpt), "/", hex(berry.writept);
				print "Using Modem pair: ", hex(berry.modem_readpt), "/", hex(berry.modem_writept), "\n"
				
				print "Claiming interface ",berry.interface
				berry.claim_interface()

				berry.read_infos()
				print "Pin: ", hex(berry.pin)
				print "Description: ", berry.desc

				# Modem use does not require to be in desktop mode, so don't do it.
				modem = bb_modem.BBModem(berry)
				
				if options.password:
					modem.set_password(options.password)
				
				pppdCommand = "/usr/sbin/pppd";
				if options.pppd:
					pppdCommand = options.pppd

				# Windows does this, however it does not seem to be required (seem to crash usb at times too)
				#bb_usb.usb_write(berry, berry.writept, bb_modem.MODEM_BYPASS_PCKT)
				#bb_usb.usb_read(berry, berry.readpt)
				#bb_usb.usb_write(berry, berry.writept, [0,0,0x8,0,0xa,0x6,0,0xa])
				#bb_usb.usb_read(berry, berry.readpt)
				#bb_usb.usb_write(berry, berry.writept, [0x6,0,0xa,0,0x40,0,0,0x1,0,0])
				#bb_usb.usb_read(berry, berry.readpt)
				#bb_usb.usb_write(berry, berry.writept, [0x6,0,0x16,0,0x40,0x1,0x1,0x2,0,0,0,0xa,0x49,0,0,0,0,0x49,0,0,0,0x1])
				#bb_usb.usb_read(berry, berry.readpt)
				
				# This will run forever (until ^C)
				try:
					modem.start(pppConfig, pppdCommand)
				except KeyboardInterrupt:
					# sometimes the KInterrupt will propagate here(if ^C before modem read thread started)
					# we don't want to crash and hang.
					pass

				print "Releasing interface"
				berry.release_interface()
				bb_osx.terminate_osx()
				print "bbtether completed."
				os._exit(0)
		else:
			print "\nNo RIM device found"


# MAIN
BBTether()

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
