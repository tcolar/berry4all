
'''
Thibaut Colar
BBTether GUI

http://www.wxpython.org/docs/api/
'''
import sys
import time

import bb_messenging
import bb_prefs
import bb_tether
import bb_usb
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
MENU_CLEAR_CONSOLE = 3
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
askEvent, EVT_ASK= NewEvent()

class BBFrame(wx.Frame):
	connected = False
	log_pane = None
	parent=None
	prefs=None

	def __init__(self, parent, ID, title):
		self.bbtether=None
		sys.stdout = SysOutListener()

		wx.Frame.__init__(self, parent, ID, title, wx.DefaultPosition)
		self.CreateStatusBar()
		self.SetStatusText("")

		menuBar = wx.MenuBar()
		menu_file = wx.Menu()
		menu_file.Append(MENU_PREFS, "&Preferences", "Preferences")
		menu_file.Append(MENU_CLEAR_CONSOLE, "&Clear Log Console", "Clear the log console.")
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
		wx.EVT_MENU(self, MENU_PREFS, self.onPrefs)
		wx.EVT_MENU(self, MENU_CLEAR_CONSOLE, self.onClearConsole)
		# close button
		self.Bind(wx.EVT_CLOSE, self.onQuit)

		self.log_pane = wx.TextCtrl(self, wx.ID_ANY, "", (4, 4), (700, 300), style=wx.TE_MULTILINE | wx.TE_READONLY)

		self.Fit()
		self.CenterOnScreen()

		# Binding custom events
		self.Bind(EVT_LOG_APPEND, self.onLogEvent)
		self.Bind(EVT_STATUS, self.onStatus)
		self.Bind(EVT_WARN, self.onWarn)
		self.Bind(EVT_ASK, self.onAsk)

	def set_parent(self, parent):
		self.parent=parent

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

	def onClearConsole(self,event):
		self.log_pane.Clear()

	def onStatus(self, event):
		self.SetStatusText(event.text)

	def onLogEvent(self,event):
		self.log_pane.AppendText(event.text)

	def onWarn(self, event):
		dlg = wx.MessageDialog(self, event.text, "Warning!", wx.OK | wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()
		self.parent.set_answer("OK")

	def onAsk(self, event):
		if not event.hide_input:
			dlg = wx.TextEntryDialog(self, event.caption,"Question", event.default)
		else:
			dlg = wx.PasswordEntryDialog(self, event.caption,"Question", event.default)
		dlg.ShowModal()
		dlg.Destroy()
		self.parent.set_answer(dlg.GetValue())
		return dlg.GetValue()

	def onAbout(self, event):
		dlg = wx.MessageDialog(self, "This is the BBGUI Version: " + VERSION + "\n\nMore infos about BBGUI at:\nhttp://wiki.colar.net/bbtether\n\nThibaut Colar", "About BBGUI", wx.OK | wx.ICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()

	def onQuit(self, event):
		if self.bbtether!=None and self.bbtether.is_running():
			dlg = wx.MessageDialog(self, "You should Disconnect before quitting!\nOtherwise your Blackberry might need a reboot\n\nDo you want to Quit anyway ?\n", "Warning !", wx.OK | wx.CANCEL)
			result = dlg.ShowModal()
			dlg.Destroy()
			if result != wx.ID_OK:
				return
		if self.prefs != None:
			try:
				self.prefs.Destroy()
			except:
				pass
		try:
			self.Destroy()
		except:
			pass

	def onStart(self, event):
		if self.bbtether!=None and self.bbtether.is_running():
			dlg = wx.MessageDialog(self, "The modem is already Connected.", "Warning!", wx.OK | wx.ICON_INFORMATION)
			dlg.ShowModal()
			dlg.Destroy()
			return
		# ask config to use
		pppd=bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "pppd_config", "")
		#todo: use a SingleChoiceDialog
		evt=askEvent(caption="PPP config to use (EX: tmobile) see conf/ folder.",hide_input=False,default=pppd)
		pppconf=self.onAsk(evt)
		if pppconf != pppd:
			prefs=bb_prefs.get_prefs()
			prefs.set(bb_prefs.SECTION_MAIN,"pppd_config",pppconf)
			bb_prefs.save_prefs(prefs)
		fake_args = [pppconf]
		if bb_messenging.verbose:
			fake_args.append("-v")
		# TODO: temporary
		fake_args.append("--veryverbose")
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

	def onPrefs(self,event):
		if self.prefs==None:
			self.prefs=PreferencesFrame()
		self.prefs.Show()

class PreferencesFrame(wx.Frame):
	def __init__(self):
		wx.Frame.__init__(self,None,-1,"BBGUI Preferences")
		
		mainpanel=wx.Panel(self)
		nb=wx.Notebook(mainpanel)
		basic=wx.Panel(nb)

		basicsizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
		basicsizer.AddGrowableCol(1)
		
		passwordl=wx.StaticText(basic,-1,"Device Password:")
		password=wx.TextCtrl(basic,-1,"fsdffds",size=(200,-1),style=wx.TE_PASSWORD)
		basicsizer.Add(passwordl, 0, wx.ALIGN_RIGHT,wx.ALIGN_CENTER_VERTICAL)
		basicsizer.Add(password, 0, wx.EXPAND)
		verbosel=wx.StaticText(basic,-1,"Verbose logging")
		verbose=wx.CheckBox(basic,-1,"")
		basicsizer.Add(verbosel, 0, wx.ALIGN_RIGHT,wx.ALIGN_CENTER_VERTICAL)
		basicsizer.Add(verbose, 0, 0)
		sverbosel=wx.StaticText(basic,-1,"Extra Verbose !")
		sverbose=wx.CheckBox(basic,-1,"")
		basicsizer.Add(sverbosel, 0, wx.ALIGN_RIGHT,wx.ALIGN_CENTER_VERTICAL)
		basicsizer.Add(sverbose, 0, 0)
		basic.SetSizer(basicsizer)
		nb.AddPage(basic,"General")
		
		modem=wx.Panel(nb)
		modemsizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
		modemsizer.AddGrowableCol(1)
		pppdconfl=wx.StaticText(modem,-1,"PPPD Config:")
		pppdconf=wx.TextCtrl(modem,-1,"tmobile")
		modemsizer.Add(pppdconfl, 0, wx.ALIGN_RIGHT,wx.ALIGN_CENTER_VERTICAL)
		modemsizer.Add(pppdconf, 0, wx.EXPAND)
		pppdl=wx.StaticText(modem,-1,"PPPD path:")
		pppd=wx.TextCtrl(modem,-1,"/usr/bin/pppd")
		modemsizer.Add(pppdl, 0, wx.ALIGN_RIGHT,wx.ALIGN_CENTER_VERTICAL)
		modemsizer.Add(pppd, 0, wx.EXPAND)
		modem.SetSizer(modemsizer)
		nb.AddPage(modem,"Modem")
		
		usb=wx.Panel(nb)
		usbsizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
		usbsizer.AddGrowableCol(1)
		rpl=wx.StaticText(usb,-1,"Data read point:")
		rp=wx.TextCtrl(usb,-1,"")
		usbsizer.Add(rpl, 0, wx.ALIGN_RIGHT,wx.ALIGN_CENTER_VERTICAL)
		usbsizer.Add(rp, 0, wx.EXPAND)
		# TODO: wp,mrp,mwp
		usb.SetSizer(usbsizer)
		nb.AddPage(usb,"USB")
		
		#save=wx.Button(self,-1,"Save")
		
		#cancel=wx.Button(self,-1,"Cancel")
		#sizer.Add(cancel)
		#sizer.Add(save,0,wx.ALIGN_RIGHT)
		
		mainsizer=wx.BoxSizer()
		mainsizer.Add(nb, 1, wx.EXPAND)
		mainpanel.SetSizer(mainsizer)
		self.CenterOnScreen()

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

	answer = None
	frame = None

	def OnInit(self):
		verbose=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN,"verbose",False)
		bb_messenging.verbose=verbose
		very=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN,"veryverbose",False)
		bb_messenging.veryVerbose=very
	
		self.frame = BBFrame(None, -1, "BBGUI")
		self.frame.set_parent(self)
		self.frame.Show(True)

		self.SetTopWindow(self.frame)

		return True

	# callback methods, called from other process threads(ex: bbtether)
	# Need to use events only as wx is not thread safe !
	def ask(self, caption, hide_input, default_value):
		print "ask"
		evt=askEvent(caption=caption,hide_input=hide_input,default=default_value)
		print "post evt"
		wx.PostEvent(self.frame, evt)
		print "wait"
		return self.wait_for_answer()

	def warn(self, msgs, waitFor=False):
		msg = ""
		for m in msgs:
			msg += m + "\n"
		evt = warnEvent(text=msg)
		wx.PostEvent(self.frame, evt)
		if waitFor:
			self.wait_for_answer()

	def append_log(self, msg):
		evt = appendEvent(text=msg+"\n")
		wx.PostEvent(self.frame, evt)

	def update_status(self, msg):
		evt = statusEvent(text=msg)
		wx.PostEvent(self.frame, evt)
	
	#End callback methods

	def set_answer(self,ans):
		self.answer=ans

	def wait_for_answer(self):
		self.answer=None
		while self.answer == None:
			time.sleep(.5)
		return self.answer
	