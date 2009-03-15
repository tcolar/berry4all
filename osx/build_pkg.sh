#!/bin/sh
#http://developer.apple.com/DOCUMENTATION/DeveloperTools/Conceptual/PackageMakerUserGuide/Overview/Overview.html
#http://s.sudre.free.fr/Stuff/PackageMaker_Howto.html

PACKAGE=pyusb-0.41
ROOT=PACK_ROOT

#copy precompiled pyusb in resources folder
#sudo cp -rf pyusb-0.4.1 Resources

rm -rf ${PACKAGE}.pkg
rm ${PACKAGE}.pkg.*
find ${ROOT} -name .DS_Store -delete
find Resources -name .DS_Store -delete
/Developer/Tools/packagemaker -build -f ${ROOT} -p ${PACKAGE}.pkg -i Info.plist \
	-d Description.plist -r Resources -ds -v

#chown -R root:wheel ${PACKAGE}.pkg
tar -h ${PACKAGE}.pkg -cf ${PACKAGE}.pkg.tar
gzip -9 ${PACKAGE}.pkg.tar

#test: sudo /usr/sbin/installer -verbose -pkg pyusb-0.41.pkg -target /


