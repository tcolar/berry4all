#!/usr/bin/python
'''
Thibaut Colar
ffff8101f45dcd80 2246019859 C Bi:4:013:9 0 8 = 00800000 00000200
'''
import sys
import re
import string

def hex_to_ascii(hex):
	text=''
	for i in range(len(hex)/2):
		nb=int(hex[i*2:i*2+2], 16)
		if(nb >= 32 and nb <= 126):
			text += chr(nb)
		else:
			text += "."
	return text

#main
filein=sys.argv[1]
file = open(filein,"r")
lines = file.readlines()

pattern = re.compile('(\S+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(-?\d+)\s+(\d+)\s+=\s+(.*)')

timeref=0
for line in lines:
	m = pattern.match(line)
	if m:
		[a,time,b,c,d,length,data] = m.groups()
		if timeref == 0:
			timeref=string.atoi(time)
		time_offset=string.atoi(time)-timeref
		# FIXME
		nice_time=""+str(time_offset/60000000)+":"+str(time_offset%60000000/1000000)+"."+str(time_offset%100000/1000)
		data_nospaces=''.join(data.split())
		print a,nice_time,b,c,d,length,data,"["+hex_to_ascii(data_nospaces)+"]"
	else:
		print "**",line,

