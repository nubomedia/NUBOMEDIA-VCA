#include "BaseFace.hpp"
#include <vector>

class Faces
{
public:
  Faces();
  Faces(vector<BaseFace>&);
  Faces(vector<Rect> &);

  //get_faces can modified the Rect through the BaseFace class
  int get_faces(vector<BaseFace> **);
  //get_faces can not modified the Rect 
  int get_faces(vector<Rect> *);
  void add_face(Rect);  
  void add_face(BaseFace);  
  void track_faces(Faces *, int, int,int,int);
  void delete_face(int);
  void draw(CvArr*,int,int);
  void print();
  void clear();

private:
  vector<BaseFace> *faces;  
  int faces_id;
  int calc_distance(Point , Point );
  int calc_diff_area_percentage(int , int , FILE *);
  int get_distance_limit(int, int);
};


