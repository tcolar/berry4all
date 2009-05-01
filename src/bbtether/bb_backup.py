#!/usr/bin/python
import time

import bb_messenging
import bb_osx
import bb_usb
import bb_util
from optparse import OptionParser
import os

def parse_cmd(args):
	usage = usage = "usage: %prog."
	parser = OptionParser(usage)
	parser.add_option("-l", "--list", action="store_true", dest="list", help="List DB Tables")
	return parser.parse_args(args)


''' Main Class '''
class BBBackup:

	def start(self, options, args):
		bb_messenging.verbose = True
		# Need to be root (unless udev or OSX)
		if os.getuid() != 0:
			bb_messenging.log("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\nThis might will only work as root!\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n")

		bb_util.remove_berry_charge()
		bb_osx.prepare_osx()
		berry = None
		berry = bb_usb.find_berry(None,None)

		if berry != None:
			# open the connection
			if berry.handle==None:
				berry.open_handle()

			berry.read_endpoints(None)

			bb_util.remove_berry_charge()

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

				bb_usb.set_mode(berry,bb_usb.COMMAND_MODE_DESKTOP)
				bb_usb.usb_read(berry,berry.readpt)
				bb_usb.usb_write(berry,berry.writept,[00,00,0x7,00,0xa,04,00])
				bb_usb.usb_read(berry,berry.readpt)
				bb_usb.usb_write(berry,berry.writept,[04,00,0xc,00,0x40,00,00,00,0x25,0x80,0x8,00])
				bb_usb.usb_read(berry,berry.readpt)
				time.sleep(2)
				bb_usb.usb_read(berry,berry.readpt)

		else:
			bb_messenging.warn(["\nNo RIM device found"])

