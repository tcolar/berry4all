'''
Modem support for Blackberry
Thibaut Colar
'''

import bb_util
import threading
import pty
import os
import bb_usb
import subprocess
import usb
import time
import array

NOTIFY_EVERY=100000
BUF_SIZE=25000
TIMEOUT=1000
PPPD_COMMAND="pppd"
MIN_PASSWD_TRIES=2
MODEM_CONNECT= [0xd, 0xa, 0x43, 0x4f, 0x4e, 0x4e, 0x45, 0x43, 0x54, 0xd, 0xa]
MODEM_START = [0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12]
MODEM_STOP = [0x01, 0x00 ,0x00, 0x00 ,0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12]
# Packets ending by this are RIM control packets (! data)
RIM_PACKET_TAIL=[0x78, 0x56, 0x34, 0x12]

class BBModem:		
	'''BlackBerry Modem class'''
	red=0
	writ=0
	lastcount=0
	parent=None
	device=None
		
	def __init__(self, dev):
		self.device=dev

	def write(self, data, timeout=TIMEOUT):
		bb_usb.usb_write(self.device,self.device.modem_writept,data,timeout,"\tModem -> ")
		self.writ+=len(data)
		if(self.red+self.writ>self.lastcount+NOTIFY_EVERY):
			print "GPRS Infos: Received Bytes:",self.red,"	Sent Bytes:",+self.writ
			self.lastcount=self.red+self.writ
			
	def try_read(self, size=BUF_SIZE,timeout=TIMEOUT):
		data=[]
		try:
			data=self.read(size,timeout)
		except usb.USBError, error:
			if error.message != "No error":
				raise
		return data
		
	def read(self, size=BUF_SIZE,timeout=TIMEOUT):
		data=bb_usb.usb_read(self.device,self.device.modem_readpt,size,timeout,"\tModem <- ")
		self.red+=len(data)
		if(self.red+self.writ>self.lastcount+NOTIFY_EVERY):
			print "GPRS Infos: Received Bytes:",self.red,"	Sent Bytes:",+self.writ
			self.lastcount=self.red+self.writ
		return data

	def init(self):
		'''Initialize the modem and start the RIM session'''
		session_key=[0, 0, 0, 0, 0, 0, 0, 0, 0]
		# clear endpoints
		bb_usb.clear_halt(self.device,self.device.modem_readpt)
		bb_usb.clear_halt(self.device,self.device.modem_writept)
		# reset modem
		self.write(MODEM_STOP)
		#might or not reply, so use try_read
		self.try_read()
		self.write(MODEM_START)
		answer=self.read()
		# check for password request (newer devices)
		if len(answer)>0 and answer[0]==0x2 and bb_util.end_with_tuple(answer,RIM_PACKET_TAIL):
			triesLeft=answer[8]
			seed=answer[4:8]
			print "Got password Request from Device (",triesLeft," tries left)"
			triesLeft=answer[8]
			if triesLeft <= MIN_PASSWD_TRIES:
				print "The device has only "+answer[8]+" password tries left, we don't want to risk it! Reboot/unplug the device to reset tries.";
				raise Exception
			#TODO
			print "Password protected Device not supported yet !"
			raise Exception		
		else:
			print "No password requested."	
		# Send session key
		# At least on my Pearl if I don't send this now, the device will reboot itself during heavy data transfer later (odd)
		session_packet=[0, 0, 0, 0, 0, 0, 0, 0, 0x3, 0, 0, 0, 0, 0xC2, 1]+ session_key + RIM_PACKET_TAIL
		self.write(session_packet)
		self.read()
		
	def start(self, pppConfig, pppdCommand):
		'''Start the modem and keep going until ^C'''
		#open modem PTY
		(master,slave)=pty.openpty()
		print "\nModem pty: ",os.ttyname(slave)
				
		print "Initializing Modem"
		try:
			self.init()
		except:
			# If init fails, cleanup and quit
			print "Modem initialization Failed !"
			os.close(master)
			os.close(slave)
			raise

		# Start the USB Modem read thread
		bbThread=BBModemThread(self,master)
		bbThread.start()
		
		print "Modem Started"
		
		if(not pppConfig):
			print "No ppp requested, you can now start pppd manually."
		else:
			#TODO: start ppp in thread/process
			print "Will try to start pppd now, (",pppdCommand,") with config: ",pppConfig
			time.sleep(.5)
			command=[pppdCommand,os.ttyname(slave),"file","conf/"+pppConfig,"nodetach"]
			if bb_util.verbose:
				command.append("debug")
			subprocess.Popen(command)
			# not terminating this myself, since it should terminate by itself (properly) when bbtether is stopped.
		
		print "********************************************\nModem Ready at ",os.ttyname(slave)," Use ^C to terminate\n********************************************"
		
		try:
			# Read from PTY and write to USB modem until ^C
			last=0
			last2=0
			started=False
			while(True):
				data=os.read(master, BUF_SIZE)
				# transform string from PTY into array of signed bytes
				bytes=array.array("B",data)
				if(len(bytes)>0):
					newbytes=[]
					# start grps fix (needs 0x7E around each Frame)
					for b in bytes:
						if started and last2 != 0x7e and last == 0x7e and b != 0x7e:
							bb_util.debug("GPRS fix: Added 0x7E to Frame.")
							newbytes.append(0x7e);
						if (not started) and b != 0x7e and last == 0x7e:
							bb_util.debug("GPRS fix: Started.")
							started=True
						last2=last
						last=b
						newbytes.append(b)					
					# end gprs fix
					self.write(newbytes)
				
		except KeyboardInterrupt:
			print "\nShutting down on ^C"
			
		# Shutting down "gracefully"
		bbThread.stop()
		os.close(master)
		os.close(slave)
					
class BBModemThread( threading.Thread ):	
	'''Thread that reads Modem data from BlackBerry USB'''

	def __init__(self, mdm, master):
		threading.Thread.__init__(self)
		self.master=master
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
					bytes=self.modem.read()
					if(len(bytes)>0):
						if bb_util.end_with_tuple(bytes,RIM_PACKET_TAIL):
							# Those are RIM control packet, not data. So not writing them back to PTY							
							bb_util.debug("Skipping RIM packet ending by "+RIM_PACKET_TAIL)
						else:
							data=array.array("B",bytes)
							os.write(self.master,data.tostring())
				except usb.USBError, error:
					# Ignore the odd "No error" error, must be a pyusb bug, maybe just means no data ?
					if error.message != "No error":
						raise
								
			except:
				if(self.done):
					# Ignoring exception during shutdown attempt
					pass
				else:
					raise

