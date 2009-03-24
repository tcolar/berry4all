'''
Deal with user messages
send mesages/questions to gui (if avail) / or console
'''
import sys

verbose=False
veryVerbose=False

# None, unless bbgui sets it
gui=None

def debug2(msg):
	global veryVerbose
	if veryVerbose:
		log(msg)

def debug(msg):
	global veryVerbose, verbose
	if verbose or veryVerbose:
		log(msg)

def log(msg):
	'''
	print in log
	'''
	print msg

def warn(msgs, waitFor=False):
	global gui
	'''
	Show a warning popup and print in log
	'''
	if gui != None:
		gui.warn(msgs, waitFor)
	for msg in msgs:
		sys.__stdout__.write(msg+"\n")

def ask(caption,hide=False,default=""):
	global gui
	'''
	Show a warning popup and print in log
	'''
	if gui != None:
		return gui.ask(caption,hide,default)
	else:
		return raw_input("Question: "+caption+"\n")

def confirm(msgs):
	global gui
	'''
	Do a warning and wait for OK (or keystroke in cmdline mode)
	'''
	warn(msgs, True)
	if gui==None:
		raw_input("Press Enter to continue")

def status(msg):
	global gui
	'''
	Update status bar and print in logs
	'''
	if gui != None:
		gui.update_status(msg)
		gui.append_log(msg)
	sys.__stdout__.write(msg+"\n")
