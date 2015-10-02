#!/bin/sh

APP_NAME=NuboFaceProfileJava
VERSION=6.0.0
DIR_INSTALL=apps_install

mkdir -p ../$DIR_INSTALL
rm -f install/*zip install/*jar
mvn package
cp target/$APP_NAME-$VERSION.jar install/
cd install; mv $APP_NAME-$VERSION.jar $APP_NAME.jar; zip $APP_NAME.zip *sh *jar;
cp $APP_NAME.zip ../../$DIR_INSTALL

echo "Done!"

