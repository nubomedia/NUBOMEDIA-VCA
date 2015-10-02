#!/bin/bash
#gst-launch-1.0 --gst-plugin-path=. uridecodebin uri="file:////opt/video/fiwarecut.webm" ! videoconvert ! facedetect ! videoconvert ! autovideosink
#gst-launch-1.0 --gst-plugin-path=. v4l2src ! videoconvert ! facedetect ! videoconvert ! autovideosink
gst-launch-1.0 --gst-plugin-path=. uridecodebin uri="file:////opt/video/car.avi" ! videoconvert ! nubotracker ! videoconvert ! autovideosink
