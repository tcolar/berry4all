#!/usr/bin/python

'''
Script to use a USB BlackBerry device as a modem (Tethering), and enable charging.

This script requires python, libusb and the python usb module:
	Ex: sudo apt-get install python libusb python-pyusb
		
Thibaut Colar - 2009
tcolar AT colar DOT net
http://wiki.colar.net/bbtether

Protocol References:
	http://www.off.net/cassis/protocol-description.html
	http://xmblackberry.cvs.sourceforge.net/viewvc/xmblackberry/XmBlackBerry/	
	http://barry.cvs.sourceforge.net/viewvc/barry/barry/
'''
 
import usb
import pty
import os
import time
import threading

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
COMMAND_HELLO=[0x00, 0x00, 0x10, 0x00, 0x01, 0xff, 0x00, 0x00,0xa8, 0x18, 0xda, 0x8d, 0x6c, 0x02, 0x00, 0x00]
#MODE_DESKTOP_PACKET=[0x00, 0x00, 0x18, 0x00, 0x52, 0x49, 0x4d, 0x20, 0x44, 0x65, 0x73, 0x6b, 0x74, 0x6f, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00]  # "RIM Desktop"
#OPEN_SOCKET = [0x00, 0x00, 0x07, 0x00, 0x0a, 0x08, 0x00]
MODEM_RESET = [0x00, 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12]
MODEM_ACK = [0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12]


''' 'Static' Methods '''
def tuple_to_hex(tuple, msg):
	text=""
	hexa=""
	cpt=0
	for t in tuple:
		print t
		try:
			hexa+=hex(t)+" "
		except TypeError:
			hexa+=t+" "
		try:
			text+=chr(t)+" "
		except TypeError:
			text+=t+" "
		cpt+=1
		if(cpt%16==0 or cpt==len(tuple)):
			print msg,"[",hexa,"] [",text,"]"

def modem_write(handle,pt,data,timeout=TIMEOUT):
	tuple_to_hex(data,"\tModem -> ")
	handle.bulkWrite(pt, data, TIMEOUT)

def modem_read(handle,pt,size=BUF_SIZE,timeout=TIMEOUT):
	bytes=handle.bulkRead(pt, size, TIMEOUT)
	tuple_to_hex(bytes,"\tModem <- ")
	return bytes 

''' Main Class '''
class BBTether:

	def find_berry(self, verbose=True):
		if(verbose):
			print "Looking for USB devices:"
		berry=None
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
		print "Switching Device to data mode"
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
		handle.bulkWrite(writept, COMMAND_PIN, TIMEOUT)
		data=handle.bulkRead(readpt, BUF_SIZE , TIMEOUT);
		if data[4] == 0x6 and data[10] == 4:
			pin=self.readlong(data,16);
		return pin

	def get_description(self, handle, writept, readpt):
		desc=""
		handle.bulkWrite(writept, COMMAND_DESC, TIMEOUT)
		data=handle.bulkRead(readpt, BUF_SIZE , TIMEOUT);
		if data[4] == 0x6 and data[10] == 2:
			desc=self.readstring(data,28)
		return desc

	#def enable_desktop_mode(self, handle, writept, readpt):
		#handle.bulkWrite(writept, MODE_DESKTOP_PACKET, TIMEOUT)
		#data=handle.bulkRead(readpt, BUF_SIZE , TIMEOUT);
		#print data
		#handle.bulkWrite(writept, OPEN_SOCKET, TIMEOUT)
		#data=handle.bulkRead(readpt, BUF_SIZE , TIMEOUT);
		#print data
		
	def __init__(self):
		print "--------------------------------"
		print "BBTether ",VERSION
		print "Thibaut Colar - 2009"
		print "http://wiki.colar.net/bbtether"
		print "--------------------------------\n"
		
		berry=None
		handle=None
		readpt=-1;
		writept=-1;
		interface=-1;

		berry=self.find_berry()

		if(berry != None):
			# open the connection
			handle=berry.open()
			# set power & reset
			self.set_bb_power(handle)
			# reopen
			print ("Waiting few seconds, for mode to change")
			time.sleep(3)
			berry=self.find_berry(False)
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
						read=att.endpoints[i].address
						write=att.endpoints[i+1].address
						i+=2
						print "		EndPoint Pair:",hex(read),"/",hex(write)
						try:
							handle.clearHalt(read)
							handle.clearHalt(write)
						
							handle.bulkWrite(write,COMMAND_HELLO, TIMEOUT)
							try:
								answer=handle.bulkRead(read, BUF_SIZE , TIMEOUT)
								good=True
								interface=int[0].interfaceNumber
								readpt=read
								writept=write
							except usb.USBError:
								print "			Pair not usable (Read failed)"
						except usb.USBError:
							print "			Pair not usable (Write failed)"
						if good:
							print "			Found Data pair:",hex(read),"/",hex(write);

			if readpt==-1:
				print "No good endpoint pair, bailing out !";
			else:
				print "Using endpoint pair:",hex(readpt),"/",hex(writept);
				
				handle.claimInterface(interface);
				
				#pin=self.get_pin(handle,writept,readpt)
				#print "Pin: ",hex(pin)
				
				#desc=self.get_description(handle,writept,readpt)
				#print "Description: ",desc

				# Modem use does not require to be in desktop mode, so don't do it.
				#self.enable_desktop_mode(handle,writept,readpt)

				handle.clearHalt(readpt)
				handle.clearHalt(writept)

				modem=BBModem(handle, readpt, writept)
				modem.start()
			
		else:
			print "\nNo RIM device found"


class BBModem:		
	
	handle=None
	readpt=None
	writept=None
	
	def start(self):
		#open modem PTY
		(master,slave)=pty.openpty()
		#print os.ttyname(master)
		print "\nModem pty: ",os.ttyname(slave)
		
		print "Resetting Modem"
		modem_write(self.handle,self.writept, MODEM_ACK)
		#self.handle.bulkWrite(self.writept, MODEM_ACK, TIMEOUT)
		
		#os._exit(0)
		
		readThread=BBModemReadThread(self.handle,self.writept,master)
		writeThread=BBModemWriteThread(self.handle,self.readpt,master)
		readThread.start()	
		writeThread.start()	
		
		time.sleep(60)
		
		#close PTY
		os.close(master)
		os.close(slave)
		
	def __init__(self, hnd, read, write):
		self.handle=hnd
		self.readpt=read
		self.writept=write

'''Read from PTY and write to USB (to modem)'''		
class BBModemReadThread( threading.Thread ):
	input=None
	handle=None
	writept=None
	
	def __init__(self, hnd, write, inp):
		threading.Thread.__init__(self)
		self.input=inp
		self.handle=hnd
		self.writept=write

	def run (self):
		print "Starting Modem read thread"
		while(True):
			bytes=os.read(self.input, 2000)
			if(len(bytes)>0):
				#print "read ",len(bytes)," bytes ",bytes
				modem_write(self.handle,self.writept, bytes)

'''read from USB and write to PTY (from modem)'''		
class BBModemWriteThread( threading.Thread ):
	output=None
	handle=None
	readpt=None
	
	def __init__(self, hnd, read, out):
		threading.Thread.__init__(self)
		self.output=out
		self.handle=hnd
		self.readpt=read
				
	def run (self):
		print "Starting Modem write thread"
		while(True):
			try:
				bytes=modem_read(self.handle, self.readpt)
				if(len(bytes)>0):
					print "write ",len(bytes)," bytes ",bytes
					bytes=os.write(self.output)
			#except usb.USBError:
				#print "usb error"
			#	toto=2


# MAIN
BBTether()

