#!/usr/bin/python

'''
TODO:
	- grps fix
	- start pppd automatically
	sudo pppd /dev/pts/11 file conf/tmobile debug nodetach
'''

'''
Script to use a USB BlackBerry device as a modem (Tethering), and enable charging.

This script requires python, pppd, libusb and the python usb module:
	You probably already have python, pppd and libusb installed
	Ex: sudo apt-get install python libusb pppd python-pyusb
		
Thibaut Colar - 2009
tcolar AT colar DOT net
http://wiki.colar.net/bbtether

Protocol References:
	http://www.off.net/cassis/protocol-description.html
	http://xmblackberry.cvs.sourceforge.net/viewvc/xmblackberry/XmBlackBerry/	
	http://barry.cvs.sourceforge.net/viewvc/barry/barry/
	http://libusb.sourceforge.net/doc/
	http://bazaar.launchpad.net/~pygarmin-dev/pygarmin/trunk/annotate/91?file_id=garmin.py-20070323161514-arelz0uc976re3e4-1
'''
 
import usb
import pty
import os
import time
import threading
import array
import string
from optparse import OptionParser, OptionGroup

VERSION="0.1"

VENDOR_RIM=0x0fca
PRODUCT_DATA=0x0001   #(older bberry)
PRODUCT_NEW_DUAL=0x0004   #(mass storage & data)
PRODUCT_NEW_8120=0x8004   #(Pearl 8120)
PRODUCT_NEW_MASS_ONLY=0x0006   #(mass storage only)   --- probably not good

BERRY_CONFIG=1

TIMEOUT=500
BUF_SIZE=2000

COMMAND_PIN = [0x00,0x00,0x0c,0x00,0x05,0xff,0x00,0x00,0x00,0x00,0x04,0x00] 
COMMAND_DESC= [0x00,0x00,0x0c,0x00,0x05,0xff,0x00,0x00,0x00,0x00,0x02,0x00]
COMMAND_HELLO = [0x00, 0x00, 0x10, 0x00, 0x01, 0xff, 0x00, 0x00,0xa8, 0x18, 0xda, 0x8d, 0x6c, 0x02, 0x00, 0x00]
MODEM_START = [0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12]
MODEM_ACK = [0x4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x1c, 0x0, 0x0, 0x0, 0x78, 0x56, 0x34, 0x12 ]
MODEM_ACK2 = [0x0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  0x18, 0x0, 0x0, 0x0, 0x78, 0x56, 0x34, 0x12 ]
#MODE_DESKTOP_PACKET=[0x00, 0x00, 0x18, 0x00, 0x52, 0x49, 0x4d, 0x20, 0x44, 0x65, 0x73, 0x6b, 0x74, 0x6f, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00]  # "RIM Desktop"
	
''' Main Class '''
class BBTether:
	verbose=False;
	ppp=None

	def find_berry(self, userdev, userbus, verbose=True):
		if verbose:
			print "Looking for USB devices:"
		berry=None
		if userdev and userbus:
			if verbose :
				print "Will use user provided bus/device: ",options.bus,"/",options.device
			for bus in usb.busses():
				if string.atoi(bus.dirname) == string.atoi(userbus):
					for dev in bus.devices:
						if string.atoi(dev.filename) == string.atoi(userdev):
							berry=dev				
		else:
			for bus in usb.busses():
				for dev in bus.devices:
					if(verbose):
						print "	Bus %s Device %s: ID %04x:%04x" % (bus.dirname,dev.filename,dev.idVendor,dev.idProduct)
					if(dev.idVendor==VENDOR_RIM):
						berry=dev
		return berry


	def set_bb_power(self,handle):
		print "\nIncreasing USB power - for charging"
		buffer= [0,0]
		handle.controlMsg(0xc0, 0xa5, buffer, 0 , 1)
		buffer = []
		handle.controlMsg(0x40, 0xA2, buffer, 0 , 1)
		handle.reset()
		print "Switching Device to data only mode"
		buffer= [0,0]
		handle.controlMsg(0xc0, 0xa9, buffer, 0 , 1)

	def readlong(self,data,index):
		return (data[index+3]<<24)+(data[index+2]<<16)+(data[index+1]<<8)+data[index]

	def readstring(self,data,index):
		s="";
		while(data[index]!=0):
			s+=chr(data[index])
			index+=1
		return s;

	def get_pin(self, handle, writept, readpt):
		pin=0x0;
		self.usb_write(handle,writept, COMMAND_PIN)
		data=self.usb_read(handle,readpt);
		if data[4] == 0x6 and data[10] == 4:
			pin=self.readlong(data,16);
		return pin

	def get_description(self, handle, writept, readpt):
		desc=""
		self.usb_write(handle,writept, COMMAND_DESC)
		data=self.usb_read(handle,readpt);
		if data[4] == 0x6 and data[10] == 2:
			desc=self.readstring(data,28)
		return desc

	def parse_cmd(self):
		usage = usage = "usage: %prog [options] [pppscript]\n	If [pppscript] is there (ppp script in conf/ folder, ex: tmobile) then will start modem and connect using that ppp script. Otherwise just opens the modem and you will have to start ppd manually."
		parser = OptionParser(usage)
		#parser.set_defaults(verbose="false",listonly="false",noppp="false",chargeonly="false")
		parser.add_option("-l", "--list", action="store_true", dest="listonly",help="Only detect and list Device, do nothing more")
		parser.add_option("-v", "--verbose", action="store_true", dest="verbose",help="Verbose: Show I/O data and other infos")
		parser.add_option("-c", "--charge", action="store_true", dest="chargeonly",help="Put the device in Charging mode and does NOT start modem.")		
		group = OptionGroup(parser, "Advanced Options","Don't use unless you know what you are doing.")
		group.add_option("-w", "--drp", dest="drp", help="Force Data read endpoint (ex -w 0x84)")
		group.add_option("-x", "--dwp", dest="dwp", help="Force Data write endpoint (ex -x 0x6)")
		group.add_option("-y", "--mrp", dest="mrp", help="Force Modem read endpoint (ex -y 0x85)")
		group.add_option("-z", "--mwp", dest="mwp", help="Force Modem write endpoint (ex -z 0x8)")
		group.add_option("-d", "--device", dest="device", help="Force to use a specific device ID (use together with -b)")
		group.add_option("-b", "--bus", dest="bus", help="Force to use a specific bus ID(use together with -d)")
		parser.add_option_group(group)
		return parser.parse_args()

	def usb_write(self,handle,pt,data,timeout=TIMEOUT,msg="\t-> "):
		# transform string from PTY into array of signed bytes
		bytes=array.array("B",data)
		if(self.verbose):	
			debug_bytes(bytes,msg)
		handle.bulkWrite(pt, bytes, TIMEOUT)

	def usb_read(self,handle,pt,size=BUF_SIZE,timeout=TIMEOUT,msg="\t-< "):
		bytes=handle.bulkRead(pt, size, TIMEOUT)
		if(self.verbose):	
			debug_bytes(bytes,msg)
		return bytes 

	def __init__(self):
		print "--------------------------------"
		print "BBTether ",VERSION
		print "Thibaut Colar - 2009"
		print "http://wiki.colar.net/bbtether"
		print "Use '-h' flag for more informations : 'python bbtether.py -h'."
		print "--------------------------------\n"
		
		(options,args)=self.parse_cmd()
		self.ppp=None
		if len(args) > 0:
			self.ppp=args[0]
		
		if(options.verbose):
			self.verbose=True
		
		berry=None
		handle=None
		readpt=-1;
		writept=-1;
		interface=-1;
		modem_readpt=-1
		modem_writept=-1

		berry=self.find_berry(options.device,options.bus)
				

		if(berry != None):
			# open the connection
			handle=berry.open()
			# set power & reset
			self.set_bb_power(handle)
			
			if options.chargeonly:
				print "Charge only requested, stopping now."
				os._exit(0)
			
			# reopen
			print ("Waiting few seconds, for mode to change")
			time.sleep(3)
			
			berry=self.find_berry(options.device,options.bus,False)
			handle=berry.open()
				
			# List device Infos for information and find USB endpair	
			config=berry.configurations[0];
			type=""
			if(berry.idProduct == PRODUCT_DATA):
				type="Data Mode"
			if(berry.idProduct == PRODUCT_NEW_DUAL):
				type="Dual Mode"
			if(berry.idProduct == PRODUCT_NEW_8120):
				type="8120"
			if(berry.idProduct == PRODUCT_NEW_MASS_ONLY):
				type="Storage Mode"
			
			print "\nFound RIM device (",type,")" 
			print "	Manufacturer:",handle.getString(berry.iManufacturer,100)
			print "	Product:",handle.getString(berry.iProduct,100)
			#print "	Serial:",handle.getString(berry.iSerialNumber,100)
			print "	Device:", berry.filename
			print "	VendorId: %04x" % berry.idVendor
			print "	ProductId: %04x" % berry.idProduct
			print "	Version:",berry.deviceVersion
			print "	Class:",berry.deviceClass," ",berry.deviceSubClass
			print "	Protocol:",berry.deviceProtocol
			print "	Max packet size:",berry.maxPacketSize
			print "	Self Powered:", config.selfPowered
			print "	Max Power:", config.maxPower
			for int in config.interfaces:
				print "	Interface:",int[0].interfaceNumber
				print "		Interface class:",int[0].interfaceClass,"/",int[0].interfaceSubClass
				print "		Interface protocol:",int[0].interfaceProtocol
				for att in int:
					i=0
					# check endpoint pairs
					while i < len(att.endpoints):
						good=False
						red=att.endpoints[i].address
						writ=att.endpoints[i+1].address
						i+=2
						print "		EndPoint Pair:",hex(red),"/",hex(writ)
						try:						
							self.usb_write(handle,writ,COMMAND_HELLO)
							try:
								self.usb_read(handle, red)
								good=True
								if readpt == -1 :
									# Use first valid data point found
									interface=int[0].interfaceNumber
									readpt=red
									writept=writ
							except usb.USBError:
								print "			Not Data Pair (Read failed)"
						except usb.USBError:
							print "			Not Data Pair (Write failed)"
						
						if good:
							print "			Found Data pair:",hex(red),"/",hex(writ);
						else:
							if readpt != -1 and modem_readpt == -1:
								# use pair after data pair as Modem pair
								modem_readpt=red
								modem_writept=writ

			if options.listonly :
				print "Listing only requested, stopping here."
				os._exit(0)

			if readpt==-1:
				print "\nNo good Data Endpoint pair, bailing out !";
			else:
				print "\nUsing Data Endpoint Pair:",hex(readpt),"/",hex(writept);				
				print "Using first pair after Data pair as Modem pair: ",hex(red),"/",hex(writ),"\n"
				
				handle.claimInterface(interface);
				
				pin=self.get_pin(handle,writept,readpt)
				print "Pin: ",hex(pin)
				
				desc=self.get_description(handle,writept,readpt)
				print "Description: ",desc

				# Modem use does not require to be in desktop mode, so don't do it.
				#self.enable_desktop_mode(handle,writept,readpt)
				modem=BBModem(self, handle, modem_readpt, modem_writept)
				
				# This will run forever (until ^C)
				modem.start(self.ppp)				
			
		else:
			print "\nNo RIM device found"


class BBModem:		
	'''BlackBerry Modem class'''
	handle=None
	readpt=None
	writept=None
	
	def __init__(self, prt, hnd, read, write):
		self.handle=hnd
		self.readpt=read
		self.writept=write
		self.parent=prt

	def write(self, handle,pt,data,timeout=TIMEOUT):
		self.parent.usb_write(handle,pt,data,timeout,"\tModem -> ")
	
	def read(self, handle,pt,size=BUF_SIZE,timeout=TIMEOUT):
		return self.parent.usb_read(handle,pt,size,timeout,"\tModem <- ")

	def start(self, ppp):
		'''Start the modem and keep going until ^C'''
		#open modem PTY
		(master,slave)=pty.openpty()
		print "\nModem pty: ",os.ttyname(slave)
				
		bbThread=BBModemThread(self.handle,self,self.readpt,self.writept,master)
		bbThread.start()
		
		print "Initializing Modem"
		self.write(self.handle,self.writept, MODEM_START)
		if(not self.parent.ppp):
			print "No ppp requested, you can now start pppd manually."
		else:
			#TODO: start ppp in thread/process
			print "Will try to start ppp now, config: ",ppp
		
		print "Modem Ready at ",os.ttyname(slave)," Use ^C to terminate"
		
		try:
			# Read from PTY and write to USB modem
			while(True):
				bytes=os.read(master, 2000)
				if(len(bytes)>0):
					bytes=fix_bb_gprs_bytes(bytes)
					self.write(self.handle,self.writept, bytes)
				
		except KeyboardInterrupt:
			print "\nShutting down on ^C"
		
		bbThread.stop()
		
		os.close(master)
		os.close(slave)
					
class BBModemThread( threading.Thread ):	
	'''Thread that reads Modem data from BlackBerry USB'''

	def __init__(self, hnd, mdm, read, write, master):
		threading.Thread.__init__(self)
		self.output=master
		self.handle=hnd
		self.writept=write
		self.readpt=read
		self.modem=mdm
	
	def stop(self):
		self.done=True

	def run (self):
		self.done=False
		print "Starting Modem thread"
		while( not self.done):
			try:
				try:
					# Read from USB modem and write to PTY
					bytes=self.modem.read(self.handle, self.readpt)
					if(len(bytes)>0):
						if(is_same_tuple(bytes,MODEM_ACK) or is_same_tuple(bytes,MODEM_ACK2)):
							# We do not write "ACK" messages from bberry (not data)
							pass
						else:
							data=array.array("B",bytes)
							os.write(self.output,data.tostring())
				except usb.USBError, error:
					if error.message != "No error":
						raise
								
			except:
				if(self.done):
					pass
				else:
					raise
	
''' 'Static' Methods '''
def debug(msg):
	print msg
	
def debug_bytes(tuple, msg):
	'''Get a tuple of bytes and print it as lines of 16 digits (hex and ascii)'''
	text=""
	hexa=""
	cpt=0
	for t in tuple:
		#print t
		hexa+=hex(t)+" "
		if(t>=32 and t<=126):
			text+=chr(t)
		else:
			text+="."
		cpt+=1
		if(cpt%16==0 or cpt==len(tuple)):
			debug(msg+"["+hexa+"] ["+text+"]")
			text=""
			hexa=""

def is_same_tuple(tuple1,tuple2):
	'''Compare 2 tuples of Byes, return True if Same(same data)'''
	if tuple1==None and tuple2==None:
		return True
	if (tuple1==None and tuple2 != None) or (tuple1!=None and tuple2==None):
		return False
	if len(tuple1) != len(tuple2):
		return False
	# !None and same length ... compare
	length=len(tuple1)
	for i in range(length):
		if tuple1[i] != tuple2[i]:
			return False	
	return True

def fix_bb_gprs_bytes(bytes):
	'''Fix data Bytes received from PPP to make them compatible with what a blackberry expects'''
	#for b in bytes:
	#	if bytes[0]==0x7E and b==0x7E:
	#		if bytes 	
	return bytes

# MAIN
BBTether()

