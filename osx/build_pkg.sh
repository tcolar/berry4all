#!/bin/sh
#http://developer.apple.com/DOCUMENTATION/DeveloperTools/Conceptual/PackageMakerUserGuide/Overview/Overview.html
#http://s.sudre.free.fr/Stuff/PackageMaker_Howto.html

PACKAGE=pyusb-0.41
ROOT=PACK_ROOT

#copy precompiled pyusb in resources folder
#sudo cp -rf pyusb-0.4.1 Resources

rm -rf ${PACKAGE}-*.pkg
rm ${PACKAGE}-*.pkg.*
find ${ROOT} -name .DS_Store -delete
find Resources-tiger -name .DS_Store -delete
find Resources-leopard -name .DS_Store -delete
/Developer/Tools/packagemaker -build -f ${ROOT} -p ${PACKAGE}-tiger.pkg -i Info.plist \
	-d Description.plist -r Resources-tiger -ds -v
/Developer/Tools/packagemaker -build -f ${ROOT} -p ${PACKAGE}-leopard.pkg -i Info.plist \
	-d Description.plist -r Resources-leopard -ds -v

#chown -R root:wheel ${PACKAGE}.pkg
tar -h ${PACKAGE}-tiger.pkg -cf ${PACKAGE}-tiger.pkg.tar
gzip -9 ${PACKAGE}-tiger.pkg.tar
tar -h ${PACKAGE}-leopard.pkg -cf ${PACKAGE}-leopard.pkg.tar
gzip -9 ${PACKAGE}-leopard.pkg.tar

#test: sudo /usr/sbin/installer -verbose -pkg pyusb-0.41.pkg -target /


