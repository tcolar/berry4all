'''
Deal with user messages
send mesages/questions to gui (if avail) / or console
'''

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
	for msg in msgs:
		print msg

def ask(msgs):
	'''
	Do a warning and wait for OK (or keystroke in cmdline mode)
	'''
	if gui != None:
		warn(msgs)
	else:
		raw_input(msg+ "then press Enter")

def status(msg):
	'''
	Update status bar and print in logs
	'''
	print msg