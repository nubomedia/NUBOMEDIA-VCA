#!/bin/bash

echo "Generating deb packages..."
sh genereate_deb.sh
echo "Generating apps .........."
sh generate_apps.sh
