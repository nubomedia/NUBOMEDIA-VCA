include_directories(
  ${KMSCORE_INCLUDE_DIRS}
  ${GSTREAMER_INCLUDE_DIRS}
  ${GSTREAMER_VIDEO_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${libsoup-2.4_INCLUDE_DIRS}
  ${OPENCV_INCLUDE_DIRS}
)

set(NUBO_MOUTH_DETECTOR_SOURCES 
  nubomouthdetector.c
  kmsmouthdetect.cpp
  kmsmouthdetect.h
)

add_library(nubomouthdetector MODULE ${NUBO_MOUTH_DETECTOR_SOURCES})

target_link_libraries(nubomouthdetector
  kmsgstcommons
  ${GSTREAMER_LIBRARIES}
  ${GSTREAMER_VIDEO_LIBRARIES}
  ${OPENCV_LIBRARIES}
  ${libsoup-2.4_LIBRARIES}
)

install(
  TARGETS nubomouthdetector
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_GST_PLUGINS_DIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
