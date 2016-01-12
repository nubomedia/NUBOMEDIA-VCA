
#ifndef __BASE_FACE_HPP__
#define __BASE_FACE_HPP__

#include <string>
#include <unistd.h>
#include <iostream>
#include <opencv2/opencv.hpp>

using namespace cv;

class BaseFace :
{

public: 

BaseFace ();
BaseFace (Rect fp);
BaseFace (int x, int y, int width, int height);

Rect get_face();
int get_width();  // {return width};
int get_height(); // {return height};
Point get_center_point();
int get_area();

void set_x(int x);
void set_y(int y);
void set_face (Rect p);
void set_width(int width);
void set_height (int height);
 
  private:
  
  Rect face;
  Point center_p;

   calc_center();
}
#endif /*__FACE_HPP__*/
