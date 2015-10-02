#!/bin/bash

OUTPUT=./output
DEB_PACK=$OUTPUT/packages

CURRENT_PATH=../../../
cd modules/nubo_ear/nubo-ear-detector/ ;  sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_eye/nubo-eye-detector/ ;  sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_face/nubo-face-detector/ ;  sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_mouth/nubo-mouth-detector/ ; sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_nose/nubo-nose-detector/ ;  sh compile_filter.sh; cd $CURRENT_PATH
cd modules/nubo_tracker/nubo-tracker/ ;  sh compile_filter.sh; cd $CURRENT_PATH

mkdir -p $DEB_PACK
rm -f $DEB_PACK/*

cp modules/nubo_ear/*deb $DEB_PACK
cp modules/nubo_eye/*deb $DEB_PACK
cp modules/nubo_face/*deb $DEB_PACK
cp modules/nubo_mouth/*deb $DEB_PACK
cp modules/nubo_nose/*deb $DEB_PACK
cp modules/nubo_tracker/*deb $DEB_PACK



