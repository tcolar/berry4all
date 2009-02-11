
'''
OSX specific stuff

For Mac:
http://www.apcupsd.org/manual/USB_Configuration.html
http://statistics.roma2.infn.it/~sestito/g15mac/HOWTO-Logitech_G15_and_Mac_OS_X.html
http://developer.apple.com/qa/qa2001/qa1076.html
http://developer.apple.com/qa/qa2001/qa1319.html#//apple_ref/doc/uid/DTS10002355

Mac:
- Unplug berry
- start bbtether
* install kext file and restart kextd
* ask user to plug berry ?
* when done, unplug berry, remove kext file. retsart kextd

Thibaut Colar
'''
import subprocess
import platform
import shutil

KEXT_FILE="bbtether.kext"

def is_osx():
    return platform.system().lower().startswith("darwin")


def is_supported_osx():
    '''
    We want at least 10.3 (Darwin 7.0), otherwise kextd cannot take SIGUP
    '''
    rel=platform.release()
    major=int(rel[0 : rel.indexof(".")])
    return is_osx() and major > 7

def restart_kextd():
    # send SIGUP to kextd
    print('Restarting Kernel Ext. Daemon')
    subprocess.call(['killall','-s','1','kextd'])

def install_kext():
    print('Installing cutom Kernel Ext. File')
    shutil.copy(KEXT_FILE, "/System/Library/Extensions")
    subprocess.call(['chmod','-R','root:wheel',KEXT_FILE])
    restart_kextd()

def uninstall_kextd():
    print('Removing cutom Kernel Ext. File')
    os.remove("/System/Library/Extensions/"+KEXT_FILE)
    restart_kextd()

    