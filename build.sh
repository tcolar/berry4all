mkdir dist
rm -f src/bbtether/*.pyc
cd src
tar czvf ../dist/bbtether.tgz bbtether
zip -r ../dist/bbtether.zip bbtether
cd ..
#pylint -e src/bbtether/*py
#pychecker src/bbtether/*py
