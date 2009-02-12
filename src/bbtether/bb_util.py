'''
Utilities
Thibaut Colar
'''
import re
import subprocess
from subprocess import PIPE
# might be set/get from other module
verbose=False

def debug(msg):
	if verbose :
		print msg
	
def debug_bytes(tuple, msg):
	'''Get a tuple of bytes and print it as lines of 16 digits (hex and ascii)'''
	text=""
	hexa=""
	cpt=0
	for t in tuple:
		#print t
		hexa+=hex(t)+" "
		if(t>=32 and t<=126):
			text+=chr(t)
		else:
			text+="."
		cpt+=1
		if(cpt%16==0 or cpt==len(tuple)):
			debug(msg+"["+hexa+"] ["+text+"]")
			text=""
			hexa=""

def end_with_tuple(tuple1,tuple2):
	'''check if tuple1 ends with tuple2'''
	if tuple1==None and tuple2==None:
		return True
	if (tuple1==None and tuple2 != None) or (tuple1!=None and tuple2==None):
		return False
	if len(tuple1) < len(tuple2):
		return False
	return is_same_tuple(tuple1[len(tuple1)-len(tuple2):len(tuple1)],tuple2)

def is_same_tuple(tuple1,tuple2):
	'''Compare 2 tuples of Bytes, return True if Same(same data)'''
	if tuple1==None and tuple2==None:
		return True
	if (tuple1==None and tuple2 != None) or (tuple1!=None and tuple2==None):
		return False
	if len(tuple1) != len(tuple2):
		return False
	# !None and same length ... compare
	length=len(tuple1)
	for i in range(length):
		if tuple1[i] != tuple2[i]:
			return False	
	return True

def debug_object_attr(obj):
	attributes=dir(obj)
	for a in attributes:
		print a

def module_loaded(mod):
	output = subprocess.Popen(["lsmod"], stdout=PIPE).communicate()[0]
	if verbose:
		print "###### Modules: ######"
		print output
		print "######################\n"
	return re.search(mod,output) != None

def unload_module(mod):
	subprocess.call(["rmmod",mod])