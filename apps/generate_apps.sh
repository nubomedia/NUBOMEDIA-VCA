#!/bin/sh

mkdir -p ../output

# Ear port => 8104
#cd NuboEarJava/; sh generate_zip.sh; cd ../

#Eye port => 8108
#cd NuboEyeJava/; sh generate_zip.sh; cd ../

#Face port => 8100
cd NuboFaceJava/; sh generate_zip.sh; cd ../

#Face Profile port  =>  8105
#cd NuboFaceProfileJava/; sh generate_zip.sh; cd ../

#Mouth port =>  8103
#cd NuboMouthJava/; sh generate_zip.sh; cd ../

#Nose port => 8102
#cd NuboNoseJava/; sh generate_zip.sh; cd ../

#Tracker port =>  8107
#cd NuboTrackerJava/; sh generate_zip.sh; cd ../

