#!/bin/bash

OUTPUT=./output
DEB_PACK=$OUTPUT/packages
JS_API=$OUTPUT/js_api

CURRENT_PATH=../../../
#cd modules/nubo_ear/nubo-ear-detector/ ;     sh compile_filter.sh;  sh compile_filter.sh js; cd $CURRENT_PATH
#cd modules/nubo_eye/nubo-eye-detector/ ;     sh compile_filter.sh; sh compile_filter.sh js; cd $CURRENT_PATH
cd modules/nubo_face/nubo-face-detector/ ;   sh compile_filter.sh; sh compile_filter.sh js; cd $CURRENT_PATH
#cd modules/nubo_mouth/nubo-mouth-detector/ ; sh compile_filter.sh; sh compile_filter.sh js; cd $CURRENT_PATH
#cd modules/nubo_nose/nubo-nose-detector/ ;   sh compile_filter.sh; sh compile_filter.sh js; cd $CURRENT_PATH
#cd modules/nubo_tracker/nubo-tracker/ ;      sh compile_filter.sh; sh compile_filter.sh js; cd $CURRENT_PATH

mkdir -p $DEB_PACK
mkdir -p $JS_API
rm -f $DEB_PACK/*
rm -rf $JS_API/*

#cp modules/nubo_ear/*deb $DEB_PACK
#cp modules/nubo_eye/*deb $DEB_PACK
cp modules/nubo_face/*deb $DEB_PACK
#cp modules/nubo_mouth/*deb $DEB_PACK
#cp modules/nubo_nose/*deb $DEB_PACK
#cp modules/nubo_tracker/*deb $DEB_PACK


#mkdir -p $JS_API/nuboeardetector/js/   $JS_API/nuboeyedetector/js/  $JS_API/nubofacedetector/js/
#mkdir -p $JS_API/nubomouthdetector/js/ $JS_API/nubonosedetector/js/ $JS_API/nubotracker/js/

mkdir -p $JS_API/nubofacedetector/js/

#cp modules/nubo_ear/nubo-ear-detector/build/js/dist/*    $JS_API/nuboeardetector/js/
#cp modules/nubo_eye/nubo-eye-detector/build/js/dist/*    $JS_API/nuboeyedetector/js/
cp modules/nubo_face/nubo-face-detector/build/js/dist/*  $JS_API/nubofacedetector/js/
#cp modules/nubo_mouth/nubo-mouth-detector/build/js/dist/*  $JS_API/nuboeardetector/js/
#cp modules/nubo_nose/nubo-nose-detector/build/js/dist/*  $JS_API/nubonosedetector/js/
#cp modules/nubo_tracker/nubo-tracker/build/js/dist/*     $JS_API/nubotracker/js/



