
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
import os
import platform
import shutil
import subprocess

KEXT_FILE="bbtether.kext"
SECRETS_FILE="/etc/ppp/pap-secrets"

def is_osx():
    return platform.system().lower().startswith("darwin")


def is_supported_osx():
	'''
	We want at least 10.3 (Darwin 7.0), otherwise kextd cannot take SIGUP
    '''
	if not is_osx():
		return False
	rel=platform.release()
	major=int(rel[0 : rel.find(".")])
	return major > 7

def restart_kextd():
    # send SIGUP to kextd
    # Force kext cache update
    #touch /System/Library/Extensions
    print('Restarting Kernel Ext. Daemon')
    subprocess.call(['killall','-s','1','kextd'])

def install_kext():
    print('Installing cutom Kernel Ext. File')
    shutil.copy(KEXT_FILE, "/System/Library/Extensions")
    subprocess.call(['chown','root:wheel',KEXT_FILE])
    restart_kextd()

def uninstall_kextd():
    print('Removing cutom Kernel Ext. File')
    os.remove("/System/Library/Extensions/"+KEXT_FILE)
    restart_kextd()

def prepare_osx():
	if is_supported_osx():
        # Note: pocketmac kext preventing us from working !:
        # net.pocketmac.driver.BlackberryUSB -> unload it ??

		# won't manage to do pap without this
		if not os.path.isfile(SECRETS_FILE):
			try:
				print "the file "+SECRETS_FILE+" does not exist, will try to create it(required)"
				file = open(SECRETS_FILE, 'w')
				file.write("*	*	\"\"	*")
				file.close()
				os.chown(SECRETS_FILE,0,0)#root:wheel
				os.chmod(SECRETS_FILE,600)#might contain passwords .. some day
			except:
				print "Could not create the file:"+SECRETS_FILE+" run me as root !"
				print "Ex: sudo python bbtether.py ....."
				os._exit(0)
				