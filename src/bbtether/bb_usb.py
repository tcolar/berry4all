'''
USB utilities for Blackberry
Thibaut Colar
'''
import usb
import bb_data
import bb_util
import string

TIMEOUT=1000
BUF_SIZE=25000
VENDOR_RIM=0x0fca
PRODUCT_DATA=0x0001   #(serial data)
PRODUCT_NEW_DUAL=0x0004   #(mass storage & data)
PRODUCT_NEW_8120=0x8004   #(Pearl 8120)
PRODUCT_NEW_MASS_ONLY=0x0006   #(mass storage only)
BERRY_CONFIG=1
COMMAND_PIN = [0x00,0x00,0x0c,0x00,0x05,0xff,0x00,0x00,0x00,0x00,0x04,0x00] 
COMMAND_DESC= [0x00,0x00,0x0c,0x00,0x05,0xff,0x00,0x00,0x00,0x00,0x02,0x00]
COMMAND_HELLO = [0x00, 0x00, 0x10, 0x00, 0x01, 0xff, 0x00, 0x00,0xa8, 0x18, 0xda, 0x8d, 0x6c, 0x02, 0x00, 0x00]

def find_berry(userdev=None, userbus=None, verbose=True):
	'''
		Look on Bus for a RIM device
		(1 max for now)
		userdev,userbus : potential user provided device/bus to force-use
	'''
	device=None
	mybus=None;

	if verbose:
		print "Looking for USB devices:"
	berry=None
	if userdev and userbus:
		if verbose :
			print "Will use user provided bus/device: ",userbus,"/",userdev
		for bus in usb.busses():
			if string.atoi(bus.dirname) == string.atoi(userbus):
				for dev in bus.devices:
					if string.atoi(dev.filename) == string.atoi(userdev):
						berry=dev
						mybus=bus
	else:
		for bus in usb.busses():
			for dev in bus.devices:
				if(verbose):
					print "	Bus %s Device %s: ID %04x:%04x" % (bus.dirname,dev.filename,dev.idVendor,dev.idProduct)
				if(dev.idVendor==VENDOR_RIM):
					berry=dev
					mybus=bus
	
	if berry != None:
		device=bb_data.Device()
		device.usbdev=berry
		device.bus=mybus

	return device

def read_bb_endpoints(device):
	'''
	Read the device endpoints and stores them in the device data structure
	device was created from find_berry
	and device.open_handle should have been called already
	'''		
	readpt=-1
	writept=-1
	modem_readpt=-1
	modem_writept=-1
	# List device Infos for information and find USB endpair	
	handle=device.handle
	berry=device.usbdev
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
	for inter in config.interfaces:
		print "	Interface:",inter[0].interfaceNumber
		handle.claimInterface(inter[0].interfaceNumber)
		print "		Interface class:",inter[0].interfaceClass,"/",inter[0].interfaceSubClass
		print "		Interface protocol:",inter[0].interfaceProtocol
		for att in inter:
			i=0
			# check endpoint pairs
			while i < len(att.endpoints):
				good=False
				red=att.endpoints[i].address
				writ=att.endpoints[i+1].address
				i+=2
				print "		EndPoint Pair:",hex(red),"/",hex(writ)
				try:
					usb_write(device,writ,COMMAND_HELLO)
					try:
						usb_read(device,red)
						good=True
						if readpt == -1 :
							# Use first valid data point found
							device.interface=inter[0].interfaceNumber
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
		handle.releaseInterface()

	device.readpt=readpt
	device.writept=writept
	device.modem_readpt=modem_readpt
	device.modem_writept=modem_writept

def clear_halt(device, endpt):
	device.handle.clearHalt(endpt)

def set_bb_power(device):
	print "\nIncreasing USB power - for charging"
	buffer= [0,0]
	device.handle.controlMsg(0xc0, 0xa5, buffer, 0 , 1)
	buffer = []
	device.handle.controlMsg(0x40, 0xA2, buffer, 0 , 1)
	device.handle.reset()
	print "Switching Device to data only mode"
	buffer= [0,0]
	device.handle.controlMsg(0xc0, 0xa9, buffer, 0 , 1)

def get_pin(device):
	pin=0x0;
	usb_write(device, device.writept, COMMAND_PIN)
	data=usb_read(device,device.readpt);
	if data[4] == 0x6 and data[10] == 4:
		pin=bb_data.readlong(data,16);
	return pin

def get_description(device):
	desc=""
	usb_write(device, device.writept, COMMAND_DESC)
	data=usb_read(device,device.readpt);
	if data[4] == 0x6 and data[10] == 2:
		desc=bb_data.readstring(data,28)
	return desc

def usb_write(device,endpt,bytes,timeout=TIMEOUT,msg="\t-> "):
	bb_util.debug_bytes(bytes,msg)
	try:
		device.handle.bulkWrite(endpt, bytes, TIMEOUT)
	except usb.USBError, error:
		if error.message != "No error":
			print "error: ",error
			raise
			
def usb_read(device,endpt,size=BUF_SIZE,timeout=TIMEOUT,msg="\t<- "):
	bytes=device.handle.bulkRead(endpt, size, TIMEOUT)
	bb_util.debug_bytes(bytes,msg)
	return bytes 
