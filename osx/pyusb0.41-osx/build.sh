#!/bin/sh

DSTNAME=pyusb
DSTVERSION=0.1.13

MACOSX_DEPLOYMENT_TARGET=$1

if   [ "$MACOSX_DEPLOYMENT_TARGET" = "10.3" ]; then
    SDKVERSION=10.3.9
    ARCHS="ppc"
elif [ "$MACOSX_DEPLOYMENT_TARGET" = "10.4" ]; then
    SDKVERSION=10.4u
    ARCHS="ppc i386"
elif [ "$MACOSX_DEPLOYMENT_TARGET" = "10.5" ]; then
    SDKVERSION=10.5
    ARCHS="ppc7400 ppc64 i386 x86_64"
else
    MACOSX_DEPLOYMENT_TARGET=default
    echo "Warning: No valid Deployment Target specified."
    echo "         Possible targets are: 10.3, 10.4 and 10.5"
    echo "         The software will be built for the MacOSX version and"
    echo "         architecture currently running."
    echo "         No SDK package will be built."
fi

[ -n "$SDKVERSION" ] && NEXT_ROOT=/Developer/SDKs/MacOSX${SDKVERSION}.sdk

if [ -n "$NEXT_ROOT" ] && [ ! -e "$NEXT_ROOT" ]; then
    echo "Error: SDK build requested, but SDK build not installed."
    exit 1
fi

SRCDIR=`pwd`/src
BUILD=/tmp/$DSTNAME.build
DSTROOT=/tmp/$DSTNAME.dst

[ -e $BUILD ]   && (      rm -rf $BUILD   || exit 1 )
[ -e $DSTROOT ] && ( sudo rm -rf $DSTROOT || exit 1 )

for d in $DSTROOT-* ; do ( rm -rf $d || exit 1 ) ; done

mkdir $BUILD

(
    cd $BUILD
    tar -z -x -f $SRCDIR/$DSTNAME-2009-02-22-svn.tar.gz

    cd $DSTNAME-2009-02-22-svn

    patch -p1 < $SRCDIR/libusb-64bit.patch
    patch -p1 < $SRCDIR/libusb-endian.patch
    patch -p1 < $SRCDIR/libusb-runloop.patch

    aclocal
    glibtoolize --force
    autoheader
    automake --add-missing --force
    autoconf

    # fix for "rm: conftest.dSYM: is a directory"
    sed -e 's/rm -f conftest\*/rm -rf conftest\*/' \
	-e 's/\$rm conftest\*/\$rm -r conftest\*/' < configure > configure.new
    mv -f configure.new configure
    chmod +x configure

    if   [ "$MACOSX_DEPLOYMENT_TARGET" = "10.3" ]; then
	CC="/usr/bin/gcc-3.3 -B$NEXT_ROOT/usr/lib/gcc/darwin/3.3"
	CFLAGS="-I$NEXT_ROOT/usr/include/gcc/darwin/3.3 -isystem $NEXT_ROOT/usr/include -F$NEXT_ROOT/System/Library/Frameworks"
	CXX="/usr/bin/g++-3.3 -B$NEXT_ROOT/usr/lib/gcc/darwin/3.3"
	CXXFLAGS="-I$NEXT_ROOT/usr/include/gcc/darwin/3.3/c++ -I$NEXT_ROOT/usr/include/gcc/darwin/3.3/c++/ppc-darwin $CFLAGS"
	CPP="/usr/bin/cpp-3.3"
	CPPFLAGS="$CFLAGS"
	LDFLAGS="-L$NEXT_ROOT/usr/lib -Wl,-F$NEXT_ROOT/System/Library/Frameworks"
    elif [ "$MACOSX_DEPLOYMENT_TARGET" = "10.4" ]; then
	CC="/usr/bin/gcc-4.0 -isysroot $NEXT_ROOT"
	CXX="/usr/bin/g++-4.0 -isysroot $NEXT_ROOT"
	CPP="/usr/bin/cpp-4.0"
    elif [ "$MACOSX_DEPLOYMENT_TARGET" = "10.5" ]; then
	CC="/usr/bin/gcc-4.0 -isysroot $NEXT_ROOT"
	CXX="/usr/bin/g++-4.0 -isysroot $NEXT_ROOT"
	CPP="/usr/bin/cpp-4.0"
    fi

    LDFLAGS="$LDFLAGS -no-undefined"

    if [ -n "$SDKVERSION" ]; then
	export MACOSX_DEPLOYMENT_TARGET
	export NEXT_ROOT
    fi

    export LD_PREBIND_ALLOW_OVERLAP=1

    if [ -n "$ARCHS" ]; then
	for arch in $ARCHS ; do
	    CC=$CC CFLAGS="$CFLAGS -arch $arch" \
		CXX=$CXX CXXFLAGS="$CXXFLAGS -arch $arch" \
		CPP=$CPP CPPFLAGS="$CPPFLAGS -arch $arch" \
		LDFLAGS="$LDFLAGS -arch $arch" \
		./configure --host `./config.guess`
	    make
	    make install DESTDIR=$DSTROOT-$arch
	    make clean
	done
	mkdir $DSTROOT
	arch=`./config.guess | \
	    sed -e s/-.*// -e s/i.86/i386/ -e s/powerpc/ppc/`
	[ "$arch" = "ppc" -a ! -d $DSTROOT-ppc ] && arch=ppc7400
	[ ! -d $DSTROOT-$arch ] && arch=`sed "s/ .*//" <<< $ARCHS`
	for d in `(cd $DSTROOT-$arch ; find . -type d)` ; do
	    mkdir -p $DSTROOT/$d
	done
	for f in `(cd $DSTROOT-$arch ; find . -type f)` ; do
	    if [ `wc -w <<< $ARCHS` -gt 1 ] ; then
		file $DSTROOT-$arch/$f | grep -q -e 'Mach-O\|ar archive'
		if [ $? -eq 0 ] ; then
		    lipo -c -o $DSTROOT/$f $DSTROOT-*/$f
		else
		    cp -p $DSTROOT-$arch/$f $DSTROOT/$f
		fi
	    else
		cp -p $DSTROOT-$arch/$f $DSTROOT/$f
	    fi
	done
	for l in `(cd $DSTROOT-$arch ; find . -type l)` ; do
	    cp -pR $DSTROOT-$arch/$l $DSTROOT/$l
	done
	rm -rf $DSTROOT-*
    else
	CC=$CC CFLAGS="$CFLAGS" \
	    CXX=$CXX CXXFLAGS="$CXXFLAGS" \
	    CPP=$CPP CPPFLAGS="$CPPFLAGS" \
	    LDFLAGS="$LDFLAGS" \
	    ./configure --host `./config.guess`
	make
	make install DESTDIR=$DSTROOT
    fi
)

rm -rf $BUILD

sudo chown -Rh root:wheel $DSTROOT
sudo chown root:admin $DSTROOT
sudo chmod 1775 $DSTROOT

PKG=`pwd`/../PKGS/$MACOSX_DEPLOYMENT_TARGET/$DSTNAME.pkg
[ -e $PKG ]        && ( rm -rf $PKG        || exit 1 )
[ -e $PKG.tar.gz ] && ( rm -rf $PKG.tar.gz || exit 1 )
mkdir -p ../PKGS/$MACOSX_DEPLOYMENT_TARGET

RESOURCEDIR=/tmp/$DSTNAME.resources
[ -e $RESOURCEDIR ] && ( rm -rf $RESOURCEDIR || exit 1 )
mkdir -p $RESOURCEDIR

(
    cd pkg/Resources
    for d in `find . -type d` ; do
	mkdir -p $RESOURCEDIR/$d
    done
    for f in `find . -type f -a ! -name .DS_Store` ; do
	cat $f | sed s/@MACOSX_DEPLOYMENT_TARGET@/$MACOSX_DEPLOYMENT_TARGET/g \
	    > $RESOURCEDIR/$f
    done
)

chmod +x $RESOURCEDIR/InstallationCheck

#  Remove the installation check if we don't use SDK
if [ -z "$SDKVERSION" ]; then
    rm $RESOURCEDIR/InstallationCheck
    rm $RESOURCEDIR/*.lproj/InstallationCheck.strings
    rmdir $RESOURCEDIR/*.lproj 2> /dev/null
fi

(
    cd $RESOURCEDIR

#  Normally either English.lproj or en.lproj are OK for language files, but due
#  to a bug in the Installer application, packages can only use one of them.
#  Also, due to another bug, the Installer doesn't have a resonable default for
#  for the InstallationCheck.strings and VolumeCheck.strings, so we must copy
#  the default file for all languages that don't have a localized version

    if [ -f English.lproj/InstallationCheck.strings ] ; then
	for lang in Dutch English French German Italian Japanese Spanish \
	    da fi ko no pt sv zh_CN zh_TW ; do
	  [ ! -d ${lang}.lproj ] && mkdir ${lang}.lproj
	  [ ! -f ${lang}.lproj/InstallationCheck.strings ] && \
	      cp -p English.lproj/InstallationCheck.strings ${lang}.lproj
	done
    fi

    if [ -f English.lproj/VolumeCheck.strings ] ; then
	for lang in Dutch English French German Italian Japanese Spanish \
	    da fi ko no pt sv zh_CN zh_TW ; do
	  [ ! -d ${lang}.lproj ] && mkdir ${lang}.lproj
	  [ ! -f ${lang}.lproj/VolumeCheck.strings ] && \
	      cp -p English.lproj/VolumeCheck.strings ${lang}.lproj
	done
    fi
)

/Developer/usr/bin/packagemaker -o $PKG -r $DSTROOT \
    -f pkg/Info.plist -n $DSTVERSION -e $RESOURCEDIR -x .DS_Store -v

# If the major version is 0 packagemaker will change it into 1
# so change it back
printf "major: %i\nminor: %i" \
    `cut -d '.' -f 1 <<< $DSTVERSION` \
    `cut -d '.' -f 2 <<< $DSTVERSION` > \
    $PKG/Contents/Resources/package_version

# packagemaker creates a PkgInfo that reads "pkmkrpkg1" (9 letters [sic!])
printf "pmkrpkg1" > $PKG/Contents/PkgInfo

# packagemaker creates Description.plist in the wrong folder (the lproj
# directory corresponding to the two-letter code of the current locale)
if [ ! -e $PKG/Contents/Resources/English.lproj/Description.plist ] ; then
    mv $PKG/Contents/Resources/*.lproj/Description.plist \
	$PKG/Contents/Resources/English.lproj
    rmdir $PKG/Contents/Resources/*.lproj 2> /dev/null
fi

(
    cd `dirname $PKG`
    tar -z -c -f $PKG.tar.gz `basename $PKG`
)

rm -rf $RESOURCEDIR

if [ -z "$SDKVERSION" ]; then
    sudo rm -rf $DSTROOT
    exit 0
fi

SDKPKG=`pwd`/../PKGS/SDKs/$DSTNAME-$SDKVERSION.sdk.pkg
SDKDSTROOT=/tmp/$DSTNAME-$SDKVERSION.sdk.dst
SDK_NEXT_ROOT=/usr/local${NEXT_ROOT}

[ -e $SDKPKG ]        && (      rm -rf $SDKPKG        || exit 1 )
[ -e $SDKPKG.tar.gz ] && (      rm -rf $SDKPKG.tar.gz || exit 1 )
[ -e $SDKDSTROOT ]    && ( sudo rm -rf $SDKDSTROOT    || exit 1 )

mkdir -p ../PKGS/SDKs

for f in `find $DSTROOT -name "*.h" -o -name "*.a" -o -name "*.la" -o \
	-name "*.dylib"`; do
    ff=`sed s#$DSTROOT#$SDKDSTROOT$SDK_NEXT_ROOT# <<< $f`
    mkdir -p `dirname $ff`
    cp -pR $f $ff
done

for f in `find $SDKDSTROOT$SDK_NEXT_ROOT -type f -a -name "*.dylib"`; do
    echo "stripping $f to create stub library..."
    strip -cx $f
done

for f in `find $SDKDSTROOT$SDK_NEXT_ROOT -type f -a -name "*.la"`; do
    sed "s#libdir='\(.*\)'#libdir='$SDK_NEXT_ROOT\1'#" < $f > $f.new
    mv -f $f.new $f
done

sed s/@SDKVERSION@/$SDKVERSION/ < sdk/Info.plist > /tmp/Info.plist

sudo chown -Rh root:wheel $SDKDSTROOT
sudo chown root:admin $SDKDSTROOT
sudo chmod 1775 $SDKDSTROOT

RESOURCEDIR=/tmp/$DSTNAME.sdk.resources
[ -e $RESOURCEDIR ] && ( rm -rf $RESOURCEDIR || exit 1 )
mkdir -p $RESOURCEDIR

(
    cd sdk/Resources
    for d in `find . -type d` ; do
	mkdir -p $RESOURCEDIR/$d
    done
    for f in `find . -type f -a ! -name .DS_Store` ; do
	cat $f | sed s/@SDKVERSION@/$SDKVERSION/g \
	    | sed s#@NEXT_ROOT@#$NEXT_ROOT#g \
	    > $RESOURCEDIR/$f
    done
)

chmod +x $RESOURCEDIR/InstallationCheck

(
    cd $RESOURCEDIR

#  Normally either English.lproj or en.lproj are OK for language files, but due
#  to a bug in the Installer application, packages can only use one of them.
#  Also, due to another bug, the Installer doesn't have a resonable default for
#  for the InstallationCheck.strings and VolumeCheck.strings, so we must copy
#  the default file for all languages that don't have a localized version

    if [ -f English.lproj/InstallationCheck.strings ] ; then
	for lang in Dutch English French German Italian Japanese Spanish \
	    da fi ko no pt sv zh_CN zh_TW ; do
	  [ ! -d ${lang}.lproj ] && mkdir ${lang}.lproj
	  [ ! -f ${lang}.lproj/InstallationCheck.strings ] && \
	      cp -p English.lproj/InstallationCheck.strings ${lang}.lproj
	done
    fi

    if [ -f English.lproj/VolumeCheck.strings ] ; then
	for lang in Dutch English French German Italian Japanese Spanish \
	    da fi ko no pt sv zh_CN zh_TW ; do
	  [ ! -d ${lang}.lproj ] && mkdir ${lang}.lproj
	  [ ! -f ${lang}.lproj/VolumeCheck.strings ] && \
	      cp -p English.lproj/VolumeCheck.strings ${lang}.lproj
	done
    fi
)

/Developer/usr/bin/packagemaker -o $SDKPKG -r $SDKDSTROOT \
    -f /tmp/Info.plist -n $DSTVERSION -e $RESOURCEDIR -x .DS_Store -v

# If the major version is 0 packagemaker will change it into 1
# so change it back
printf "major: %i\nminor: %i" \
    `cut -d '.' -f 1 <<< $DSTVERSION` \
    `cut -d '.' -f 2 <<< $DSTVERSION` > \
    $SDKPKG/Contents/Resources/package_version

# packagemaker creates a PkgInfo that reads "pkmkrpkg1" (9 letters [sic!])
printf "pmkrpkg1" > $SDKPKG/Contents/PkgInfo

# packagemaker creates Description.plist in the wrong folder (the lproj
# directory corresponding to the two-letter code of the current locale)
if [ ! -e $SDKPKG/Contents/Resources/English.lproj/Description.plist ] ; then
    mv $SDKPKG/Contents/Resources/*.lproj/Description.plist \
	$SDKPKG/Contents/Resources/English.lproj
    rmdir $SDKPKG/Contents/Resources/*.lproj 2> /dev/null
fi

(
    cd `dirname $SDKPKG`
    tar -z -c -f $SDKPKG.tar.gz `basename $SDKPKG`
)

rm /tmp/Info.plist
rm -rf $RESOURCEDIR

sudo rm -rf $SDKDSTROOT

sudo rm -rf $DSTROOT
