'''
Deal with user messages
send mesages/questions to gui (if avail) / or console
'''
import sys
# None, unless bbgui sets it
gui=None

def log(msg):
	'''
	print in log
	'''
	print msg

def warn(msgs):
	'''
	Show a warning popup and print in log
	'''
	if gui != None:
		gui.warn(msgs)
	for msg in msgs:
		sys.__stdout__.write(msg+"\n")


def confirm(msgs):
	'''
	Do a warning and wait for OK (or keystroke in cmdline mode)
	'''
	warn(msgs)
	if gui==None:
		raw_input("Press Enter to continue")

def status(msg):
	'''
	Update status bar and print in logs
	'''
	if gui != None:
		gui.update_status(msg)
		gui.append_log(msg)
	sys.__stdout__.write(msg+"\n")
