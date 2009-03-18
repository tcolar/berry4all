'''
Deal with user preferences / settings
Thibaut Colar
'''
import ConfigParser
from ConfigParser import NoSectionError, NoOptionError
import bb_messenging
import bb_osx
import os

SECTION_MAIN="Global"
SECTION_EP="EndPoints"

PREF_FILE=os.environ['HOME']+"/.bbtether.conf"
if bb_osx.is_osx():
	PREF_FILE=os.environ['HOME']+"/library/Preferences/bbtether.conf"

my_config=None

def get_def_string(section, option, default):
	config=get_prefs()
	try:
		return config.getstring(section, option)
	except NoSectionError:
		return default
	except NoOptionError:
		return default

def get_def_bool(section, option, default):
	config=get_prefs()
	try:
		return config.getboolean(section, option)
	except NoSectionError:
		return default
	except NoOptionError:
		return default

def get_def_int(section, option, default):
	config=get_prefs()
	try:
		return config.getint(section, option)
	except NoSectionError:
		return default
	except NoOptionError:
		return default

def read_prefs():
	config = ConfigParser.RawConfigParser()
	if os.path.isfile(PREF_FILE):
		bb_messenging.log("Reading prefs from "+PREF_FILE)
		config.read(PREF_FILE)
	else:
		bb_messenging.log("Creating initial config file "+PREF_FILE)
		config.add_section(SECTION_MAIN)
		config.set(SECTION_MAIN, "verbose", "True")
		# do not use save() to prevent recursion
		configfile=open(PREF_FILE, 'wb')
		config.write(configfile)
		configfile.close()
	return config

def get_prefs():
	global my_config
	if my_config==None:
		my_config=read_prefs()
	return my_config

def save_prefs(prefs):
	global my_config
	my_config=prefs
	configfile=open(PREF_FILE, 'wb')
	my_config.write(configfile)
	configfile.close()

