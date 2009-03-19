
'''
Thibaut Colar
BBTether GUI

http://www.wxpython.org/docs/api/
'''
import sys

import bb_messenging
import bb_prefs
import bb_tether
import bb_usb
import bb_util
from bb_version import VERSION
import os
import threading
try:
	import wx
	from wx.lib.newevent import NewEvent
except ImportError:
	print "The GUI requires wxPython to be installed !"
	print "Linux: sudo apt-get install python-wxgtk2.8"
	print "Max OSX: http://www.wxpython.org/download.php (get the one for python 2.5 unicode)"
	os._exit(0)

MENU_PREFS = 2
MENU_EXIT = 4
MENU_CONNECT=14
MENU_DISCONNECT=15
MENU_MODEM_RESET=17
MENU_DEV_CHARGE=22
MENU_DEV_RESET=24
MENU_DEV_RESCAN=25
MENU_ABOUT = 94

# custom events
appendEvent, EVT_LOG_APPEND= NewEvent()
statusEvent, EVT_STATUS= NewEvent()
warnEvent, EVT_WARN= NewEvent()

class BBFrame(wx.Frame):
	connected = False
	log_pane = None

	def __init__(self, parent, ID, title):
		self.bbtether=None
		sys.stdout = SysOutListener()

		wx.Frame.__init__(self, parent, ID, title, wx.DefaultPosition)
		self.CreateStatusBar()
		self.SetStatusText("")

		menuBar = wx.MenuBar()
		menu_file = wx.Menu()
		menu_file.Append(MENU_PREFS, "&Preferences", "Preferences")
		menu_file.AppendSeparator()
		menu_file.Append(MENU_EXIT, "E&xit", "Terminate the program")

		#item = wx.MenuItem(menu, 0, "Some Item")
		#item.Enable(False)
		menuBar.Append(menu_file, "&File");

		menu_dev = wx.Menu()
		menu_dev.Append(MENU_DEV_CHARGE, "&Charge", "Put in charge mode (If the BB complains about low voltage)")
		menu_dev.AppendSeparator()
		menu_dev.Append(MENU_DEV_RESCAN, "Re&scan", "Force rescan of device endpoints")
		menu_dev.AppendSeparator()
		menu_dev.Append(MENU_DEV_RESET, "&Reset", "Force device reset(if stuck)")
		menuBar.Append(menu_dev, "&Device");

		menu_modem = wx.Menu()
		menu_modem.Append(MENU_CONNECT, "&Connect", "Connect the modem")
		menu_modem.Append(MENU_DISCONNECT, "&Disconnect", "Disconnect the modem")
		menuBar.Append(menu_modem, "&Modem");

		#menu_todo = wx.Menu()
		#menu_todo.Append(MENU_CONNECT, "&Upload App", "COB / JAD")
		#menu_todo.Append(MENU_DISCONNECT, "&Backup", "")
		#menuBar.Append(menu_todo, "&Firmware");

		menu_help = wx.Menu()
		menu_help.Append(MENU_ABOUT, "&About", "More information about this program")
		menuBar.Append(menu_help, "&Help");

		self.SetMenuBar(menuBar)

		# Menu events
		wx.EVT_MENU(self, MENU_ABOUT, self.onAbout)
		wx.EVT_MENU(self, MENU_EXIT, self.onQuit)
		wx.EVT_MENU(self, MENU_CONNECT, self.onStart)
		wx.EVT_MENU(self, MENU_DISCONNECT, self.onStop)
		wx.EVT_MENU(self, MENU_DEV_CHARGE, self.onCharge)
		wx.EVT_MENU(self, MENU_DEV_RESET, self.onDevReset)
		wx.EVT_MENU(self, MENU_DEV_RESCAN, self.onDevRescan)
		# close button
		self.Bind(wx.EVT_CLOSE, self.onQuit)

		self.log_pane = wx.TextCtrl(self, wx.ID_ANY, "", (4, 4), (700, 300), style=wx.TE_MULTILINE | wx.TE_READONLY)

		self.Fit()
		self.CenterOnScreen()

		# Binding custom events
		self.log_pane.Bind(EVT_LOG_APPEND, self.onLogEvent)
		self.log_pane.Bind(EVT_STATUS, self.onStatus)
		self.log_pane.Bind(EVT_WARN, self.onWarn)

	def onCharge(self, event):
		berry = bb_usb.find_berry(None, None)
		if berry != None:
			if berry.handle==None:
				berry.open_handle()
			bb_usb.set_bb_power(berry)

	def onDevReset(self, event):
		berry = bb_usb.find_berry(None, None)
		if berry != None:
			if berry.handle==None:
				berry.open_handle()
			bb_usb.reset(berry)

	def onDevRescan(self, event):
		berry = bb_usb.find_berry(None, None)
		if berry != None:
			if berry.handle==None:
				berry.open_handle()
			prefs=bb_prefs.get_prefs()
			prefs.remove_section(bb_prefs.SECTION_EP)
			#Note: will replace prefs with sanedd values
			bb_usb.read_bb_endpoints(berry, None)

	def onStatus(self, event):
		self.SetStatusText(event.text)

	def onLogEvent(self,event):
		self.log_pane.AppendText(event.text)

	def onWarn(self, event):
		dlg = wx.MessageDialog(self, event.text, "Warning!", wx.OK | wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()

	def onAbout(self, event):
		dlg = wx.MessageDialog(self, "This is the BBGUI Version: " + VERSION + "\n\nMore infos about BBGUI at:\nhttp://wiki.colar.net/bbtether\n\nThibaut Colar", "About BBGUI", wx.OK | wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()

	def onQuit(self, event):
		if self.bbtether!=None and self.bbtether.is_running():
			dlg = wx.MessageDialog(self, "You should Disconnect before quitting!\nOtherwise your Blackberry might need a reboot\n\nDo you want to Quit anyway ?\n", "Warning !", wx.OK | wx.CANCEL)
			result = dlg.ShowModal()
			dlg.Destroy()
			if result == wx.ID_OK:
				self.Destroy()
		else:
			self.Destroy()

	def onStart(self, event):
		if self.bbtether!=None and self.bbtether.is_running():
			dlg = wx.MessageDialog(self, "The modem is already Connected.", "Warning!", wx.OK | wx.ICON_INFORMATION)
			dlg.ShowModal()
			dlg.Destroy()
			return
		self.log_pane.Clear()
		fake_args = ["tmobile"]
		if bb_util.verbose:
			fake_args.append("-v")
		#instance & start bbtether
		(options,args)=bb_tether.parse_cmd(fake_args)
		self.bbtether = BBTetherThread(options, args)
		self.bbtether.start()

	def onStop(self, event):
		if self.bbtether==None or not self.bbtether.is_running():
			dlg = wx.MessageDialog(self, "The modem is not Connected.", "Warning!",wx.OK | wx.ICON_INFORMATION)
			dlg.ShowModal()
			dlg.Destroy()
			return
		msg="Please WAIT for shutdown to complete (up to 30s)\n Otherwise you might have to reboot your BB !"
		dlg = wx.MessageDialog(self, msg, "Warning!", wx.OK | wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()
		if self.bbtether!=None:
			self.bbtether.stop()

class SysOutListener:
	def write(self, msg):
		sys.__stdout__.write(msg)
		#and gui too
		evt = appendEvent(text=msg)
		wx.PostEvent(wx.GetApp().frame.log_pane, evt)

class BBTetherThread(threading.Thread):
	def __init__(self, options, args):
		threading.Thread.__init__(self)
		self.options=options
		self.args=args
		self.bbtether=None
		#self.setDaemon(True)

	def stop(self):
		if self.bbtether!=None:
			self.bbtether.shutdown()

	def run (self):
		bb_messenging.status("Starting Modem thread")
		# runs "forever"
		self.bbtether = bb_tether.BBTether()
		self.bbtether.start(self.options, self.args)
		bb_messenging.log("BBTether Thread completed.")

	def is_running(self):
		return self.bbtether!=None and self.bbtether.is_running()



class BBGui(wx.App):

	frame = None

	def OnInit(self):
		self.frame = BBFrame(None, -1, "BBGUI")
		self.frame.Show(True)

		self.SetTopWindow(self.frame)

		# set options
		bb_util.verbose=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN,"verbose",True)

		return True

	# callback methods, called from other process threads(ex: bbtether)
	# Need to use events only as wx is not thread safe !
	def warn(self, msgs):
		msg = ""
		for m in msgs:
			msg += m + "\n"
		evt = warnEvent(text=msg)
		wx.PostEvent(wx.GetApp().frame.log_pane, evt)

	def append_log(self, msg):
		evt = appendEvent(text=msg+"\n")
		wx.PostEvent(wx.GetApp().frame.log_pane, evt)

	def update_status(self, msg):
		evt = statusEvent(text=msg)
		wx.PostEvent(wx.GetApp().frame.log_pane, evt)
		
	#End callback methods
