#!/bin/bash

echo "Generating deb packages..."
sh generate_deb.sh
echo "Generating apps .........."
sh generate_apps.sh
