
#!/bin/sh

mkdir -p build
if [ "$1" = "js" ]; then
	cd build;  cmake .. -DGENERATE_JS_CLIENT_PROJECT=TRUE;  cd js; sudo npm install grunt grunt-browserify grunt-contrib-clean grunt-jsdoc grunt-npm2bower-sync minifyify; grunt

else 
	cd build; cmake ../; sleep 2 ; make; sleep 2 ; sudo make install;  cmake ../  -DGENERATE_JAVA_CLIENT_PROJECT=TRUE; sleep 2; make java_install
	sudo debuild -us -uc
fi

