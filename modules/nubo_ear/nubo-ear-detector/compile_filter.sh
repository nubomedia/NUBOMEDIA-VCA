
#!/bin/sh

#kurento-module-scaffold.sh example kms-example opencv_filter
mkdir -p build
cd build; cmake ../; sleep 2 ; make; sleep 2 ; sudo make install;  cmake ../  -DGENERATE_JAVA_CLIENT_PROJECT=TRUE; sleep 2; make java_install
sudo debuild -us -uc

