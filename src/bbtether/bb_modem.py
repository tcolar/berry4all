'''
Modem support for Blackberry
Thibaut Colar
'''

import array
import pty
import time

import bb_usb
import bb_util
import os
import subprocess
import threading
import usb

NOTIFY_EVERY=100000
BUF_SIZE=1400
TIMEOUT=50
PPPD_COMMAND="pppd"
MIN_PASSWD_TRIES=2
MODEM_STOP = [0x1, 0x0 ,0x0, 0x0 ,0x0, 0x0, 0x0, 0x0, 0x78, 0x56, 0x34, 0x12]
MODEM_START = [0x1, 0x0 ,0x0, 0x0 ,0x1, 0x0, 0x0, 0x0, 0x78, 0x56, 0x34, 0x12]
# "RIM BYPASS" (usb sniff from windows)
MODEM_BYPASS_PCKT = [0x0, 0x0, 0x18, 0x0, 0x7, 0xff, 0x0, 0x9, 0x52, 0x49, 0x4d, 0x20, 0x42, 0x79, 0x70, 0x61, 0x73, 0x73, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0]

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
		self.data_mode=False

	def write(self, data, timeout=TIMEOUT):
		bb_usb.usb_write(self.device,self.device.modem_writept,data,timeout,"\tModem -> ")
		self.writ+=len(data)
		if(self.red+self.writ>self.lastcount+NOTIFY_EVERY):
			print "GPRS Infos: Received Bytes:",self.red,"	Sent Bytes:",+self.writ
			self.lastcount=self.red+self.writ
			
	def read(self, size=BUF_SIZE,timeout=TIMEOUT):
		data=[]
		datar=[1]
		while len(datar) > 0:
			datar=bb_usb.usb_read(self.device,self.device.modem_readpt,size,timeout,"\tModem <- ")
			if len(datar) > 0:
				data.extend(datar)
		self.red+=len(data)
		if(self.red+self.writ>self.lastcount+NOTIFY_EVERY):
			print "GPRS Infos: Received Bytes:",self.red,"	Sent Bytes:",+self.writ
			self.lastcount=self.red+self.writ
		return data

	def init(self):
		'''Initialize the modem and start the RIM session'''
		self.session_key=[0x42,0x42,0x54,0x45,0x54,0x48,0x45,0x52] #(BBTETHER)
		# clear endpoints
		bb_usb.clear_halt(self.device,self.device.modem_readpt)
		bb_usb.clear_halt(self.device,self.device.modem_writept)
		init_packet=[0,0,0,0,0,0,0,0,3,0,0,0,0,0xc2,1,0,0,0,0,0,0,0,0,0]+RIM_PACKET_TAIL
		self.write(init_packet)
		self.read()
		try:
			# we hangup the modem and close a potentially open session, in case that did not happen properly
			# during last shutdown
			self.write([0x41,0x54,0x5a,0xd]) #hangup modem (ATZ)
			self.read()
			end_session_packet=[0, 0, 0, 0, 0x23, 0, 0, 0, 3, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
			self.write(end_session_packet)
			self.read()
		except:
			print "Failed closing previous session, continuing anyway"
		# reset modem
		self.write(MODEM_STOP)
		self.read()
		self.write(MODEM_STOP)
		self.read()
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
		# Send session key & start sessin (as seen on windows trace)
		# At least on my Pearl if I don't send this now, the device will reboot itself during heavy data transfer later (odd)
		session_packet=[0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
		self.write(session_packet)
		self.read()
		session_packet=[0, 0, 0, 0, 0x23, 0, 0, 0, 3, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
		self.write(session_packet)
		self.read()
		print "session pack sent"
		#self.write(MODEM_BYPASS_PCKT)
		#self.read()
		
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
			process=subprocess.Popen(command)
		
		print "********************************************\nModem Ready at ",os.ttyname(slave)," Use ^C to terminate\n********************************************"
		
		#session_packet=[0, 0, 0, 0, 0x23, 0, 0, 0, 3, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
		#self.write(session_packet)
		#self.read()

		try:
			# Read from PTY and write to USB modem until ^C
			while(True):
				data=os.read(master, BUF_SIZE)
				# transform string from PTY into array of signed bytes
				bytes=array.array("B",data)
				if(len(bytes)>0):
					if (not self.data_mode) and len(bytes)>4 and bytes[0] == 0x7e :
						bb_util.debug("GPRS fix: Started.")
						self.data_mode=True

					if self.data_mode:
						if bytes[0] != 0x7E:
							bb_util.debug("GPRS fix: Added 0x7E to Frame Start.")
							bytes.insert(0,0x7E)
						if bytes[len(bytes)-1] != 0x7E:
							bb_util.debug("GPRS fix: Added 0x7E to Frame End.")
							bytes.append(0x7E)
					# end gprs fix
					print "Will write "+str(len(bytes))+" bytes"
					self.write(bytes)
				
		except KeyboardInterrupt:
			print "\nShutting down on ^c"
			
		# Shutting down "gracefully"
		try:
			self.data_mode=False
			self.write([0x41,0x54,0x5a,0xd]) #hangup modem (ATZ)
			self.read()
			# stop BB modem
			self.write(MODEM_STOP)
			# close RIM session (as traced on windows)
			end_session_packet=[0, 0, 0, 0, 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
			self.write(end_session_packet)
			bbThread.stop()
		except:
			print "Failure during shutdown"
		# stopping pppd
		os.kill(process.pid,0)#/*signal.SIGKILL*/
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
							bb_util.debug("Skipping RIM packet")
							# what if it's just data that ends ,like rim packet ????
						else:
							data=array.array("B",bytes)
							print "Read  "+str(len(bytes))+" bytes"
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
		print "Modem thread Stopped"
