#!/bin/bash

os=`uname`
if [[ "$os" == 'Darwin' ]]
then
	echo "OSX: Will force use of 32 bits python as it's needed for 32 bits wxpython to work."
	export VERSIONER_PYTHON_PREFER_32_BIT=yes
fi

python bbgui.py
