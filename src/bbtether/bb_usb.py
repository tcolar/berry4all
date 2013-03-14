'''
USB utilities for Blackberry
Thibaut Colar
'''
import bb_modem
from bb_prefs import SECTION_SCANNED_EP
import sys

import bb_data
import bb_messenging
import bb_osx
import bb_prefs
from bb_prefs import PREF_FILE
import bb_util
import string
import traceback
import usb

TIMEOUT=1000
BUF_SIZE=25000
VENDOR_RIM=0x0fca
PRODUCT_DATA=0x0001   #(serial data)
PRODUCT_NEW_DUAL=0x0004   #(mass storage & data)
PRODUCT_NEW_8120=0x8004   #(Pearl 8120)
PRODUCT_NEW_MASS_ONLY=0x0006   #(mass storage only)
BERRY_CONFIG=1
COMMAND_MODE_DESKTOP= [0,0,0x18,0,7,0xFF,0,7,0x52,0x49,0x4D,0x20,0x44,0x65,0x73,0x6B,0x74,0x6F,0x70,0,0,0,0,0]
COMMAND_PIN = [0x00,0x00,0x0c,0x00,0x05,0xff,0x00,0x00,0x00,0x00,0x04,0x00]
COMMAND_DESC= [0x00,0x00,0x0c,0x00,0x05,0xff,0x00,0x00,0x00,0x00,0x02,0x00]
COMMAND_HELLO = [0x00, 0x00, 0x10, 0x00, 0x01, 0xff, 0x00, 0x00,0xa8, 0x18, 0xda, 0x8d, 0x6c, 0x02, 0x00, 0x00]
MODEM_HELLO_REPLY = [0x7, 0x0, 0x0, 0x0, 0xc, 0x0, 0x0, 0x0, 0x78, 0x56, 0x34, 0x12 ]
COMMAND_PASS_START_CHLG = [0,0,8,0,0xA,4,1,8]
COMMAND_MODE_APP_LOADER = [0,0,0x18,0,7,0xff,00,0x0B,0x52,0x49,0x4d,0x5f,0x4a,0x61,0x76,0x61,0x4c,0x6f,0x61,0x64,0x65,0x72,0x00,0x00]

def find_berry(userdev=None, userbus=None, verbose=True):
    '''
        Look on Bus for a RIM device
        (1 max for now)
        userdev,userbus : potential user provided device/bus to force-use
    '''
    device=None
    mybus=None

    bb_messenging.status("Looking for USB devices:")
    berry=None
    if userdev and userbus:
        if verbose :
            bb_messenging.log("Will use user provided bus/device: "+userbus+"/"+userdev)
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
                    bb_messenging.log("    Bus %s Device %s: ID %04x:%04x" % (bus.dirname,dev.filename,dev.idVendor,dev.idProduct))
                if(dev.idVendor==VENDOR_RIM):
                    berry=dev
                    mybus=bus

    if berry != None:
        device=bb_data.Device()
        device.usbdev=berry
        device.bus=mybus

    if verbose :
        bb_messenging.log("USB Device lookup finished")

    return device

def read_bb_endpoints(device, userInterface):
    '''
    Read the device endpoints and stores them in the device data structure
    device was created from find_berry
    and device.open_handle should have been called already
    Once we found endpoints, we save them as some devices (esp. Bold) don't like being probed.
    '''

    #look for previously saved endpoints
    config=bb_prefs.get_prefs()
    if config.has_section(SECTION_SCANNED_EP):
        device.interface=bb_prefs.get_def_int(bb_prefs.SECTION_SCANNED_EP,'interface',-1)
        device.readpt=bb_prefs.get_def_int(bb_prefs.SECTION_SCANNED_EP,'readpt',-1)
        device.writept=bb_prefs.get_def_int(bb_prefs.SECTION_SCANNED_EP,'writept',-1)
        device.modem_readpt=bb_prefs.get_def_int(bb_prefs.SECTION_SCANNED_EP,'modem_readpt',-1)
        device.modem_writept=bb_prefs.get_def_int(bb_prefs.SECTION_SCANNED_EP,'modem_writept',-1)
        bb_messenging.log("Using saved EP data: "+str(device.interface)+", "+str(device.readpt)+", "+str(device.writept)+", "+str(device.modem_readpt)+", "+str(device.modem_writept))
        # return saved data if good (earlier version saved bad ones)
        if device.readpt != -1 and device.writept !=-1:
            return device
        else:
            print "Invalid saved endpoints, will rescan."

    readpt=-1
    writept=-1
    modem_readpt=-1
    modem_writept=-1
    next_readpt=-1
    next_writept=-1
    # List device Infos for information and find USB endpair
    handle=device.handle
    berry=device.usbdev
    config=berry.configurations[0]
    type=""
    if(berry.idProduct == PRODUCT_DATA):
        type="Data Mode"
    if(berry.idProduct == PRODUCT_NEW_DUAL):
        type="Dual Mode"
    if(berry.idProduct == PRODUCT_NEW_8120):
        type="8120"
    if(berry.idProduct == PRODUCT_NEW_MASS_ONLY):
        type="Storage Mode"

    bb_messenging.log("\nFound RIM device ("+type+")")
    bb_messenging.log("    Manufacturer:"+handle.getString(berry.iManufacturer,100))
    bb_messenging.log("    Product:"+handle.getString(berry.iProduct,100))
    #print "    Serial:",handle.getString(berry.iSerialNumber,100)
    bb_messenging.log("    Device:"+berry.filename)
    bb_messenging.log("    VendorId: %04x" % berry.idVendor)
    bb_messenging.log("    ProductId: %04x" % berry.idProduct)
    bb_messenging.log("    Version:"+berry.deviceVersion)
    bb_messenging.log("    Class:"+str(berry.deviceClass)+" "+str(berry.deviceSubClass))
    bb_messenging.log("    Protocol:"+str(berry.deviceProtocol))
    bb_messenging.log("    Max packet size:"+str(berry.maxPacketSize))
    bb_messenging.log("    Self Powered:"+str(config.selfPowered))
    bb_messenging.log("    Max Power:"+str(config.maxPower))
    for inter in config.interfaces:
        if len(inter) == 0:
            bb_messenging.log("Skipping Interface -> empty array !")
            continue
        bb_messenging.log("\n    *Interface:"+str(inter[0].interfaceNumber))
        if userInterface!=None and int(userInterface)!=inter[0].interfaceNumber:
            bb_messenging.log("Skipping interface (-i flag used)")
            continue
        if readpt != -1:
            bb_messenging.log("Skipping interface (valid endpoints already found), use -i flag to force")
            continue
        try:
            try:
                handle.claimInterface(inter[0].interfaceNumber)
            except usb.USBError, error:
                bb_messenging.log("Failed to claim interface: "+str(error)+"\nMust be in use.")
                if not bb_osx.is_osx():
                    #Only implemented on libusb Linux !
                    #For mac we need the kext stuff.
                    bb_messenging.log("Will try to release it.")
                    detach_kernel_driver(device,inter[0].interfaceNumber)
                    try:
                        handle.claimInterface(inter[0].interfaceNumber)
                        bb_messenging.log("Interface is now claimed !")
                    except usb.USBError, error:
                        bb_messenging.log("Still could not claim the interface: "+str(error))

            bb_messenging.log("        Interface class:"+str(inter[0].interfaceClass)+"/"+str(inter[0].interfaceSubClass))
            bb_messenging.log("        Interface protocol:"+str(inter[0].interfaceProtocol))
            for att in inter:
                i=0
                # check endpoint pairs
                while i < len(att.endpoints):
                    isDataPair=False
                    red=att.endpoints[i].address
                    writ=att.endpoints[i+1].address
                    i+=2
                    bb_messenging.log("        EndPoint Pair:"+hex(red)+"/"+hex(writ))
                    try:
                        usb_write(device,writ,COMMAND_HELLO)
                        try:
                            bytes=usb_read(device,red)
                            if len(bytes) == 0:
                                raise usb.USBError
                            # on some devices, the modem replies to hello with (others, read fails):
                            # [0x7 0x0 0x0 0x0 0xc 0x0 0x0 0x0 0x78 0x56 0x34 0x12 ] [........xV4.]
                            if bb_util.is_same_tuple(bytes, MODEM_HELLO_REPLY):
                                if modem_readpt==-1:
                                    modem_readpt=red
                                    modem_writept=writ
                                    bb_messenging.log("            Found Modem endpoints: "+hex(red)+"/"+hex(writ))

                            else:
                                if readpt == -1 :
                                    # Use first valid data point found
                                    device.interface=inter[0].interfaceNumber
                                    bb_util.debug("Setting interface to: "+str(device.interface))
                                    readpt=red
                                    writept=writ
                                    isDataPair=True
                                    bb_messenging.log("            Found Data endpoints: "+hex(red)+"/"+hex(writ))
                        except usb.USBError:
                            bb_messenging.log("            Not Data Pair (Read failed)")
                    except usb.USBError:
                        bb_messenging.log("            Not Data Pair (Write failed)")

                    if (isDataPair==False) and readpt != -1 and next_readpt == -1:
                        next_readpt=red
                        next_writept=writ
                        bb_messenging.log("            Next endpoints:"+hex(red)+"/"+hex(writ))

            handle.releaseInterface()
        except usb.USBError:
            bb_messenging.log("Error while scanning interface: "+str(inter[0].interfaceNumber)+" -> skipping")
            traceback.print_exc(file=sys.stdout)

    # if no specific modem port found, try the one after the data one
    if modem_readpt==-1:
        modem_readpt=next_readpt
        modem_writept=next_writept
        bb_messenging.log("Defaulted Modem endpoints: "+hex(modem_readpt)+"/"+hex(modem_writept))

    device.readpt=readpt
    device.writept=writept
    device.modem_readpt=modem_readpt
    device.modem_writept=modem_writept

    #save scan results to file (only if found)
    if writept!=-1:
        bb_messenging.log("***********************************************")
        msgs=["We just ran the initial device Endpoints Scan",
        "This needs to be done only once.",
        "Saved the scan results to "+PREF_FILE,
        "some devices (Bold) do not like being scanned.","",
        "AND WILL NEED THE BATTERY REMOVED / ADDED (just this once)",
        "BEFORE YOU CAN USE THE MODEM."]
        bb_messenging.warn(msgs)
        bb_messenging.log("***********************************************")
        bb_prefs.set(bb_prefs.SECTION_SCANNED_EP,'interface', device.interface)
        bb_prefs.set(bb_prefs.SECTION_SCANNED_EP,'readpt', device.readpt)
        bb_prefs.set(bb_prefs.SECTION_SCANNED_EP,'writept', device.writept)
        bb_prefs.set(bb_prefs.SECTION_SCANNED_EP,'modem_readpt', device.modem_readpt)
        bb_prefs.set(bb_prefs.SECTION_SCANNED_EP,'modem_writept', device.modem_writept)
        bb_messenging.log("Saving EP data: "+str(device.interface)+", "+str(device.readpt)+", "+str(device.writept)+", "+str(device.modem_readpt)+", "+str(device.modem_writept))
        bb_prefs.save_prefs()

def clear_halt(device, endpt):
    device.handle.clearHalt(endpt)

def set_bb_power(device):
    '''
    Added try / expect blocks as I had reports of failure(which ?) on storm 9500
    '''
    bb_messenging.status("Increasing USB power - for charging")
    try:
        buffer= [0,0]
        device.handle.controlMsg(0xc0, 0xa5, buffer, 0 , 1)
        buffer = []
        device.handle.controlMsg(0x40, 0xA2, buffer, 0 , 1)
        # reset
        # reset()
        bb_messenging.status("Increased USB power")
    except usb.USBError, error:
        bb_messenging.log("Error increasing power "+str(error)+", continuing anyway.")

def set_mode(device, command, password=''):
    # TODO: always try ? but only send password if requested ?
    bb_messenging.status("Switching Device to Desktop mode")
    usb_write(device, device.writept, command)
    data=usb_read(device,device.readpt)
    if len(password) > 0:
        bb_messenging.log("Trying to send Desktop mode password.")
        usb_write(device, device.writept, COMMAND_PASS_START_CHLG)
        data=usb_read(device,device.readpt)
        if len(data) <=8 :
            bb_messenging.log("No seed sent by device, probably doesn't need a password.")
        else:
            seed=data[9:]
            digest=bb_modem.digest_password(seed,password)
            print("digest: "+str(digest))

def set_data_mode(device):
    bb_messenging.status("Switching Device to data only mode")
    try:
        buffer= [0,0]
        device.handle.controlMsg(0xc0, 0xa9, buffer, 0 , 1)
    except usb.USBError, error:
        bb_messenging.log("Error setting device to data mode "+str(error)+", continuing anyway.")

def reset(device):
    bb_messenging.status("Resetting device")
    device.handle.reset()
    bb_messenging.status("Reset sent.")

def get_pin(device):
    pin=0x0
    usb_write(device, device.writept, COMMAND_PIN)
    data=usb_read(device,device.readpt)
    if len(data)>0 and data[4] == 0x6 and data[10] == 4:
        pin=bb_data.readlong(data,16)
    return pin

def get_description(device):
    desc="N/A"
    usb_write(device, device.writept, COMMAND_DESC)
    data=usb_read(device,device.readpt)
    if len(data)>0 and data[4] == 0x6 and data[10] == 2:
        desc=bb_data.readstring(data,28)
    return desc

def usb_write(device,endpt,bytes,timeout=TIMEOUT,msg="\t-> "):
    bb_util.debug_bytes(bytes,msg)
    try:
        bb_util.debug2(">bulkwrite")
        device.handle.bulkWrite(endpt, bytes, timeout)
        bb_util.debug2("<bulkwrite")
    except usb.USBError, error:
        bb_util.debug2("<bulkwrite(exc)")
        # ! osx returns an empty error (no errorno) so we justcan't check anything :-(
        if str(error) != "No error" and not (bb_osx.is_osx() and error.errno == None):
            bb_messenging.log("error: "+str(error))
            raise

def usb_read(device,endpt,size=BUF_SIZE,timeout=TIMEOUT,msg="\t<- "):
    bytes=[]
    try:
        bb_util.debug2(">bulkread")
        bytes=device.handle.bulkRead(endpt, size, timeout)
        bb_util.debug2("<bulkread")
        bb_util.debug_bytes(bytes,msg)
    except usb.USBError, error:
        bb_util.debug2("<bulkread (exc)")
        # ! osx returns an empty error (no errorno) so we justcan't check anything :-(
        if str(error) != "No error" and not (bb_osx.is_osx() and error.errno == None):
            bb_messenging.log("error: "+str(error))
            raise
    return bytes

def detach_kernel_driver(device,interface):
    print "Trying to detach interface driver"
    code=device.handle.detachKernelDriver(interface)
    print "Detaching driver returned: "+str(code)
