#!/bin/bash

OUTPUT=./output
DEB_PACK=$OUTPUT/packages
APPS=apps
OUT_APPS=$OUTPUT/apps

CURRENT_PATH=../../../
cd modules/nubo_ear/nubo-ear-detector/ ;  sudo sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_face/nubo-face-detector/ ;  sudo sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_mouth/nubo-mouth-detector/ ; sudo sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_nose/nubo-nose-detector/ ;  sudo sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_tracker/nubo-tracker/ ;  sudo sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_eye/nubo-eye-detector/ ;  sudo sh compile_filter.sh; cd $CURRENT_PATH

mkdir -p $DEB_PACK
mkdir -p $OUT_APPS
rm -f $DEB_PACK/*

cp modules/nubo_ear/*deb $DEB_PACK
cp modules/nubo_face/*deb $DEB_PACK
cp modules/nubo_mouth/*deb $DEB_PACK
cp modules/nubo_nose/*deb $DEB_PACK
cp modules/nubo_tracker/*deb $DEB_PACK
cp modules/nubo_eye/*deb $DEB_PACK

cd $APPS/; sh generate_apps.sh; cp -r  apps_install/*zip  ../$OUT_APPS
