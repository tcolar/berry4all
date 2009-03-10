'''
Modem support for Blackberry
Thibaut Colar
'''

import array
import pty
import signal
import time
import fcntl

import bb_usb
import bb_util
import os
import random
import subprocess
import threading
import usb

NOTIFY_EVERY=100000
BUF_SIZE=25000
# it appears that with timeout to low(50), usb read hangs at some point (libusb author recommands>100)
TIMEOUT=250
PPPD_COMMAND="pppd"
MIN_PASSWD_TRIES=2
MODEM_STOP = [0x1, 0x0 ,0x0, 0x0 ,0x0, 0x0, 0x0, 0x0, 0x78, 0x56, 0x34, 0x12]
MODEM_START = [0x1, 0x0 ,0x0, 0x0 ,0x1, 0x0, 0x0, 0x0, 0x78, 0x56, 0x34, 0x12]
# "RIM BYPASS" (usb sniff from windows)
MODEM_BYPASS_PCKT = [0x0, 0x0, 0x18, 0x0, 0x7, 0xff, 0x0, 0x9, 0x52, 0x49, 0x4d, 0x20, 0x42, 0x79, 0x70, 0x61, 0x73, 0x73, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0]

# Packets ending by this are RIM control packets (! data)
RIM_PACKET_TAIL=[0x78, 0x56, 0x34, 0x12]
MTU=1500
# we read this many bytes at a time max from usb, without limit it slows down downloads
MAX_RD_SIZE=3000

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
		bb_util.debug("Writing data size: "+str(len(data)))
		bb_usb.usb_write(self.device,self.device.modem_writept,data,timeout,"\tModem -> ")
		self.writ+=len(data)
		if(self.red+self.writ>self.lastcount+NOTIFY_EVERY):
			print "GPRS Infos: Received Bytes:",self.red,"	Sent Bytes:",+self.writ
			self.lastcount=self.red+self.writ
			
	def read(self, size=BUF_SIZE,timeout=TIMEOUT):
		data=[]
		datar=[1]
		while len(datar) > 0 and len(data)<MAX_RD_SIZE:
			datar=bb_usb.usb_read(self.device,self.device.modem_readpt,size,timeout,"\tModem <- ")
			if len(datar) > 0:
				data.extend(datar)
		self.red+=len(data)
		if(self.red+self.writ>self.lastcount+NOTIFY_EVERY):
			print "GPRS Infos: Received Bytes:",self.red,"	Sent Bytes:",+self.writ
			self.lastcount=self.red+self.writ
		if len(data)>0:
			bb_util.debug("read: "+str(len(data)))
		return data

	def init(self):
		'''Initialize the modem and start the RIM session'''
		# create a (semi-random) session key
		self.session_key=[0x42,0x42,0x54,0,0,0,0,0] #(BBT)
		for i in range(5):
			self.session_key[i+3]=random.randrange(0, 256)
		# clear endpoints
		bb_usb.clear_halt(self.device,self.device.modem_readpt)
		bb_usb.clear_halt(self.device,self.device.modem_writept)
		# reset modem
		self.write(MODEM_STOP)
		self.read()
		self.write(MODEM_STOP)
		self.read()
		self.write(MODEM_START)
		answer=self.read()
		# check for password request (newer devices)
		if len(answer)>0 and answer[0]==0x2 and bb_util.end_with_tuple(answer,RIM_PACKET_TAIL):
			#triesLeft=answer[8]
			#seed=answer[4:8]
			#print "Got password Request from Device (",triesLeft," tries left)"
			#triesLeft=answer[8]
			#if triesLeft <= MIN_PASSWD_TRIES:
			#	print "The device has only "+answer[8]+" password tries left, we don't want to risk it! Reboot/unplug the device to reset tries.";
			#	raise Exception
			#TODO
			print "Password protected Device not supported yet !"
			raise Exception		
		else:
			print "No password requested."	

		# Send init session (as seen on windows trace)
		session_packet=[0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
		self.write(session_packet)
		self.read()
		print "session pack sent"
	
	def readline(self,fd, timeout=15000):
		'''
		Read a line until:
		- no more data avail
		- end of line found (\n or \rx or \r\n)
		'''	
		line=""
		char=''
		prev=0
		elapsed=0
		while(True):
			try:
				char=os.read(fd, 1)
			except OSError:
				if prev == 0xD:
					line+=char
					break
				#not ready yet
				time.sleep(.1)
				elapsed+=100
				if elapsed > timeout:
					raise
				continue
			if prev == 0xD and ord(char) != 0xA:
				os.lseek(fd,-1,1)
				line+=char
				break
			if ord(char) == 0xA:
				line+=char
				break
			else:
				line+=char
			prev=ord(char)
		return line
		
	def start(self, pppConfig, pppdCommand):
		'''Start the modem and keep going until ^C'''
		#open modem PTY
		(master,slave)=pty.openpty()
		# make master non blocking
		flag = fcntl.fcntl(master, fcntl.F_GETFL)
		fcntl.fcntl(master, fcntl.F_SETFL, flag | os.O_NDELAY)
		 
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
			#TODO: start pppd in thread/process
			print "Will try to start pppd now, (",pppdCommand,") with config: ",pppConfig
			time.sleep(.5)
			command=[pppdCommand,os.ttyname(slave),"file","conf/"+pppConfig,"nodetach"]
			if bb_util.verbose:
				command.append("debug")
			process=subprocess.Popen(command)
		
		print "********************************************\nModem Ready at ",os.ttyname(slave)," Use ^C to terminate\n********************************************"
		
		try:
			# Read from PTY and write to USB modem until ^C
			
			prev2=0x7E #start with this, so first 0x7E not doubled
			prev=0x00
			while(True):
				
				if not self.data_mode:
					data=self.readline(master)
					print "PPP data: "+data
					# check for ~p (data mode start)
					bytes=array.array("B",data)
					if len(data)>0 and data.startswith("~p") :
						bb_util.debug("Entering data mode")
						self.data_mode=True

					# check for special bbtether packet (BBT_xx.) where xx is the command
					if data.startswith("BBT_OS"):
						print "Starting session"
						session_packet=[0, 0, 0, 0, 0x23, 0, 0, 0, 3, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
						self.write(session_packet)
						# return OK, so chat script can proceed to next step
						os.write(master,"\nOK\n")
					else:
						self.write(bytes)
				else:
					data=[]
					try:											
						data=os.read(master, BUF_SIZE)
					except OSError:
						# wait a tiny bit (10 ms)
						time.sleep(.01)
					if len(data) > 0:
						bytes=array.array("B",data)
						newbytes=[]
						for i in range(len(data)):
							# doubling frame separators (0x7E)
							# a single in between frames should work but does not always
							if prev2!=0x7E and prev==0x7E and bytes[i]!=0x7E:
								bb_util.debug("doubling 0x7E at: "+str(i))
								newbytes.append(0x7E)
								prev2=0x7E	
							else:
								prev2=prev
							prev=bytes[i]										
							newbytes.append(bytes[i])
						self.write(newbytes)
				
		except KeyboardInterrupt:
			print "\nShutting down on ^c"
			
		# Shutting down "gracefully"
		try:
			self.data_mode=False
			#stop PPP
			self.write([0x41,0x54,0x48,0x0d]) # send ATH (modem hangup)
			self.readline(master)
			# send SIGHUP(1) to pppd (causes ppd to hangup and terminate)
			os.kill(process.pid,1)
			# wait for pppd to be done
			os.waitpid(process.pid, 0)
			print "PPP finished"
			# Ending session (by opening different one, as seen in windows trace - odd)
			end_session_packet=[0, 0, 0, 0, 0x23, 0, 0, 0, 3, 0, 0, 0, 0, 0xC2, 1, 0] + [0x71,0x67,0x7d,0x20,0x3c,0xcd,0x74,0x7d] + RIM_PACKET_TAIL
			self.write(end_session_packet)
			# stop BB modem
			self.write(MODEM_STOP)
			# Ending the actual session (as traced on windows)
			end_session_packet=[0, 0, 0, 0, 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0xC2, 1, 0]+ self.session_key + RIM_PACKET_TAIL
			self.write(end_session_packet)
			#stopping modem read thread
			print "Stopping modem thread"
			bbThread.stop()
		except Exception, error:
			print "Failure during shutdown ",error
		# stopping pppd
		try:
			# making sure ppp process is gone (only if something went wrong)
			os.kill(process.pid,signal.SIGKILL)
		except:
			# ppd already gone, all is good
			pass
		# close pty descriptors
		os.close(master)
		os.close(slave)
		# done
					
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
							#print "Read  "+str(len(bytes))+" bytes"
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
