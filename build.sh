rm -f src/bbtether/*.pyc
cd src
tar czvf ../bbtether.tgz bbtether
cd ..
pylint -e src/bbtether/*py
