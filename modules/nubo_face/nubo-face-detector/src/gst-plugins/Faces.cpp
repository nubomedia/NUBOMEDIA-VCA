#include "Faces.hpp"
#include <math.h>

#define AREA_PERCENTAGE 15

Faces::Faces(void)
{
  
  faces = new vector<BaseFace>;
  faces_id= 0;
}

Faces::Faces(vector<BaseFace> &v)
{
  int i;
  faces = new vector<BaseFace>;
  *faces = v;
  faces_id= 0;
  for (i=0; i < faces->size();i++)
    {
      faces->at(i).set_id(faces_id);      
      faces_id++;
    }
}

Faces::Faces(vector<Rect> &v)
{
  int i;
  faces = new vector<BaseFace>;
  faces_id=0;

  for (i=0; i < v.size();i++)
    {
      faces->push_back(BaseFace(v.at(i)));          
      faces->at(i).set_id(faces_id);
      faces_id++;      
    }
}

int Faces::get_faces(vector<BaseFace> **v)
{
  
  if (NULL == *v)
    return -1;

  *v= faces;

  return 0;
}

int Faces::get_faces(vector<Rect> *v)
{

  if (NULL == v)
    return -1;

  for (vector<BaseFace>::const_iterator it = faces->begin() ; it != faces->end(); ++it)    
    {
      BaseFace bf = (*it);
      v->push_back(bf.get_face());
    }

  cout << "Size get faces " << v->size()<< endl;
  return 0;
}

void Faces::add_face(Rect f)
{
  BaseFace new_f(f);  
  faces->push_back(new_f);  
}

void Faces::add_face(BaseFace f)
{
  faces->push_back(f);  
}

void Faces::track_faces(Faces *cf,int track_threshold, int pos_threshold, int area_threshold, int n_iter)
{
  vector<BaseFace>::iterator it_cf;
  vector<BaseFace>::iterator it_f ;
  vector<BaseFace> *new_v = new vector<BaseFace>;

  int t_distance;
  int pos;
  int iter;
  FILE *f;

  for (it_f = faces->begin(); it_f !=  faces->end(); it_f++)
    {
      t_distance = track_threshold;
      pos=-1;
      iter=0;
      for (it_cf = cf->faces->begin(); it_cf != cf->faces->end(); it_cf++)
	{
	  int aux_dis = calc_distance( it_cf->get_center(),
				       it_f->get_center());
	  if (t_distance > aux_dis)
	    {
	      pos=iter;
	      t_distance = aux_dis;
	    }
	  iter++;
	}
      
      if (pos >= 0)
	{ //There is a merge of faces

	  int aux_dis= calc_distance(it_f->get_center(),
				     cf->faces->at(pos).get_center());
	  	
	  if ( (get_distance_limit(it_f->get_area(),cf->faces->at(pos).get_area())) < aux_dis)
	    { //Adding new face. Distance among faces > limit (based on size)
	      cf->faces->at(pos).set_id(it_f->get_id());
	      new_v->push_back(cf->faces->at(pos));	      
	    }
	  else 
	    if  (AREA_PERCENTAGE < calc_diff_area_percentage(it_f->get_area(),
							     cf->faces->at(pos).get_area(),f))
	      {//Adding new face. Difference based in size is higher than AREA PERCENTAGE
		Rect new_r;
		new_r.x = it_f->get_face().x;
		new_r.y = it_f->get_face().y;
		new_r.width  = cf->faces->at(pos).get_face().width;
		new_r.height = cf->faces->at(pos).get_face().height;
		BaseFace new_face(new_r);
		new_face.set_id(it_f->get_id());
		new_v->push_back(new_face);
	      }

	    else{ // leave the old face	      	      
	      new_v->push_back(*it_f);
	    }
	  
	  cf->delete_face(pos);	  
	}

    }   
  if (cf->faces->size() > 0)
          
      for (it_cf = cf->faces->begin(); it_cf != cf->faces->end(); it_cf++)
	{
	  it_cf->set_id(faces_id);
	  faces_id++;
	  new_v->push_back(*it_cf);
	}

  delete faces;
  faces = new_v;

  this->print();

}

void Faces::delete_face(int pos)
{
  faces->erase(faces->begin() + pos);  
}

int Faces::calc_diff_area_percentage(int size1, int size2, FILE *f)
{
  int diff = abs(size1 -size2);
  return  (diff*100)/size2;
}

int Faces::get_distance_limit(int size1, int size2)
{
  int big_size;
  int limit;

  if (size1 > size2) big_size=size1;
  else big_size=size2;

  if (big_size > 5000)
    limit=8;
  else if (big_size > 2500)
    limit = 5;
  else  limit=3;
      
  return limit;
}

int Faces::calc_distance(Point p1, Point p2)
{
  double h2;
  h2 = sqrt(pow((p2.x -p1.x),2) + pow((p2.y- p1.y),2));
  return (int)h2;
}

void Faces::draw(CvArr *img,int scale, int iter)
{
  for (vector<BaseFace>::iterator it = faces->begin() ; it != faces->end(); ++it)        
    it->draw_face_in_image(img,scale,iter);
    
}

void Faces::clear()
{
  if (faces->size() > 0)
    faces->clear();
}
void Faces::print()
{
  cout<< "Number of faces: " << faces->size() << endl;

  for(int i=0; i < faces->size();i++)    
    {
      cout << "********** Face " << i << "**********" << endl;
      faces->at(i).print();
    } 
}

