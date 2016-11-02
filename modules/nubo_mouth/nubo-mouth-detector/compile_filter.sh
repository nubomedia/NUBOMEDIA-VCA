#!/bin/sh

mkdir -p build
cd build; cmake ../; make; cmake ../ -DGENERATE_JAVA_CLIENT_PROJECT=TRUE;
debuild -us -uc
