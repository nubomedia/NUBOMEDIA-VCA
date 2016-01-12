#include <iostream>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;
 
class BaseFace
{
public:
  BaseFace();  // This is the constructor
  BaseFace(Rect);  // This is the constructor
  BaseFace(int,int,int,int);
  
  Point get_center();
  Rect get_face();

  int get_id();

  void set_face(Point);
  void set_face(Rect);
  void set_id(int);
  void draw_face_in_image(CvArr*);
  void draw_face_in_image(CvArr*, int, int);
  int get_area();
  void print();
 
  ~BaseFace();

private:
  Rect face;
  Point center_p;
  void calc_center();
  int id;
  const static Scalar colors[];
};


