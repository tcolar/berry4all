'''
Data structures for Blackberry access
Thibaut Colar
'''
import bb_usb

class device:
	'''
	Data structure for a device infos
	Usually created by bb_usb.find_berry
	'''
	# the udb device
	usbdev=None
	# usb handle (once opened)
	handle=None
	# readpt empty until read_endpoints() called
	readpt=-1;
	writept=-1;
	interface=-1;
	modem_readpt=-1
	modem_writept=-1
	# Following empty until read_infos() called
	pin=None
	desc=None
	
	def claim_interface(self):
		self.handle.claimInterface(self.interface)
		
	def read_infos(self):
		''' read pin and description and store them in this data structure'''
		self.pin=bb_usb.get_pin(self)
		self.desc=bb_usb.get_description(self)

	def read_endpoints(self):
		''' read endpoints (data & modem) and store them in this data structure'''
		bb_usb.read_bb_endpoints(self)
		
	def open_handle(self):
		''' Open the usb handle '''
		self.handle=self.usbdev.open()

#Data utilities
def readlong(data,index):
	return (data[index+3]<<24)+(data[index+2]<<16)+(data[index+1]<<8)+data[index]

def readstring(data,index):
	s="";
	while(data[index]!=0):
		s+=chr(data[index])
		index+=1
	return s;

