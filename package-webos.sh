#!/bin/bash
# Package Abuse for webOS

set -e

PKGDIR=webos-pkg
APPID=com.abuse.game
APPDIR=$PKGDIR/usr/palm/applications/$APPID
OUTFILE=${APPID}_0.8.0_armv7.ipk

echo "Creating package structure..."
rm -rf $PKGDIR/usr $PKGDIR/data.tar.gz $PKGDIR/control.tar.gz
mkdir -p $APPDIR

# Copy binary
echo "Copying binary..."
cp src/abuse $APPDIR/

# Copy game data
echo "Copying game data..."
cp -r data $APPDIR/

# Copy appinfo.json
cp $PKGDIR/appinfo.json $APPDIR/

# Create a placeholder icon if missing
if [ ! -f "$APPDIR/icon.png" ]; then
    echo "Note: No icon.png found - creating placeholder"
fi

# Create data.tar.gz
echo "Creating data archive..."
cd $PKGDIR
tar czf data.tar.gz usr
cd ..

# Create control.tar.gz
echo "Creating control archive..."
cd $PKGDIR/CONTROL
tar czf ../control.tar.gz .
cd ../..

# Create debian-binary
echo "2.0" > $PKGDIR/debian-binary

# Create IPK (ar archive)
echo "Creating IPK package..."
cd $PKGDIR
ar -r ../$OUTFILE debian-binary control.tar.gz data.tar.gz
cd ..

echo ""
echo "Package created: $OUTFILE"
ls -la $OUTFILE
echo ""
echo "To install on webOS device:"
echo "  novacom put file:///media/internal/$OUTFILE < $OUTFILE"
echo "  novacom run file:///usr/bin/ipkg install /media/internal/$OUTFILE"
