'''
Thibaut Colar
BBTether GUI

http://www.wxpython.org/docs/api/
'''

from bb_version import VERSION
import os
try:
	from wxPython.wx import *
except ImportError:
	print "The GUI requires wxPython to be installed !"
	print "Linux: sudo apt-get install python-wxgtk2.8"
	print "Max OSX: http://www.wxpython.org/download.php (get the one for python 2.5 unicode)"
	os._exit(0)

MENU_ABOUT=1
MENU_EXIT=2

class BBFrame(wxFrame):
	connected=False

	def __init__(self, parent, ID, title):
		wxFrame.__init__(self, parent, ID, title, wxDefaultPosition)
		self.CreateStatusBar()
		self.SetStatusText("")

		menuBar = wxMenuBar()
		menu_file = wxMenu()
		#menu_file.AppendSeparator()
		menu_file.Append(MENU_EXIT, "E&xit", "Terminate the program")

		menuBar.Append(menu_file, "&File");

		menu_help = wxMenu()
		menu_help.Append(MENU_ABOUT, "&About BBTether","More information about this program")
		menuBar.Append(menu_help, "&Help");

		self.SetMenuBar(menuBar)

		# Menu events
		self.Connect(MENU_ABOUT, -1, wxEVT_COMMAND_MENU_SELECTED, self.onAbout)
		self.Connect(MENU_EXIT, -1, wxEVT_COMMAND_MENU_SELECTED, self.onQuit)
		# close button
		self.Bind(EVT_CLOSE, self.onQuit)
		
		self.panel = wxPanel ( self, -1 )
		self.text = wxTextCtrl(self, wxID_ANY, "", (2,2), (400, 300), style=wxTE_MULTILINE)

	def onAbout(self, event):
		dlg = wxMessageDialog(self, "This is the GUI for bbtether.\nBBTether Version: "+VERSION+"\n\nMore infos about BBTether at:\nhttp://wiki.colar.net/bbtether\n","About BBTether", wxOK | wxICON_INFORMATION)
		dlg.ShowModal()
		dlg.Destroy()

	def onQuit(self, event):
		if self.connected:
			dlg = wxMessageDialog(self, "You should Disconnect before quitting!\nOtherwise your Blackberry might need a reboot\n\nDo you want to Quit anyway ?\n", "Warning !", wxOK | wxCANCEL)
			result=dlg.ShowModal()
			dlg.Destroy()
			if result == wxID_OK:
				self.Destroy()
		else:
			self.Destroy()


class BBGui(wxApp):
    def OnInit(self):
        frame = BBFrame(NULL, -1, "BBTether GUI")
        frame.Show(true)
		
        self.SetTopWindow(frame)
        return true

# Main
gui = BBGui(0)
gui.MainLoop()
