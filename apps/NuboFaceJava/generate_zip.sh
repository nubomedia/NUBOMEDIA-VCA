#!/bin/sh

APP_NAME=NuboFaceJava
VERSION=6.4.0
DIR_INSTALL=apps_install
MVN_CENTRAL=mvn_central

mkdir -p ../$DIR_INSTALL
rm -f install/*zip install/*jar
mvn package 
cp target/$APP_NAME-$VERSION.jar install/
cd install; mv $APP_NAME-$VERSION.jar ./$APP_NAME.jar; zip $APP_NAME.zip *sh *jar;
cp $APP_NAME.zip ../../$DIR_INSTALL

if [ "$1" = "maven-central" ]; then
    cd ../
    mvn javadoc:jar
    mkdir -p $MVN_CENTRAL
    rm -rf $MVN_CENTRAL/*
    cp target/$APP_NAME-$VERSION.jar  $MVN_CENTRAL
    cp target/$APP_NAME-$VERSION-javadoc.jar  $MVN_CENTRAL/
    cp target/$APP_NAME-$VERSION-sources.jar  $MVN_CENTRAL/

    cp target/$APP_NAME-$VERSION-sources.zip  $MVN_CENTRAL/
    cp $MVN_CENTRAL/$APP_NAME-$VERSION-sources.zip target/$APP_NAME-$VERSION-sources.zio

    cp pom.xml $MVN_CENTRAL/
    cd $MVN_CENTRAL/; gpg2 -ab $APP_NAME-$VERSION.jar
    gpg2 -ab $APP_NAME-$VERSION-javadoc.jar
    gpg2 -ab $APP_NAME-$VERSION-sources.jar
    gpg2 -ab pom.xml 
fi

echo "Done!"
