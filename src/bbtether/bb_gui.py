
'''
Thibaut Colar
BBTether GUI

http://www.wxpython.org/docs/api/
'''
import os
try:
    import wx
except ImportError:
    print "The GUI requires wxPython to be installed !"
    print "Linux: sudo apt-get install python-wxgtk2.8"
    print "Max OSX: http://www.wxpython.org/download.php (get the one for python 2.5 unicode)"
    os._exit(0)

import sys
import time

import base64
import bb_messenging
import bb_prefs
import bb_tether
import bb_usb
from bb_version import VERSION
import threading
from wx.lib.newevent import NewEvent

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
pickEvent, EVT_PICK= NewEvent()
infoEvent, EVT_INFO= NewEvent()
networkStatsEvent, EVT_NETWORK_STATS= NewEvent()

# prepare icon
icon=None
def get_icon():
    global icon
    if icon == None:
        image = wx.Image('img/berry4all.png', wx.BITMAP_TYPE_PNG).ConvertToBitmap()
        icon = wx.EmptyIcon()
        icon.CopyFromBitmap(image)
    return icon

def get_ppp_confs():
    configs=[]
    # We don't cache so user can add files on the fly
    for file in os.listdir ("conf/"):
        if not file.endswith("-chat") and not file.startswith('.'):
            configs.append(file)
    configs.sort()
    return configs

class BBFrame(wx.Frame):
    connected = False
    log_pane = None
    parent=None
    prefs=None
    ns_thread=None

    def __init__(self, parent, ID, title):
        global icon
        self.bbtether=None
        sys.stdout = SysOutListener()

        wx.Frame.__init__(self, parent, ID, title, wx.DefaultPosition)

        self.SetIcon(get_icon())
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
        menuBar.Append(menu_file, "&File")

        menu_dev = wx.Menu()
        menu_dev.Append(MENU_DEV_CHARGE, "&Charge", "Put in charge mode (If the BB complains about low voltage)")
        menu_dev.AppendSeparator()
        menu_dev.Append(MENU_DEV_RESCAN, "Re&scan", "Force rescan of device endpoints")
        menu_dev.AppendSeparator()
        menu_dev.Append(MENU_DEV_RESET, "&Reset", "Force device reset(if stuck)")
        menuBar.Append(menu_dev, "&Device")

        menu_modem = wx.Menu()
        menu_modem.Append(MENU_CONNECT, "&Connect", "Connect the modem")
        menu_modem.Append(MENU_DISCONNECT, "&Disconnect", "Disconnect the modem")
        menuBar.Append(menu_modem, "&Modem")

        #menu_todo = wx.Menu()
        #menu_todo.Append(MENU_CONNECT, "&Upload App", "COB / JAD")
        #menu_todo.Append(MENU_DISCONNECT, "&Backup", "")
        #menuBar.Append(menu_todo, "&Firmware")

        menu_help = wx.Menu()
        menu_help.Append(MENU_ABOUT, "&About", "More information about this program")
        menuBar.Append(menu_help, "&Help")

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

        sizer=wx.BoxSizer(wx.VERTICAL)
        self.modem_panel=ModemPanel(self)
        self.log_pane = wx.TextCtrl(self, wx.ID_ANY, "", (4, 4), (700, 300), style=wx.TE_MULTILINE | wx.TE_READONLY)
        sizer.Add(self.modem_panel,0,wx.EXPAND)
        sizer.Add(self.log_pane,0,wx.EXPAND)
        self.SetSizer(sizer)

        self.Fit()
        self.CenterOnScreen()

        # Binding custom events
        self.Bind(EVT_LOG_APPEND, self.onLogEvent)
        self.Bind(EVT_STATUS, self.onStatus)
        self.Bind(EVT_WARN, self.onWarn)
        self.Bind(EVT_ASK, self.onAsk)
        self.Bind(EVT_INFO, self.onInfo)
        self.Bind(EVT_NETWORK_STATS, self.onStats)

    def onStats(self,event):
        self.modem_panel.field_con_time.SetLabel("%d:%d" % (event.elapsed/60,event.elapsed%60))
        self.modem_panel.field_con_speed.SetLabel("%d / %d" % (event.speedDown, event.speedUp))
        self.modem_panel.field_con_avg_speed.SetLabel("%d / %d" % (event.avgDown, event.avgUp))
        self.modem_panel.field_con_max_speed.SetLabel("%d / %d" % (event.maxDown, event.maxUp))
        self.modem_panel.field_con_transfer.SetLabel("%d / %d" % (event.totalDown, event.totalUp))

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
            prefs.remove_section(bb_prefs.SECTION_SCANNED_EP)
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
        returnCode=dlg.ShowModal()
        dlg.Destroy()
        if returnCode == wx.ID_CANCEL:
            return None
        self.parent.set_answer(dlg.GetValue())
        return dlg.GetValue()

    def onPick(self, event):
        dlg=wx.SingleChoiceDialog (self, event.caption, 'Choose One', event.choices)
        try:
            index=event.choices.index(event.default)
            dlg.SetSelection(index)
        except ValueError:
            # selection not in list, ignore
            pass
        returnCode=dlg.ShowModal()
        dlg.Destroy()
        if returnCode == wx.ID_CANCEL:
            return None
        selection=event.choices[dlg.GetSelection()]
        self.parent.set_answer(selection)
        return selection

    def onAbout(self, event):
        dlg = wx.MessageDialog(self, "This is the Berry4All Version: " + VERSION + "\n\nMore infos about Berry4All at:\nhttp://www.berry4all.com\n\nThibaut Colar", "About Berry4All", wx.OK | wx.ICON_INFORMATION)
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
        pppdconf=bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "pppd_config", "")
        # if none picked yet, ask and save in prefs for next time
        if pppdconf == "":
            choices=get_ppp_confs()
            choices.append("--Do not start PPPD--")
            evt=pickEvent(caption="PPP config to use (EX: tmobile) see conf/ folder.",choices=choices,default=pppdconf)
            pppconf=self.onPick(evt)
            print "pppconf:"+str(pppconf)
            if pppconf==None:
                # cancelled
                return
            if pppconf == "--Do not start PPPD--":
                pppconf=""
            bb_prefs.set(bb_prefs.SECTION_MAIN,"pppd_config",pppconf)
            bb_prefs.save_prefs()

        fake_args=self.build_args_from_prefs()
        bb_messenging.log("Will run bbtether with args: "+str(fake_args))

        (options,args)=bb_tether.parse_cmd(fake_args)
        self.bbtether = BBTetherThread(options, args)
        self.bbtether.start()

    def build_args_from_prefs(self):
        fake_args=[]
        fake_args.append(bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "pppd_config", ""))
        password=base64.b64decode(bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "password", ""))
        if len(password) > 0:
            fake_args.append("-P")
            fake_args.append(password)
        verb=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN, "verbose", False)
        if verb:
            fake_args.append("-v")
        vverb=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN, "veryverbose", False)
        if vverb:
            fake_args.append("--veryverbose")
        data_mode=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN, "data_mode", False)
        if data_mode:
            fake_args.append("-m")
        pppd=bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "pppd_path", "/usr/bin/pppd")
        if pppd != "/usr/bin/pppd":
            fake_args.append("-p")
            fake_args.append(pppd)
        device=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "device", "")
        if device != "":
            fake_args.append("-d")
            fake_args.append(device)
        interface=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "interface", "")
        if interface != "":
            fake_args.append("-i")
            fake_args.append(interface)
        bus=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "bus", "")
        if bus != "":
            fake_args.append("-b")
            fake_args.append(bus)
        rp=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "readpt", "")
        if rp != "":
            fake_args.append("-w")
            fake_args.append(rp)
        wp=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "writept", "")
        if wp != "":
            fake_args.append("-x")
            fake_args.append(wp)
        mrp=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "modem_readpt", "")
        if mrp != "":
            fake_args.append("-y")
            fake_args.append(mrp)
        mwp=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "modem_writept", "")
        if mwp != "":
            fake_args.append("-z")
            fake_args.append(mwp)
        return fake_args

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
        if self.ns_thread!=None:
            self.ns_thread.stop()

    def onPrefs(self,event):
        if self.prefs==None:
            self.prefs=PreferencesFrame()
        self.prefs.Show()

    def onInfo(self,event):
        field=event.field
        msg=event.text
        if field=="ip":
            self.modem_panel.field_ip.SetLabel(msg)
            # we will consider the connection "started" once we receive an IP
            if self.ns_thread==None:
                self.ns_thread=NetworkStatsThread(self)
                self.ns_thread.start()
        if field=="peer":
            self.modem_panel.field_peer.SetLabel(msg)
        if field=="dns":
            self.modem_panel.field_dns.SetLabel(msg)
        if field=="dns2":
            self.modem_panel.field_dns2.SetLabel(msg)

class NetworkStatsThread(threading.Thread):
    done=False
    start_time=None
    def __init__(self, parent):
        threading.Thread.__init__(self)
        self.start_time=time.time()
        self.frame=parent

    def stop(self):
        self.done=True

    def run (self):
        bb_messenging.status("Starting Network Stats thread")
        modem=self.frame.bbtether.bbtether.modem
        lastIn=0
        lastOut=0
        maxIn=0
        maxOut=0
        while(True):
            time.sleep(1)
            if modem==None or self.done:
                self.done=True
                break
            try:
                totalDown=modem.red
                totalUp=modem.writ
                newIn=totalUp-lastIn
                newOut=totalDown-lastOut
                lastIn=totalUp
                lastOut=totalDown
                elapsed=time.time()-self.start_time
                if newIn > maxIn:
                    maxIn=newIn
                if newOut > maxOut:
                    maxOut=newOut
                evt = networkStatsEvent(
                #TODO: hours
                    elapsed=elapsed,
                    speedUp=newIn*8/1024,
                    speedDown=newOut*8/1024,
                    avgUp=totalUp*8/1024/elapsed,
                    avgDown=totalDown*8/1024/elapsed,
                    maxDown=maxOut*8/1024,
                    maxUp=maxIn*8/1024,
                    totalUp=totalUp/1024,
                    totalDown=totalDown/1024
                )
                wx.PostEvent(self.frame, evt)
            except:
                raise
                bb_messenging.log("Network stats thread error.")
                self.done=True
        bb_messenging.log("Network stats thread completed.")

class ModemPanel(wx.Panel):
    def __init__(self, parent):
        wx.Panel.__init__(self, parent)
        mainsizer=wx.BoxSizer(wx.HORIZONTAL)
        self.field_ip=wx.StaticText(self,-1,"",size=(130,-1))
        self.field_peer=wx.StaticText(self,-1,"")
        self.field_dns=wx.StaticText(self,-1,"")
        self.field_dns2=wx.StaticText(self,-1,"")
        self.field_con_time=wx.StaticText(self,-1,"",size=(130,-1))
        self.field_con_speed=wx.StaticText(self,-1,"")
        self.field_con_avg_speed=wx.StaticText(self,-1,"")
        self.field_con_max_speed=wx.StaticText(self,-1,"")
        self.field_con_transfer=wx.StaticText(self,-1,"")

        box=wx.StaticBox(self,label="IP infos")
        ipsizer=wx.StaticBoxSizer(box,orient=wx.VERTICAL)
        sizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
        sizer.Add(wx.StaticText(self,-1,"Local IP:"))
        sizer.Add(self.field_ip)
        sizer.Add(wx.StaticText(self,-1,"Peer IP:"))
        sizer.Add(self.field_peer)
        sizer.Add(wx.StaticText(self,-1,"DNS server:"))
        sizer.Add(self.field_dns)
        sizer.Add(wx.StaticText(self,-1,"Alt. DNS  :"))
        sizer.Add(self.field_dns2)
        ipsizer.Add(sizer,0,wx.EXPAND)

        box=wx.StaticBox(self,label="Connection infos")
        consizer=wx.StaticBoxSizer(box,orient=wx.VERTICAL)
        sizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
        sizer.Add(wx.StaticText(self,-1,"Connect. time    :"))
        sizer.Add(self.field_con_time)
        sizer.Add(wx.StaticText(self,-1,"Cur. speed (Kb/s):"))
        sizer.Add(self.field_con_speed)
        sizer.Add(wx.StaticText(self,-1,"AVG speed (Kb/s) :"))
        sizer.Add(self.field_con_avg_speed)
        sizer.Add(wx.StaticText(self,-1,"Max speed (Kb/s) :"))
        sizer.Add(self.field_con_max_speed)
        sizer.Add(wx.StaticText(self,-1,"Transfered KB    :"))
        sizer.Add(self.field_con_transfer)
        consizer.Add(sizer,0,wx.EXPAND)

        box=wx.StaticBox(self,label="Network Graphs")
        graphsizer=wx.StaticBoxSizer(box,orient=wx.HORIZONTAL)
        graphsizer.Add(wx.StaticText(self,-1,"--TBD--",size=(220,-1)))

        mainsizer.Add(ipsizer,0,wx.EXPAND)
        mainsizer.Add(consizer,0,wx.EXPAND)
        mainsizer.Add(graphsizer,0,wx.EXPAND)
        self.SetSizer(mainsizer)

class PreferencesFrame(wx.Frame):
    def __init__(self):
        global icon
        wx.Frame.__init__(self,None,-1,"Berry4All Preferences")
        self.SetIcon(get_icon())
        self.Bind(wx.EVT_CLOSE, self.onQuit)

        mainpanel=wx.Panel(self)
        nb=wx.Notebook(mainpanel)
        basic=wx.Panel(nb)

        basicsizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
        basicsizer.AddGrowableCol(1)

        passwordl=wx.StaticText(basic,-1,"Device Password:")
        passwords=bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "password", "")
        passwords=base64.b64decode(passwords)
        self.password=wx.TextCtrl(basic,-1,passwords,size=(200,-1),style=wx.TE_PASSWORD)
        basicsizer.Add(passwordl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        basicsizer.Add(self.password, 0, wx.EXPAND)
        verbosel=wx.StaticText(basic,-1,"Verbose logging")
        verboses=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN, "verbose", True)
        self.verbose=wx.CheckBox(basic,-1,"")
        self.verbose.SetValue(verboses)
        basicsizer.Add(verbosel, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        basicsizer.Add(self.verbose, 0, 0)
        sverbosel=wx.StaticText(basic,-1,"Extra Verbose !")
        sverboses=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN, "veryverbose", False)
        self.sverbose=wx.CheckBox(basic,-1,"")
        self.sverbose.SetValue(sverboses)
        basicsizer.Add(sverbosel, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        basicsizer.Add(self.sverbose, 0, 0)
        basic.SetSizer(basicsizer)
        nb.AddPage(basic,"General")

        modem=wx.Panel(nb)
        modemsizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
        modemsizer.AddGrowableCol(1)
        pppdconfl=wx.StaticText(modem,-1,"PPPD Config:")
        self.pppdchoices=get_ppp_confs()
        self.pppdchoices.append("--Do not start PPPD--")
        self.pppdconf=wx.Choice(modem,-1,choices=self.pppdchoices)
        selection=bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "pppd_config", "")
        self.pppdconf.SetSelection(self.pppdconf.FindString(selection))
        modemsizer.Add(pppdconfl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        modemsizer.Add(self.pppdconf, 0, wx.EXPAND)
        modemsizer.AddSpacer((1,1))
        pppdconft=wx.StaticText(modem,-1,"(Conf. files are in bbtether/conf/)")
        modemsizer.Add(pppdconft,0,0)
        pppdl=wx.StaticText(modem,-1,"PPPD path:")
        pppds=bb_prefs.get_def_string(bb_prefs.SECTION_MAIN, "pppd_path", "/usr/bin/pppd")
        self.pppd=wx.TextCtrl(modem,-1,pppds)
        modemsizer.Add(pppdl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        modemsizer.Add(self.pppd, 0, wx.EXPAND)
        datal=wx.StaticText(modem,-1,"Use Data Only Mode:")
        datas=bb_prefs.get_def_bool(bb_prefs.SECTION_MAIN, "data_mode", False)
        self.datamode=wx.CheckBox(modem,-1,"")
        self.datamode.SetValue(datas)
        modemsizer.Add(datal, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        modemsizer.Add(self.datamode, 0, wx.EXPAND)
        modem.SetSizer(modemsizer)
        nb.AddPage(modem,"Modem")

        usb=wx.Panel(nb)

        usbmainsizer=wx.BoxSizer(wx.VERTICAL)
        usbmainsizer.Add(wx.StaticText(usb,-1,"Leave blank, unless you know what you are doing."),0,0)
        usbmainsizer.Add(wx.StaticLine(usb,size=(-1,15)),0,wx.EXPAND)

        usbsizer=wx.FlexGridSizer(cols=2, hgap=5, vgap=5)
        usbsizer.AddGrowableCol(1)
        devicel=wx.StaticText(usb,-1,"Device:")
        devices=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "device", "")
        self.device=wx.TextCtrl(usb,-1,str(devices),size=(40,-1))
        usbsizer.Add(devicel, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        usbsizer.Add(self.device, 0, 0)
        interfacel=wx.StaticText(usb,-1,"Interface:")
        interfaces=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "interface", "")
        self.interface=wx.TextCtrl(usb,-1,str(interfaces),size=(40,-1))
        usbsizer.Add(interfacel, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        usbsizer.Add(self.interface, 0, 0)
        busl=wx.StaticText(usb,-1,"Bus:")
        buss=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "bus", "")
        self.bus=wx.TextCtrl(usb,-1,str(buss),size=(40,-1))
        usbsizer.Add(busl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        usbsizer.Add(self.bus, 0, 0)
        rpl=wx.StaticText(usb,-1,"Data read point(Ex: 0x83):")
        rps=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "readpt", "")
        self.rp=wx.TextCtrl(usb,-1,str(rps),size=(40,-1))
        usbsizer.Add(rpl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        usbsizer.Add(self.rp, 0, 0)
        wpl=wx.StaticText(usb,-1,"Data write point:(Ex: 0x4)")
        wps=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "writept", "")
        self.wp=wx.TextCtrl(usb,-1,str(wps),size=(40,-1))
        usbsizer.Add(wpl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        usbsizer.Add(self.wp, 0, 0)
        mrpl=wx.StaticText(usb,-1,"Modem read point:(Ex: 0x85)")
        mrps=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "modem_readpt", "")
        self.mrp=wx.TextCtrl(usb,-1,str(mrps),size=(40,-1))
        usbsizer.Add(mrpl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        usbsizer.Add(self.mrp, 0, 0)
        mwpl=wx.StaticText(usb,-1,"Modem write point:(Ex: 0x6)")
        mwps=bb_prefs.get_def_string(bb_prefs.SECTION_USER_EP, "modem_writept", "")
        self.mwp=wx.TextCtrl(usb,-1,str(mwps),size=(40,-1))
        usbsizer.Add(mwpl, 0, wx.ALIGN_RIGHT | wx.ALIGN_CENTER_VERTICAL)
        usbsizer.Add(self.mwp, 0, 0)

        usbmainsizer.Add(usbsizer,0,wx.EXPAND)
        usb.SetSizer(usbmainsizer)
        nb.AddPage(usb,"USB")

        bottomsizer=wx.BoxSizer(wx.HORIZONTAL)
        save=wx.Button(mainpanel,-1,"Save")
        cancel=wx.Button(mainpanel,-1,"Cancel")
        bottomsizer.Add(cancel,0)
        # expandable spacer in middle so i get a button on left and one on right
        bottomsizer.AddSpacer((1,1),wx.EXPAND)
        bottomsizer.Add(save,0,wx.ALIGN_RIGHT)

        mainsizer=wx.BoxSizer(wx.VERTICAL)
        mainsizer.Add(nb, 1, wx.EXPAND)
        mainsizer.Add(bottomsizer, 0, wx.EXPAND)
        mainpanel.SetSizer(mainsizer)
        mainsizer.SetSizeHints(self)
        self.CenterOnScreen()

        # Button events:
        cancel.Bind(wx.EVT_BUTTON, self.onQuit)
        save.Bind(wx.EVT_BUTTON, self.onSave)

    def onQuit(self,event):
        # Just hide it, so we don't need to rebuild it everytime
        self.Hide()

    def onSave(self,event):
        # Save, then hide
        # not very safe but at least no human readable
        passwd=base64.b64encode(self.password.GetValue())
        bb_prefs.set(bb_prefs.SECTION_MAIN,"password",passwd)
        bb_prefs.set(bb_prefs.SECTION_MAIN,"verbose",self.verbose.GetValue())
        bb_prefs.set(bb_prefs.SECTION_MAIN,"veryverbose",self.sverbose.GetValue())
        # Make those take effect right away
        bb_messenging.verbose=self.verbose.GetValue()
        bb_messenging.veryVerbose=self.sverbose.GetValue()
        selection=self.pppdchoices[self.pppdconf.GetSelection()]
        if selection == "--Do not start PPPD--":
            selection=""
        bb_prefs.set(bb_prefs.SECTION_MAIN,"pppd_config",selection)
        bb_prefs.set(bb_prefs.SECTION_MAIN,"pppd_path",self.pppd.GetValue())
        bb_prefs.set(bb_prefs.SECTION_MAIN,"data_mode",self.datamode.GetValue())
        bb_prefs.set(bb_prefs.SECTION_USER_EP,"device",self.device.GetValue())
        bb_prefs.set(bb_prefs.SECTION_USER_EP,"bus",self.bus.GetValue())
        bb_prefs.set(bb_prefs.SECTION_USER_EP,"interface",self.interface.GetValue())
        bb_prefs.set(bb_prefs.SECTION_USER_EP,"readpt",self.rp.GetValue())
        bb_prefs.set(bb_prefs.SECTION_USER_EP,"writept",self.wp.GetValue())
        bb_prefs.set(bb_prefs.SECTION_USER_EP,"modem_readpt",self.mrp.GetValue())
        bb_prefs.set(bb_prefs.SECTION_USER_EP,"modem_writept",self.mwp.GetValue())
        bb_prefs.save_prefs()
        self.Hide()

class SysOutListener:
    def write(self, msg):
        sys.__stdout__.write(msg)
        #and gui too
        evt = appendEvent(text=msg)
        wx.PostEvent(wx.GetApp().frame, evt)

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

        self.frame = BBFrame(None, -1, "Berry4All")
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

    def set_info(self, field, msg):
        evt = infoEvent(text=msg, field=field)
        wx.PostEvent(self.frame, evt)
    #End callback methods

    def set_answer(self,ans):
        self.answer=ans

    def wait_for_answer(self):
        self.answer=None
        while self.answer == None:
            time.sleep(.5)
        return self.answer
