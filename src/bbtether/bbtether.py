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

import bb_usb
import bb_util
import bb_modem
import time
import os
from optparse import OptionParser, OptionGroup

VERSION="0.1d"

''' Main Class '''
class BBTether:

	def parse_cmd(self):
		usage = usage = "usage: %prog [options] [pppscript]\n	If [pppscript] is there (ppp script in conf/ folder, ex: tmobile) then will start modem and connect using that ppp script. Otherwise just opens the modem and you will have to start pppd manually."
		parser = OptionParser(usage)
		parser.add_option("-l", "--list", action="store_true", dest="listonly",help="Only detect and list Device, do nothing more")
		parser.add_option("-p", "--pppd", dest="pppd",help="Path To pppd binary (default: /usr/sbin/pppd)")
		parser.add_option("-v", "--verbose", action="store_true", dest="verbose",help="Verbose: Show I/O data and other infos")
		parser.add_option("-c", "--charge", action="store_true", dest="chargeonly",help="Put the device in Charging mode and does NOT start modem.")		
		group = OptionGroup(parser, "Advanced Options","Don't use unless you know what you are doing.")
		group.add_option("-w", "--drp", dest="drp", help="Force Data read endpoint - Hex(ex -w 0x84)")
		group.add_option("-x", "--dwp", dest="dwp", help="Force Data write endpoint - Hex (ex -x 0x6)")
		group.add_option("-y", "--mrp", dest="mrp", help="Force Modem read endpoint - Hex (ex -y 0x85)")
		group.add_option("-z", "--mwp", dest="mwp", help="Force Modem write endpoint - Hex (ex -z 0x8)")
		group.add_option("-d", "--device", dest="device", help="Force to use a specific device ID (use together with -b)")
		group.add_option("-b", "--bus", dest="bus", help="Force to use a specific bus ID(use together with -d)")
		parser.add_option_group(group)
		return parser.parse_args()

	def __init__(self):
		print "--------------------------------"
		print "BBTether ",VERSION
		print "Thibaut Colar - 2009"
		print "http://wiki.colar.net/bbtether"
		print "Use '-h' flag for more informations : 'python bbtether.py -h'."
		print "--------------------------------\n"
		
		(options,args)=self.parse_cmd()
		
		pppConfig=None
		if len(args) > 0:
			pppConfig=args[0]
		
		if(options.verbose):
			bb_util.verbose=True
		
		berry=None
		
		berry=bb_usb.find_berry(options.device,options.bus)				

		if berry!=None :
			
			# open the connection
			berry.open_handle()
			# set power & reset
			bb_usb.set_bb_power(berry)
			
			if options.chargeonly:
				print "Charge only requested, stopping now."
				os._exit(0)
			
			# reopen
			print ("Waiting few seconds, for mode to change")
			time.sleep(1.5)
			
			# rescan after power / reset
			berry=bb_usb.find_berry(options.device,options.bus,False)
			berry.open_handle()
			handle=berry.handle

			# lookup endpoints
			berry.read_endpoints()

			# overwrite found endpoints with user endpoints if specified
			if options.drp:
				berry.readpt=int(options.drp, 16)
			if options.dwp:
				berry.writept=int(options.dwp, 16)
			if options.mrp:
				berry.modem_readpt=int(options.mrp, 16)
			if options.mwp:
				berry.modem_writept=int(options.mwp, 16)

			if options.listonly :
				print "Listing only requested, stopping here."
				os._exit(0)

			if berry.readpt==-1:
				print "\nNo good Data Endpoint pair, bailing out !";
			else:
				print "\nUsing Data Endpoint Pair:",hex(berry.readpt),"/",hex(berry.writept);				
				print "Using first pair after Data pair as Modem pair: ",hex(berry.modem_readpt),"/",hex(berry.modem_writept),"\n"
				
				print "Claiming interface"
				berry.claim_interface()
				
				berry.read_infos()
				print "Pin: ",hex(berry.pin)
				print "Description: ",berry.desc

				# Modem use does not require to be in desktop mode, so don't do it.
				modem=bb_modem.BBModem(berry)
				
				pppdCommand="/usr/sbin/pppd";
				if options.pppd:
					pppdCommand=options.pppd
					
				# This will run forever (until ^C)
				modem.start(pppConfig,pppdCommand)				

				print "Releasing interface"
				berry.release_interface()
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
'''
