#!/bin/bash

EAR_NAME=nubo-ear-detector
EAR_PACK=modules/nubo_ear
EAR_MODULE=$EAR_PACK/nubo-ear-detector

NOSE_NAME=nubo-nose-detector
NOSE_PACK=modules/nubo_nose
NOSE_MODULE=$NOSE_PACK/nubo-nose-detector

MOUTH_NAME=nubo-mouth-detector
MOUTH_PACK=modules/nubo_mouth
MOUTH_MODULE=$MOUTH_PACK/nubo-mouth-detector

FACE_NAME=nubo-face-detector
FACE_PACK=modules/nubo_face
FACE_MODULE=$FACE_PACK/nubo-face-detector

EYE_NAME=nuboeyedetector
EYE_PACK=modules/nubo_eye
EYE_MODULE=$EYE_PACK/nubo-eye-detector

TRACKER_NAME=nubotracker
TRACKER_PACK=modules/nubo_tracker
TRACKER_MODULE=$TRACKER_PACK/nubotrackerdetector

#Ear
rm $EAR_PACK/*deb
rm $EAR_PACK/$EAR_NAME_*
rm -rf $EAR_MODULE/build/
rm -rf $EAR_MODULE/obj-x86_64-linux-gnu/
rm -rf $EAR_MODULE/debian/$EAR_NAME/
rm -rf $EAR_MODULE/debian/$EAR_NAME-dev/
rm -rf $EAR_MODULE/debian/$EAR_NAME*substvars
rm -rf $EAR_MODULE/debian/$EAR_NAME*debhelper
rm -rf $EAR_MODULE/debian/tmp/

#Nose
rm $NOSE_PACK/*deb
rm $NOSE_PACK/$NOSE_NAME_*
rm -rf $NOSE_MODULE/build/
rm -rf $NOSE_MODULE/obj-x86_64-linux-gnu/
rm -rf $NOSE_MODULE/debian/$NOSE_NAME/
rm -rf $NOSE_MODULE/debian/$NOSE_NAME-dev/
rm -rf $NOSE_MODULE/debian/$NOSE_NAME*substvars
rm -rf $NOSE_MODULE/debian/$NOSE_NAME*debhelper
rm -rf $NOSE_MODULE/debian/tmp/

#Mouth
rm $MOUTH_PACK/*deb
rm $MOUTH_PACK/$MOUTH_NAME_*
rm -rf $MOUTH_MODULE/build/
rm -rf $MOUTH_MODULE/obj-x86_64-linux-gnu/
rm -rf $MOUTH_MODULE/debian/$MOUTH_NAME/
rm -rf $MOUTH_MODULE/debian/$MOUTH_NAME-dev/
rm -rf $MOUTH_MODULE/debian/$MOUTH_NAME*substvars
rm -rf $MOUTH_MODULE/debian/$MOUTH_NAME*debhelper
rm -rf $MOUTH_MODULE/debian/tmp/

#Face
#rm $FACE_PACK/*deb
#rm $FACE_PACK/$FACE_NAME_*
rm -rf $FACE_MODULE/build/
rm -rf $FACE_MODULE/obj-x86_64-linux-gnu/
rm -rf $FACE_MODULE/debian/$FACE_NAME/
rm -rf $FACE_MODULE/debian/$FACE_NAME-dev/
rm -rf $FACE_MODULE/debian/$FACE_NAME*substvars
rm -rf $FACE_MODULE/debian/$FACE_NAME*debhelper
rm -rf $FACE_MODULE/debian/tmp/

#EYE
rm $EYE_PACK/*deb
rm $EYE_PACK/$EYE_NAME_*
rm -rf $EYE_MODULE/build/
rm -rf $EYE_MODULE/obj-x86_64-linux-gnu/
rm -rf $EYE_MODULE/debian/$EYE_NAME/
rm -rf $EYE_MODULE/debian/$EYE_NAME-dev/
rm -rf $EYE_MODULE/debian/$EYE_NAME*substvars
rm -rf $EYE_MODULE/debian/$EYE_NAME*debhelper
rm -rf $EYE_MODULE/debian/tmp/

#TRACKER
rm $TRACKER_PACK/*deb
rm $TRACKER_PACK/$EYE_NAME_*
rm -rf $TRACKER_MODULE/build/
rm -rf $TRACKER_MODULE/obj-x86_64-linux-gnu/
rm -rf $TRACKER_MODULE/debian/$TRACKER_NAME/
rm -rf $TRACKER_MODULE/debian/$TRACKER_NAME-dev/
rm -rf $TRACKER_MODULE/debian/$TRACKER_NAME*substvars
rm -rf $TRACKER_MODULE/debian/$TRACKER_NAME*debhelper
rm -rf $TRACKER_MODULE/debian/tmp/
