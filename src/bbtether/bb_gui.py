
'''
Thibaut Colar
BBTether GUI

http://www.wxpython.org/docs/api/
'''
import sys

import bb_messenging
import bb_tether
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
MENU_DEV_RESET=24
MENU_DEV_RESCAN=25
MENU_ABOUT = 94

wx.StdOut, EVT_LOG_APPEND= NewEvent()

class BBFrame(wx.Frame):
	connected = False
	log_pane = None

	def __init__(self, parent, ID, title):
		sys.stdout = SysOutListener()

		wx.Frame.__init__(self, parent, ID, title, wx.DefaultPosition)
		self.CreateStatusBar()
		self.SetStatusText("")

		menuBar = wx.MenuBar()
		menu_file = wx.Menu()
		menu_file.Append(MENU_PREFS, "&Preferences", "Preferences")
		#menu_file.AppendSeparator()
		menu_file.Append(MENU_EXIT, "E&xit", "Terminate the program")

		#item = wx.MenuItem(menu, 0, "Some Item")
		#item.Enable(False)

		menuBar.Append(menu_file, "&File");

		menu_dev = wx.Menu()
		menu_dev.Append(MENU_DEV_RESET, "&Reset", "Force device reset(if stuck)")
		menu_dev.Append(MENU_DEV_RESCAN, "Re&scan", "Force rescan of device endpoints")
		menuBar.Append(menu_dev, "&Device");

		menu_modem = wx.Menu()
		menu_modem.Append(MENU_CONNECT, "&Connect", "Connect the modem")
		menu_modem.Append(MENU_DISCONNECT, "&Disconnect", "Disconnect the modem")
		#menu_file.AppendSeparator()
		menu_modem.Append(MENU_MODEM_RESET, "&Reset", "Force modem reset(if stuck)")
		menuBar.Append(menu_modem, "&Modem");

		menu_help = wx.Menu()
		menu_help.Append(MENU_ABOUT, "&About", "More information about this program")
		menuBar.Append(menu_help, "&Help");

		self.SetMenuBar(menuBar)

		# Menu events
		wx.EVT_MENU(self, MENU_ABOUT, self.onAbout)
		wx.EVT_MENU(self, MENU_EXIT, self.onQuit)
		wx.EVT_MENU(self, MENU_CONNECT, self.onStart)
		wx.EVT_MENU(self, MENU_DISCONNECT, self.onStop)
		# close button
		self.Bind(wx.EVT_CLOSE, self.onQuit)

		self.log_pane = wx.TextCtrl(self, wx.ID_ANY, "", (4, 4), (700, 500), style=wx.TE_MULTILINE | wx.TE_READONLY)
		self.log_pane.Bind(EVT_LOG_APPEND, self.onLogEvent)

		self.Fit()
		self.CenterOnScreen()

	def onLogEvent(self,event):
		text = event.text
		self.log_pane.AppendText(text)

	def onAbout(self, event):
		dlg = wx.MessageDialog(self, "This is the BBGUI Version: " + VERSION + "\n\nMore infos about BBGUI at:\nhttp://wiki.colar.net/bbtether\n\nThibaut Colar", "About BBGUI", wx.OK | wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()

	def onQuit(self, event):
		if self.connected:
			dlg = wx.MessageDialog(self, "You should Disconnect before quitting!\nOtherwise your Blackberry might need a reboot\n\nDo you want to Quit anyway ?\n", "Warning !", wx.OK | wx.CANCEL)
			result = dlg.ShowModal()
			dlg.Destroy()
			if result == wx.ID_OK:
				self.Destroy()
		else:
			self.Destroy()

	def onStart(self, event):
		self.log_pane.Clear()
		options = {"verbose":True}
		fake_args = ["tmobile","-v"]
		#instance & start bbtether
		(options,args)=bb_tether.parse_cmd(fake_args)
		self.bbtether = BBTetherThread(options, args)
		self.bbtether.start()

	def onStop(self, event):
		if self.bbtether:
			self.bbtether.stop()


class SysOutListener:
	
	def write(self, string):
		#wx.GetApp().frame.log_pane.AppendText(string)
		evt = wx.StdOut(text=string)
		wx.PostEvent(wx.GetApp().frame.log_pane, evt)

class BBTetherThread(threading.Thread):
	def __init__(self, options, args):
		threading.Thread.__init__(self)
		self.options=options
		self.args=args
		self.setDaemon(True)

	def stop(self,):
		self.bbtether.shutdown()

	def run (self):
		bb_messenging.status("Starting Modem thread")
		# runs "forever"
		self.bbtether = bb_tether.BBTether()
		self.bbtether.start(self.options, self.args)


class BBGui(wx.App):

	frame = None

	def OnInit(self):
		self.frame = BBFrame(None, -1, "BBGUI")
		self.frame.Show(True)

		self.SetTopWindow(self.frame)
		return True

	# callback methods
	def warn(self, msgs):
		msg = ""
		for m in msgs:
			msg += m + "\n"
		dlg = wx.MessageDialog(self.frame, msg, "Warning!", wx.OK | wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()

	def append_log(self, msg):
		evt = wx.StdOut(text=msg+"\n")
		wx.PostEvent(wx.GetApp().frame.log_pane, evt)

	def update_status(self, msg):
		#TODO: need to be an event too ??
		self.frame.SetStatusText(msg)

