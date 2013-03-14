
'''
OSX specific stuff

** For Mac:
http://www.apcupsd.org/manual/USB_Configuration.html
http://statistics.roma2.infn.it/~sestito/g15mac/HOWTO-Logitech_G15_and_Mac_OS_X.html
http://developer.apple.com/qa/qa2001/qa1076.html
http://developer.apple.com/qa/qa2001/qa1319.html#//apple_ref/doc/uid/DTS10002355

** More mac pppd stuff to explore:
sudo kextload /System/Library/Extensions/PPTP.ppp/Contents/PlugIns/PPTP.kext
http://fedward.org/GC82/pppd.html
http://forums.macosxhints.com/showthread.php?t=4415
pppconfig
route, etc...: http://blog.liip.ch/archive/2006/01/07/changing-default-routes-on-os-x-on-vpn.html
http://njr.sabi.net/2005/08/04/overriding-dns-for-domains-in-os-x-tiger/ (scutil --dns)
https://macosx.com/forums/mac-os-x-system-mac-software/5620-ppp-server-setup-os-x.html
http://mpd.sourceforge.net/
http://www.afp548.com/articles/Jaguar/vpnd.html

Mac:
- Unplug berry
- start bbtether
* install kext file and restart kextd
* ask user to plug berry ?
* when done, unplug berry, remove kext file. retsart kextd

Thibaut Colar
'''
import bb_messenging
import os
import platform
import shutil
import subprocess

KEXT_FOLDER = "osx/libusbshield_rim.kext"
SECRETS_FILE = "/etc/ppp/pap-secrets"

def is_osx():
    return platform.system().lower().startswith("darwin")

def is_supported_osx():
    '''
    We want at least 10.3 (Darwin 7.0), otherwise kextd cannot take SIGUP
    '''
    if not is_osx():
        return False
    rel = platform.release()
    major = int(rel[0: rel.find(".")])
    return major > 7

def restart_kextd():
    bb_messenging.status('Restarting Kernel Ext. Daemon')
    # Force kext cache update
    subprocess.call(['touch', '/System/Library/Extensions'])
    # send SIGUP to kextd
    subprocess.call(['killall', '-s', '1', 'kextd'])

def install_kextd():
    bb_messenging.status('Installing custom Kernel Ext. File')
    shutil.copytree(KEXT_FOLDER, "/System/Library/Extensions/libusbshield_rim.kext")
    subprocess.call(['chown', 'root:wheel', "/System/Library/Extensions/libusbshield_rim.kext"])
    subprocess.call(['kextload', '/System/Library/Extensions/libusbshield_rim.kext'])
    restart_kextd()
    bb_messenging.log("Warning: If the device still fails to claim an interface, you should reboot.")

def uninstall_kextd():
    bb_messenging.status('Removing cutom Kernel Ext. File')
    os.remove("/System/Library/Extensions/libusbshield_rim.kext")
    restart_kextd()

def load_pocketmac():
    bb_messenging.status('Re-enabling pocketmac')
    subprocess.call(['kextload', '/System/Library/Extensions/net.pocketmac.driver.BlackberryUSB.kext/'])
    restart_kextd()

def unload_pocketmac():
    bb_messenging.status('Temporarely disabling pocketmac kernel extension (imcopatible)')
    subprocess.call(['kextunload', '/System/Library/Extensions/net.pocketmac.driver.BlackberryUSB.kext/'])
    restart_kextd()

def prepare_osx():
    if is_supported_osx():
        if os.getuid() == 0:
            # Note: pocketmac kext preventing us from working !:
            if os.path.isdir("/System/Library/Extensions/net.pocketmac.driver.BlackberryUSB.kext"):
                msgs=["PocketMac found, it's incompatible with bbtether, will try to disbale it temporarily",
                "*** Note that this might not work, you might have to uninstall pocketMac :-( **"]
                bb_messenging.warn(msgs)
                bb_messenging.confirm(["Please Unplug the Blackberry"])
                unload_pocketmac()
                bb_messenging.confirm(["Please Plug the Blackberry"])
            #if not os.path.isdir("/System/Library/Extensions/libusbshield_rim.kext/"):
            #    raw_input("Please Unplug the Blackberry, then press Enter")
            #    install_kextd()
            #    raw_input("Please Plug the Blackberry, then press Enter")
        else:
            if os.path.isdir("/System/Library/Extensions/net.pocketmac.driver.BlackberryUSB.kext"):
                bb_messenging.warn(["Need to run as root(sudo) to disable net.pocketmac.driver.BlackberryUSB.kext"])
                os._exit(0)
            #if not os.path.isdir("/System/Library/Extensions/libusbshield_rim.kext/"):
            #    print "Need to run as root(sudo) to install libusbshield_rim.kext (just once)"
            #    os._exit(0)
            if not os.path.isfile(SECRETS_FILE):
                bb_messenging.warn(["Need to run as root(sudo) to install " + SECRETS_FILE + " (just once)"])
                os._exit(0)


        # won't manage to do pap without this
        if not os.path.isfile(SECRETS_FILE):
            bb_messenging.warn(["the file " + SECRETS_FILE + " does not exist, will try to create it(required)"])
            file = open(SECRETS_FILE, 'w')
            file.write("*    *    \"\"    *")
            file.close()
            os.chown(SECRETS_FILE, 0, 0)#root:wheel
            os.chmod(SECRETS_FILE, 600)#might contain passwords .. some day

def terminate_osx():
    if is_supported_osx():
        if os.getuid() == 0:
            if os.path.isdir("/System/Library/Extensions/net.pocketmac.driver.BlackberryUSB.kext"):
                load_pocketmac()
            # we will leave it unless somebody complains
                #uninstall_kextd()
                    # instead
                restart_kextd()

