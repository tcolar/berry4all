#!/usr/bin/python
import time

import bb_messenging
import bb_modem
import bb_osx
import bb_usb
import bb_util
from bb_version import VERSION
from optparse import OptionGroup
from optparse import OptionParser
import os

def parse_cmd(args):
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
	return parser.parse_args(args)


''' Main Class '''
class BBTether:
	modem=None

	def __init__(self):
		bb_messenging.log("--------------------------------")
		bb_messenging.log("BBTether " + VERSION)
		bb_messenging.log("Thibaut Colar - 2009")
		bb_messenging.log("More infos: http://wiki.colar.net/bbtether")
		bb_messenging.log("Use '-h' flag for more informations : 'python bbtether.py -h'.")
		bb_messenging.log("--------------------------------\n")

	def start(self, options, args):
		pppConfig = None
		if len(args) > 0:
			pppConfig = args[0]

		if(options.verbose):
			bb_util.verbose = True

		# Need to be root (unless udev or OSX)
		if os.getuid() != 0:
			bb_messenging.log("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\nThis might will only work as root!\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n")

		bb_util.remove_berry_charge()
		bb_osx.prepare_osx()

		berry = None
		
		berry = bb_usb.find_berry(options.device, options.bus)

		if berry != None:

			# open the connection
			if berry.handle==None:
				berry.open_handle()

			#bb_usbfs.find_kernel_driver(berry)

			# lookup endpoints
			# IMPORTANT: We need to do this BEFORE RESET, otherwise modem will be screwed
			# folowing "test" hello packet (fail on Pearl, ok on storm)
			# all right on the Bold the hello packet also causes problem, but reset won't fix it :-(
			if not (options.drp and options.dwp and options.mrp and options.mwp):
				berry.read_endpoints(options.interface)

			if options.listonly:
				bb_messenging.warn(["Listing only requested, stopping here."])
				bb_osx.terminate_osx()
				os._exit(0)

			bb_util.remove_berry_charge()

			# set power & reset (only if '-c' requested)
			# Only needed with BB os < 4.5 ?
			if options.charge:
				bb_usb.set_bb_power(berry)
				bb_messenging.status("Waiting few seconds, for mode to change")
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
				bb_messenging.warn(["\nNo good Data Endpoint pair, bailing out !"])
			else:
				bb_messenging.log("\nUsing Data Endpoint Pair:"+ hex(berry.readpt)+ "/"+ hex(berry.writept))
				bb_messenging.log("Using Modem pair: "+ hex(berry.modem_readpt)+ "/"+ hex(berry.modem_writept)+ "\n")
				
				bb_messenging.log("Claiming interface "+str(berry.interface))
				berry.claim_interface()

				berry.read_infos()
				bb_messenging.log("Pin: "+ hex(berry.pin))
				bb_messenging.log("Description: "+ berry.desc)

				# Modem use does not require to be in desktop mode, so don't do it.
				self.modem = bb_modem.BBModem(berry)
				
				if options.password:
					self.modem.set_password(options.password)
				
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
					self.modem.start(pppConfig, pppdCommand)
				except KeyboardInterrupt:
					bb_messenging.log("KBD interrupt")
					# sometimes the KInterrupt will propagate here(if ^C before modem read thread started)
					# we don't want to crash and hang.
					if self.modem!=None:
						self.modem.do_shutdown()

				bb_messenging.status("Releasing interface")
				berry.release_interface()
				bb_osx.terminate_osx()
				bb_messenging.status("bbtether completed.")
				#os._exit(0)
		else:
			bb_messenging.warn(["\nNo RIM device found"])

	def shutdown(self):
		self.modem.shutdown()

	def is_running(self):
		return self.modem!=None and self.modem.running
