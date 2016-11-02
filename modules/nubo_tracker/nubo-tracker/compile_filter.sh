#!/bin/sh

mkdir -p build
cd build; cmake ../ -DGENERATE_JAVA_CLIENT_PROJECT=TRUE; make;
debuild -us -uc
