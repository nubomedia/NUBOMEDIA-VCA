#!/bin/sh

OUTPUT=./output
APPS=apps
OUT_APPS=$OUTPUT/apps

mkdir -p $OUT_APPS

cd $APPS/; sh generate_apps.sh; cp -r  apps_install/*zip  ../$OUT_APPS
