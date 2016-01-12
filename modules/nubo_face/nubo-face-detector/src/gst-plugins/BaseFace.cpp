#include "BaseFace.hpp"

// Member functions definitions including constructor
BaseFace::BaseFace(void)
{
  face.x=0;
  face.y=0;
  face.width=0;
  face.height=0;
  id=-1;
  calc_center();
}

BaseFace::BaseFace(Rect f)
{
  face=f;
  id=-1;
  calc_center();
}
 
BaseFace::BaseFace(int x,int y,int width,int height)
{
  face.x=x;
  face.y=y;
  face.width=width;
  face.height=height;
  id=-1;
  calc_center();
} 

Point BaseFace::get_center()
{
  return center_p;
}

Rect BaseFace::get_face()
{
  return face;
}

int BaseFace::get_id()
{
  return id;
}

void BaseFace::set_face(Point f)
{
  face.x = f.x;
  face.y = f.y;
  calc_center();
}

void BaseFace::set_face(Rect f)
{
  face=f;
  calc_center();    
}

void BaseFace::set_id(int id)
{
  this->id = id;
}

void BaseFace::draw_face_in_image(CvArr *img)
{
  draw_face_in_image(img, 1,1);

}

void BaseFace::draw_face_in_image(CvArr *img, int scale=1, int iter=0)
{
  Scalar color = colors[1];
  char it_name[100];
  sprintf(it_name,"%d",iter);

  cvRectangle( img, cvPoint(cvRound(face.x*scale), 
			    cvRound(face.y*scale)),
	       cvPoint(cvRound((face.x + face.width-1)*scale), 
		       cvRound((face.y + face.height-1)*scale)),
	       color, 3, 8, 0);

}

void BaseFace::print()
{
  
  cout << "Face id: "<< id <<  " (x,y): " << face.x << "," << face.y;
  cout << " Size (width,height): " << face.width << "," << face.height << endl;
  cout << "center x,y: " << center_p.x << "," << center_p.y << endl;
}


int BaseFace::get_area()
{
  return face.width * face.height;
}

void BaseFace::calc_center()
{
  center_p.x = face.x + face.width/2;
  center_p.y = face.y + face.height/2; 
}

BaseFace::~BaseFace(void)
{
  
}

							       
