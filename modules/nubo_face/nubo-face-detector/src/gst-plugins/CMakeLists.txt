include(GLibHelpers)

include_directories(
  ${KMSCORE_INCLUDE_DIRS}
  ${GSTREAMER_INCLUDE_DIRS}
  ${GSTREAMER_VIDEO_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${OPENCV_INCLUDE_DIRS}
  ${CMAKE_CURRENT_BINARY_DIR}

)


set(NUBO_FACE_DETECTOR_SOURCES 
  nubo_face_meta.cpp
  nubofacedetector.c
  kmsfacedetect.cpp
  kmsfacedetect.h
  BaseFace.hpp
  BaseFace.cpp
  Faces.hpp
  Faces.cpp
)


add_library(nubofacedetector MODULE ${NUBO_FACE_DETECTOR_SOURCES})

target_link_libraries(nubofacedetector
  kmsgstcommons
  ${GSTREAMER_LIBRARIES}
  ${GSTREAMER_VIDEO_LIBRARIES}
  ${OPENCV_LIBRARIES}
)

install(
  TARGETS nubofacedetector
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_GST_PLUGINS_DIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
