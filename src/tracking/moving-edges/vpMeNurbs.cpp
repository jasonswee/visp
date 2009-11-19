/****************************************************************************
 *
 * $Id: vpMeNurbs.cpp,v 1.1 2009-01-15 15:52:32 nmelchio Exp $
 *
 * Copyright (C) 1998-2006 Inria. All rights reserved.
 *
 * This software was developed at:
 * IRISA/INRIA Rennes
 * Projet Lagadic
 * Campus Universitaire de Beaulieu
 * 35042 Rennes Cedex
 * http://www.irisa.fr/lagadic
 *
 * This file is part of the ViSP toolkit.
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined by Trolltech AS of Norway and appearing in the file
 * LICENSE included in the packaging of this file.
 *
 * Licensees holding valid ViSP Professional Edition licenses may
 * use this file in accordance with the ViSP Commercial License
 * Agreement provided with the Software.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Contact visp@irisa.fr if any conditions of this licensing are
 * not clear to you.
 *
 * Description:
 * Moving edges.
 *
 * Authors:
 * Nicolas Melchior
 *
 *****************************************************************************/


/*!
  \file vpMeNurbs.cpp
  \brief Moving edges
*/

#include <stdlib.h>
#include <visp/vpMeTracker.h>
#include <visp/vpMe.h>
#include <visp/vpMeSite.h>
#include <visp/vpMeNurbs.h>
#include <visp/vpRobust.h>
#include <visp/vpTrackingException.h>
#include <visp/vpImagePoint.h>
#include <visp/vpMath.h>
#include <visp/vpRect.h>
#include <visp/vpImageTools.h>
#include <visp/vpImageConvert.h>

#ifdef VISP_HAVE_OPENCV
#include <cv.h>
#endif

//Compute the angle delta = arctan(deltai/deltaj)
//and normalize it between 0 and pi
double
computeDelta(double deltai, double deltaj)
{
  double delta;
  delta =  atan2(deltai,deltaj) ;
  delta -= M_PI/2.0 ;
  while (delta > M_PI) { delta -= M_PI ; }
  while (delta < 0) { delta += M_PI ; }
  return(delta);
}

//Check if the image point is in the image and not to close to
//its edge to enable the computation of a convolution whith a mask.
static
bool outOfImage( vpImagePoint iP , int half , int rows , int cols)
{
  return((iP.get_i() < half + 1) || ( iP.get_i() > (rows - half - 3) )||(iP.get_j() < half + 1) || (iP.get_j() > (cols - half - 3) )) ;
}

//if iP is a edge point, it computes the angle corresponding to the
//highest convolution result. the angle is between 0 an 179.
//The result gives the angle in RADIAN + pi/2 (to deal with the moving edeg alpha angle)
//and the corresponding convolution result.
void findAngle(const vpImage<unsigned char> &I, const vpImagePoint iP, vpMe* me, double &angle, double &convlt)
{
  angle = 0.0;
  convlt = 0.0;
  for (int i = 0; i < 180; i++)
  {
    double conv = 0.0;
    int half;
    int index_mask;
    half = (me->mask_size - 1) >> 1 ;

    if(outOfImage( iP , half + me->strip , I.getHeight(), I.getWidth()))
    {
      conv = 0.0 ;
    }
    else
    {
      if (me->anglestep !=0)
        index_mask = (int)(i/(double)me->anglestep);
      else
	throw (vpException(vpException::divideByZeroError,"angle step = 0"));

      int ihalf = (int)(iP.get_i()-half) ;
      int jhalf = (int)(iP.get_j()-half) ;
      int ihalfa ;
      int a ;
      int b ;
      for( a = 0 ; a < me->mask_size ; a++ )
      {
        ihalfa = ihalf+a ;
        for( b = 0 ; b < me->mask_size ; b++ )
        {
	  conv += me->mask[index_mask][a][b] *
	  I(ihalfa,jhalf+b) ;
        }
      }
    }
    conv = fabs(conv);
    if (conv > convlt)
    {
      convlt = conv;
      angle = vpMath::rad(i);
      angle += M_PI/2;
      while (angle > M_PI) { angle -= M_PI ; }
      while (angle < 0) { angle += M_PI ; }
    }
  }
}


//Find the point belonging to the edge of the sub image which respects the following hypotheses:
//- the value of the pixel is upper than zero.
//- the distantce between the point and iP is less than 4 pixels.
//The function returns the nearest point of iP which respect the hypotheses
//If no point is found the returned point is (-1,-1)
vpImagePoint findFirstBorder(const vpImage<unsigned char> Isub, const vpImagePoint iP)
{
  double dist = 1e6;
  double dist_1 = 1e6;
  vpImagePoint index(-1,-1);
  for (unsigned int i = 0; i <= Isub.getRows(); i++)
  {
    for (unsigned int j = 0; j <= Isub.getCols(); j++)
    {
      if(i == 0 || i == Isub.getRows()-1 || j == 0 || j == Isub.getCols()-1)
      {
        if (Isub(i,j) > 0)
        {
          dist = vpImagePoint::sqrDistance(vpImagePoint(iP),vpImagePoint(i,j));
          if (dist <= 16 && dist < dist_1)
          {
            dist_1 = dist;
	    index.set_ij(i,j);
          }
        }
      }
    }
  }
  return index;
}


//Check if the list of vpImagePoint contains a distant point of less tha 4 pixels
//from the center of the sub image (ie the point (15,15). 
bool findCenterPoint(vpList<vpImagePoint> *ip_edges_list)
{
  ip_edges_list->front();
  double dist;
  while (!ip_edges_list->outside())
  {
    vpImagePoint iP = ip_edges_list->value();
    dist = vpImagePoint::sqrDistance(iP,vpImagePoint(15,15));
    if (dist <= 16)
    {
      return true;
    }
    ip_edges_list->next();
  }
  return false;
}

/***************************************/

/*!
  Basic constructor that calls the constructor of the class vpMeTracker.
*/
vpMeNurbs::vpMeNurbs():vpMeTracker()
{
  nbControlPoints = 20;
  beginPtFound = 0;
  endPtFound =0;
  enableCannyDetection = false;
  cannyTh1 = 100.0;
  cannyTh2 = 200.0;
}


/*!
  Basic destructor.
*/
vpMeNurbs::~vpMeNurbs()
{
}


/*!
  Initialization of the tracking. Ask the user to click left on several points
  along the edge to track and click right at the end.

  \param I : Image in which the edge appears.
*/
void
vpMeNurbs::initTracking(vpImage<unsigned char> &I)
{
  vpList<vpImagePoint> ptList;
  vpImagePoint pt;
  vpMouseButton::vpMouseButtonType b;

  while (vpDisplay::getClick(I, pt, b))
  {
    if (b == vpMouseButton::button1)
    {
      //std::cout<<pt<<std::endl;
      ptList.addRight(pt);
      vpDisplay::displayCross(I,pt,10,vpColor::green);
      vpDisplay::flush(I);
    }
    if (b == vpMouseButton::button3) break;
  }
  if (ptList.nbElements() > 0)
    initTracking(I, ptList);
  else
    throw (vpException(vpException::notInitialized,"No point to initialize the Nurbs"));
}


/*!
  Initilization of the tracking. The Nurbs is initialized thanks to the
  list of vpImagePoint.

  \param I : Image in which the edge appears.
  \param ptList  : List of point to initialize the Nurbs.
*/
void
vpMeNurbs::initTracking(vpImage<unsigned char> &I,
		       vpList<vpImagePoint> &ptList)
{
  nurbs.globalCurveInterp(ptList);

  sample(I);
  
  vpMeTracker::initTracking(I) ;
  track(I);
}


/*!
  Construct a list of vpMeSite moving edges at a particular sampling
  step between the two extremities of the nurbs.

  \param I : Image in which the edge appears.
*/
void
vpMeNurbs::sample(vpImage<unsigned char>& I)
{
  int rows = I.getHeight() ;
  int cols = I.getWidth() ;
  double step = 1.0 / (double)me->points_to_track;

  // Delete old list
  list.front();
  list.kill();

  vpImagePoint ip;
  double u = 0.0;
  vpImagePoint *pt = NULL;
  vpImagePoint pt_1(-rows,-cols);
  while (u <= 1.0)
  {
    if (pt!=NULL) delete[] pt;
    pt = nurbs.computeCurveDersPoint(u, 1);
    double delta = computeDelta(pt[1].get_i(),pt[1].get_j());

    // If point is in the image, add to the sample list
    if(!outOfImage(pt[0], 0, rows, cols) && vpImagePoint::sqrDistance(pt[0],pt_1) >= vpMath::sqr(me->sample_step))
    {
      vpMeSite pix ; //= list.value();
      pix.init(pt[0].get_i(), pt[0].get_j(), delta) ;
      pix.setDisplay(selectDisplay) ;

      list.addRight(pix);
      pt_1 = pt[0];
    }
    u = u+step;
  }
  if (pt!=NULL) delete[] pt;
}


/*!
  Suppression of the points which:
  
  - belong no more to the edge.
  - which are to closed to another point.
*/
void
vpMeNurbs::suppressPoints()
{
  // Loop through list of sites to track
  list.front();
  while(!list.outside())
  {
    vpMeSite s = list.value() ;//current reference pixel

    if (s.suppress != 0)
    {
      list.suppress() ;
    }
    else
      list.next() ;
  }
}


/*!
  Set the alpha value (normal to the edge at this point)
  of the different vpMeSite to a value computed thanks to the nurbs.
*/
void
vpMeNurbs::updateDelta()
{
  double u = 0.0;
  double d = 1e6;
  double d_1 = 1e6;
  list.front();
  
  vpImagePoint Cu;
  vpImagePoint* der = NULL;
  double step = 0.01;
  while (u < 1 && !list.outside())
  {
    vpMeSite s = list.value();
    vpImagePoint pt(s.i,s.j);
    while (d <= d_1 && u<1)
    {
      Cu = nurbs.computeCurvePoint(u);
      d_1=d;
      d = vpImagePoint::distance(pt,Cu);
      u +=step;
    }
    
    u-=step;
    if (der != NULL) delete[] der;
    der = nurbs.computeCurveDersPoint(u, 1);
      vpImagePoint toto(der[0].get_i(),der[0].get_j());
      //vpDisplay::displayCross(I,toto,4,vpColor::red);
    
    s.alpha = computeDelta(der[1].get_i(),der[1].get_j());
    list.modify(s);
    list.next();
    d = 1e6;
    d_1 = 1.5e6;
  }
  if (der != NULL) delete[] der;
}


/*!  
  Seek along the edge defined by the nurbs, the two extremities of
  the edge. This function is useful in case of translation of the
  edge.

  \param I : Image in which the edge appears.
*/
void
vpMeNurbs::seekExtremities(vpImage<unsigned char> &I)
{
  int rows = I.getHeight() ;
  int cols = I.getWidth() ;

  vpImagePoint* begin = NULL;
  vpImagePoint* end = NULL;

  begin = nurbs.computeCurveDersPoint(0.0,1);
  end = nurbs.computeCurveDersPoint(1.0,1);

  //Check if the two extremities are not to close to eachother.
  double d = vpImagePoint::distance(begin[0],end[0]);
  double threshold = 3*me->sample_step;
  double sample = me->sample_step;
  vpImagePoint pt;
  if ( d > threshold /*|| (list.firstValue()).mask_sign != (list.lastValue()).mask_sign*/)
  {
    vpMeSite P ;
    
    //Init vpMeSite
    P.init(begin[0].get_i(), begin[0].get_j(), (list.firstValue()).alpha, 0, (list.firstValue()).mask_sign) ;
    P.setDisplay(selectDisplay) ;

    //Set the range
    int  memory_range = me->range ;
    me->range = 2 ;
    
    //Point at the beginning of the list
    bool beginPtAdded = false;
    vpImagePoint pt_max = begin[0];
    double angle = atan2(begin[1].get_i(),begin[1].get_j());
    double co = vpMath::abs(cos(angle));
    co = co * vpMath::sign(begin[1].get_j());
    double si = vpMath::abs(sin(angle));
    si = si * vpMath::sign(begin[1].get_i());
    for (int i=0 ; i < 3 ; i++)
    {
      P.ifloat = P.ifloat - si*sample ; P.i = (int)P.ifloat ;
      P.jfloat = P.jfloat - co*sample ; P.j = (int)P.jfloat ;
      pt.set_ij(P.ifloat,P.jfloat);
      if (vpImagePoint::distance(end[0],pt) < threshold) break;
      if(!outOfImage(P.i, P.j, 5, rows, cols))
      {
        P.track(I,me,false);

        if (P.suppress ==0)
        {
	  list.front();
	  list.addLeft(P) ;
	  beginPtAdded = true;
	  pt_max = pt;
	  if (vpDEBUG_ENABLE(3)) {
	    vpDisplay::displayCross(I, pt, 5, vpColor::blue) ;
	  }
        }
        else {
	  if (vpDEBUG_ENABLE(3)) {
	    vpDisplay::displayCross(I, pt, 10, vpColor::blue) ;
	  }
        }
      }
    }
    
    if (!beginPtAdded) beginPtFound++;

    P.init(end[0].get_i(), end[0].get_j(), (list.lastValue()).alpha, 0, (list.lastValue()).mask_sign);
    P.setDisplay(selectDisplay);
    
    bool endPtAdded = false;
    angle = atan2(end[1].get_i(),end[1].get_j());
    co = vpMath::abs(cos(angle));
    co = co * vpMath::sign(end[1].get_j());
    si = vpMath::abs(sin(angle));
    si = si * vpMath::sign(end[1].get_i());
    for (int i=0 ; i < 3 ; i++)
    {
      P.ifloat = P.ifloat + si*sample ; P.i = (int)P.ifloat ;
      P.jfloat = P.jfloat + co*sample ; P.j = (int)P.jfloat ;
      pt.set_ij(P.ifloat,P.jfloat);
      if (vpImagePoint::distance(begin[0],pt) < threshold) break;
      if(!outOfImage(P.i, P.j, 5, rows, cols))
      {
        P.track(I,me,false);

        if (P.suppress ==0)
        {
	  list.end();
	  list.addRight(P) ;
	  endPtAdded = true;
	  if (vpDEBUG_ENABLE(3)) {
	    vpDisplay::displayCross(I, pt, 5, vpColor::blue) ;
	  }
        }
        else {
	  if (vpDEBUG_ENABLE(3)) {
	    vpDisplay::displayCross(I, pt, 10, vpColor::blue) ;
	  }
        }
      }
    }
    if (!endPtAdded) endPtFound++;
    me->range = memory_range ;
  }
  else
  {
    list.front();
    list.suppress();
  }
  if(begin != NULL) delete[] begin;
  if(end != NULL) delete[] end;
}


/*!  
  Seek the extremities of the edge thanks to a canny edge detection.
  The edge detection enable to find the points belonging to the edge.
  The any vpMesite  are initialize at this points.
  
  This method is practicle when the edge is not smooth.
  
  \note To use the canny detection, OpenCV has to be installed.

  \param I : Image in which the edge appears.
*/
void
vpMeNurbs::seekExtremitiesCanny(vpImage<unsigned char> &I)
{
#ifdef VISP_HAVE_OPENCV
  vpMeSite pt = list.firstValue();
  vpImagePoint firstPoint(pt.ifloat,pt.jfloat);
  pt = list.lastValue();
  vpImagePoint lastPoint(pt.ifloat,pt.jfloat);
  if (beginPtFound >=3 && farFromImageEdge(I, firstPoint))
  {
    vpImagePoint *begin = NULL;
    begin = nurbs.computeCurveDersPoint(0.0,1);
    vpImage<unsigned char> Isub(32,32); //Sub image.
    vpImagePoint topLeft(begin[0].get_i()-15,begin[0].get_j()-15); 
    vpRect rect(topLeft,32,32);
    
    vpDisplay::displayRectangle(I,rect,vpColor::green);
    
    vpImageTools::createSubImage(I,rect,Isub);
    
    vpImagePoint lastPtInSubIm(begin[0]);
    double u = 0.0;
    double step =0.0001;
    //Find the point of the nurbs closest from the edge of the subImage and in the subImage.
    while (inRectangle(lastPtInSubIm,rect) && u < 1)
    {
      u += step;
      lastPtInSubIm = nurbs.computeCurvePoint(u);
    }

    u -= step;
    if( u > 0)
      lastPtInSubIm = nurbs.computeCurvePoint(u);
    
    IplImage* Ip = NULL;
    vpImageConvert::convert(Isub, Ip);
    

    IplImage* dst = cvCreateImage( cvSize(Isub.getWidth(),Isub.getHeight()), 8, 1 );
    cvCanny( Ip, dst, cannyTh1, cannyTh2, 3 );
    
    vpImageConvert::convert(dst, Isub);
    
    vpImagePoint firstBorder(-1,-1);
    
    firstBorder = findFirstBorder(Isub, lastPtInSubIm-topLeft);
    
    vpList<vpImagePoint> ip_edges_list;
    if (firstBorder != vpImagePoint(-1, -1))
    {
      unsigned int dir;
      if (firstBorder.get_i() == 0) dir = 4;
      else if (firstBorder.get_i() == Isub.getHeight()-1) dir = 0;
      else if (firstBorder.get_j() == 0) dir = 2;
      else if (firstBorder.get_j() == Isub.getWidth()-1) dir = 6;
      computeFreemanChainElement(Isub, firstBorder , dir);
      unsigned int firstDir = dir;
      ip_edges_list.addRight( firstBorder );
      vpImagePoint border(firstBorder);
      vpImagePoint dBorder;
      do 
      {
	computeFreemanParameters(dir, dBorder);
	border = border + dBorder;	
	vpDisplay::displayPoint(I, border+topLeft, vpColor::orange) ;

	ip_edges_list.addRight( border );
	
	computeFreemanChainElement(Isub, border , dir);
      } while( (border != firstBorder || dir != firstDir) && isInImage(Isub,border) );
    }
    
    if (findCenterPoint(&ip_edges_list))
    {
      list.front();
      vpMeSite s;
      while(!list.outside())
      {
	s = list.value() ;
	vpImagePoint iP(s.ifloat,s.jfloat);
	if (inRectangle(iP,rect))
	  list.suppress() ;
	else
	  break;
      }
      
      list.front();
      ip_edges_list.front();
      double convlt;
      double delta;
      int nbr = 0;
      vpList<vpMeSite> addedPt;
      while (!ip_edges_list.outside())
      {
	s = list.value();
	vpImagePoint iPtemp = ip_edges_list.value() + topLeft;
	vpMeSite pix;
	pix.init(iPtemp.get_i(), iPtemp.get_j(), delta);
	dist = vpMeSite::sqrDistance(s,pix);
	if (dist >= vpMath::sqr(me->sample_step)/*25*/)
	{
	  bool exist = false;
	  addedPt.front();
	  while (!addedPt.outside())
	  {
	    dist = vpMeSite::sqrDistance(pix, addedPt.value());
	    if (dist < vpMath::sqr(me->sample_step)/*25*/)
	      exist = true;
	    addedPt.next();
	  }
	  if (!exist)
	  {
	    findAngle(I, iPtemp, me, delta, convlt);
	    pix.init(iPtemp.get_i(), iPtemp.get_j(), delta, convlt);
	    pix.setDisplay(selectDisplay);
	    list.addLeft(pix);
	    addedPt.front();
	    addedPt.lastValue();
	    addedPt.addRight(pix);
	    nbr++;
	    //std::cout << ip_edges_list.value() << std::endl;
	  }
	  
	}
	ip_edges_list.next();
      }
      
      int  memory_range = me->range ;
      me->range = 3 ;
      list.front();
      for (int j = 0; j < nbr; j++)
      {
	s = list.value();
        s.track(I,me,false);
	list.modify(s);
	list.next();
      }
      me->range = memory_range;
    }
    
    if (begin != NULL) delete[] begin;
    beginPtFound = 0;
  }

  if(endPtFound >= 3 && farFromImageEdge(I, lastPoint))
  {
    vpImagePoint *end = NULL;
    end = nurbs.computeCurveDersPoint(1.0,1);

    vpImage<unsigned char> Isub(32,32); //Sub image.
    vpImagePoint topLeft(end[0].get_i()-15,end[0].get_j()-15); 
    vpRect rect(topLeft,32,32);
    
    vpDisplay::displayRectangle(I,rect,vpColor::green);
    
    vpImageTools::createSubImage(I,rect,Isub);
    
    vpImagePoint lastPtInSubIm(end[0]);
    double u = 1.0;
    double step =0.0001;
    //Find the point of the nurbs closest from the edge of the subImage and in the subImage.
    while (inRectangle(lastPtInSubIm,rect) && u > 0)
    {
      u -= step;
      lastPtInSubIm = nurbs.computeCurvePoint(u);
    }

    u += step;
    if( u < 1.0)
      lastPtInSubIm = nurbs.computeCurvePoint(u);
    
    IplImage* Ip = NULL;
    vpImageConvert::convert(Isub, Ip);
    

    IplImage* dst = cvCreateImage( cvSize(Isub.getWidth(),Isub.getHeight()), 8, 1 );
    cvCanny( Ip, dst, cannyTh1, cannyTh2, 3 );
    
    vpImageConvert::convert(dst, Isub);
    
    vpImagePoint firstBorder(-1,-1);
    
    firstBorder = findFirstBorder(Isub, lastPtInSubIm-topLeft);
    
    vpList<vpImagePoint> ip_edges_list;
    if (firstBorder != vpImagePoint(-1, -1))
    {
      unsigned int dir;
      if (firstBorder.get_i() == 0) dir = 4;
      else if (firstBorder.get_i() == Isub.getHeight()-1) dir = 0;
      else if (firstBorder.get_j() == 0) dir = 2;
      else if (firstBorder.get_j() == Isub.getWidth()-1) dir = 6;
      computeFreemanChainElement(Isub, firstBorder , dir);
      unsigned int firstDir = dir;
      ip_edges_list.addRight( firstBorder );
      vpImagePoint border(firstBorder);
      vpImagePoint dBorder;
      do 
      {
	computeFreemanParameters(dir, dBorder);
	border = border + dBorder;
	vpDisplay::displayPoint(I, border+topLeft, vpColor::orange) ;

	ip_edges_list.addRight( border );
	
	computeFreemanChainElement(Isub, border , dir);
      } while( (border != firstBorder || dir != firstDir) && isInImage(Isub,border) );
    }
    
    if (findCenterPoint(&ip_edges_list))
    {
      list.end();
      vpMeSite s;
      while(!list.outside())
      {
	s = list.value() ;
	vpImagePoint iP(s.ifloat,s.jfloat);
	if (inRectangle(iP,rect))
	{
	  list.suppress() ;
	  list.end();
	}
	else
	  break;
      }
      
      list.end();
      ip_edges_list.front();
      double convlt;
      double delta;
      int nbr = 0;
      vpList<vpMeSite> addedPt;
      while (!ip_edges_list.outside())
      {
	s = list.value();
	vpImagePoint iPtemp = ip_edges_list.value() + topLeft;
	vpMeSite pix;
	pix.init(iPtemp.get_i(), iPtemp.get_j(), 0);
	dist = vpMeSite::sqrDistance(s,pix);
	if (dist >= vpMath::sqr(me->sample_step))
	{
	  bool exist = false;
	  addedPt.front();
	  while (!addedPt.outside())
	  {
	    dist = vpMeSite::sqrDistance(pix, addedPt.value());
	    if (dist < vpMath::sqr(me->sample_step))
	      exist = true;
	    addedPt.next();
	  }
	  if (!exist)
	  {
	    findAngle(I, iPtemp, me, delta, convlt);
	    pix.init(iPtemp.get_i(), iPtemp.get_j(), delta, convlt);
	    pix.setDisplay(selectDisplay);
	    list.addRight(pix);
	    addedPt.end();
	    //addedPt.lastValue();
	    addedPt.addRight(pix);
	    nbr++;
	  }
	}
	ip_edges_list.next();
      }
      
      int  memory_range = me->range ;
      me->range = 3 ;
      list.end();
      for (int j = 0; j < nbr; j++)
      {
	s = list.value();
        s.track(I,me,false);
	list.modify(s);
	list.previous();
      }
      me->range = memory_range;
    }
    
    if (end != NULL) delete[] end;
    endPtFound = 0;
  }
#else
  vpTRACE("To use the canny detection, OpenCV has to be installed.");
#endif
}


/*!
  Resample the edge if the number of sample is less than 70% of the
  expected value.
	
  \note The expected value is computed thanks to the length of the
  nurbs and the parameter which indicates the number of pixel between
  two points (vpMe::sample_step).

  \param I : Image in which the edge appears.
*/
void
vpMeNurbs::reSample(vpImage<unsigned char> &I)
{
  int n = numberOfSignal();
  double nbPt = floor(dist / me->sample_step);

  if ((double)n < 0.7*nbPt)
  {
    std::cout <<"Resample"<< std::cout;
    sample(I);
    vpMeTracker::initTracking(I);
  }
}


/*!
  Resample a part of the edge if two vpMeSite are too far from eachother.
  In this case the method try to initialize any vpMeSite between the two points.

  \param I : Image in which the edge appears.
*/
void
vpMeNurbs::localReSample(vpImage<unsigned char> &I)
{
  int rows = I.getHeight() ;
  int cols = I.getWidth() ;
  vpImagePoint* iP = NULL;
  
  int n = numberOfSignal();
  
  list.front();
  
  int range_tmp = me->range;
  me->range=2;

  while(!list.nextOutside() && n <= me->points_to_track)
  {
    vpMeSite s = list.value() ;//current reference pixel
    vpMeSite s_next = list.nextValue() ;//current reference pixel
    
    double d = vpMeSite::sqrDistance(s,s_next);
    if(d > 4 * vpMath::sqr(me->sample_step) && d < 1600)
    {
      vpImagePoint iP0(s.ifloat,s.jfloat);
      vpImagePoint iPend(s_next.ifloat,s_next.jfloat);
      vpImagePoint iP_1(s.ifloat,s.jfloat);

      double u = 0.0;
      double ubegin = 0.0;
      double uend = 0.0;
      double dmin1_1 = 1e6;
      double dmin2_1 = 1e6;
      while(u < 1)
      {
        u+=0.01;
        double dmin1 = vpImagePoint::sqrDistance(nurbs.computeCurvePoint(u),iP0);
        double dmin2 = vpImagePoint::sqrDistance(nurbs.computeCurvePoint(u),iPend);

        if (dmin1 < dmin1_1)
        {
          dmin1_1 = dmin1;
          ubegin = u;
        }

        if (dmin2 < dmin2_1)
        {
          dmin2_1 = dmin2;
          uend = u;
        }
      }
      u = ubegin;
      
      if( u != 1.0 || uend != 1.0)
      {
        iP = nurbs.computeCurveDersPoint(u, 1);
      
        while (vpImagePoint::sqrDistance(iP[0],iPend) > vpMath::sqr(me->sample_step) && u < uend)
        {     //std::cout << "Et boum !!!! " << std::endl;
	  u+=0.01;
	  if (iP!=NULL) delete[] iP;
	  iP = nurbs.computeCurveDersPoint(u, 1);
	  if ( vpImagePoint::sqrDistance(iP[0],iP_1) > vpMath::sqr(me->sample_step) && !outOfImage(iP[0], 0, rows, cols))
	  {
	    double delta = computeDelta(iP[1].get_i(),iP[1].get_j());
	    vpMeSite pix ; //= list.value();
	    pix.init(iP[0].get_i(), iP[0].get_j(), delta) ;
	    pix.setDisplay(selectDisplay) ;
	    pix.track(I,me,false);
	    if (pix.suppress == 0)
	    {
	      list.addRight(pix);
	      iP_1 = iP[0];
	    }
	  }
        }
      }
    }
    list.next();
  }
  me->range=range_tmp;
  if (iP!=NULL) delete[] iP;
}


/*!
  Suppress vpMeSites if they are too close to each other.
  
  The goal is to keep the order of the vpMeSites in the list.
*/
void
vpMeNurbs::supressNearPoints()
{
  // Loop through list of sites to track
  list.front();
  while(!list.nextOutside())
  {
    vpMeSite s = list.value() ;//current reference pixel
    vpMeSite s_next = list.nextValue() ;//current reference pixel
    
    if(vpMeSite::sqrDistance(s,s_next) < vpMath::sqr(me->sample_step))
    {
      s_next.suppress = 4;
      list.next();
      list.modify(s_next);
      if (!list.nextOutside()) list.next();
    }
    else
      list.next() ;
  }
}


/*!
  Track the edge in the image I.

  \param I : Image in which the edge appears.
*/
void
vpMeNurbs::track(vpImage<unsigned char> &I)
{
  //Tracking des vpMeSites
  vpMeTracker::track(I);
  
  //Suppress points which are too close to each other
  supressNearPoints();
  
  //Suppressions des points ejectes par le tracking
  suppressPoints();

  //Recalcule les param�tres
//  nurbs.globalCurveInterp(list);
  nurbs.globalCurveApprox(list,nbControlPoints);
  
  //On resample localement
  localReSample(I);

  seekExtremities(I);
  if(enableCannyDetection)
    seekExtremitiesCanny(I);

//   nurbs.globalCurveInterp(list);
  nurbs.globalCurveApprox(list,nbControlPoints);
  
  double u = 0.0;
  vpImagePoint pt;
  vpImagePoint pt_1;
  dist = 0;
  while (u<=1.0)
  {
    pt = nurbs.computeCurvePoint(u);
    if(u!=0)
      dist = dist + vpImagePoint::distance(pt,pt_1);
    pt_1 = pt;
    u=u+0.01;
  }

  updateDelta();

  reSample(I);
}


/*!
  Display edge.

  \warning To effectively display the edge a call to
  vpDisplay::flush() is needed.

  \param I : Image in which the edge appears.
  \param col : Color of the displayed line.

 */
void
vpMeNurbs::display(vpImage<unsigned char>&I, vpColor col)
{
  double u = 0.0;
  vpImagePoint pt;
  while (u <= 1)
  {
    pt = nurbs.computeCurvePoint(u);
    vpDisplay::displayCross(I,pt,4,col);
    u+=0.01;
  }
}


/*!
  Considering a pixel iP compute the next element of the Freeman chain
  code.

  According to the gray level of pixel iP and his eight neighbors determine
  the next element of the chain in order to turn around the dot
  counterclockwise.

  \param I : The image we are working with.
  \param iP : The current pixel.
  \param element : The next freeman element chain code (0, 1, 2, 3, 4, 5, 6, 7)
  with 0 for right moving, 2 for down, 4 for left and 6 for up moving.

  \return false if an element cannot be found. Occurs for example with an area
  constituted by a single pixel. Return true if success.
*/
bool
vpMeNurbs::computeFreemanChainElement(const vpImage<unsigned char> &I,
				   vpImagePoint &iP,
				   unsigned int &element)
{
  vpImagePoint diP;
  vpImagePoint iPtemp;
  if (hasGoodLevel( I, iP )) {
    // get the point on the right of the point passed in
    computeFreemanParameters((element + 2) %8, diP);
    iPtemp = iP + diP;
    if (hasGoodLevel( I, iPtemp )) {
      element = (element + 2) % 8;      // turn right
    }
    else {
      computeFreemanParameters((element + 1) %8, diP);
      iPtemp = iP + diP;

      if ( hasGoodLevel( I, iPtemp )) {
	element = (element + 1) % 8;      // turn diag right
      }
      else {
	computeFreemanParameters(element, diP);
        iPtemp = iP + diP;

	if ( hasGoodLevel( I, iPtemp )) {
	  element = element;      // keep same dir
	}
	else {
	  computeFreemanParameters((element + 7) %8, diP);
          iPtemp = iP + diP;

	  if ( hasGoodLevel( I, iPtemp )) {
	    element = (element + 7) %8;      // turn diag left
	  }
	  else {
	    computeFreemanParameters((element + 6) %8, diP);
            iPtemp = iP + diP;

	    if ( hasGoodLevel( I, iPtemp )) {
	      element = (element + 6) %8 ;      // turn left
	    }
	    else {
	      computeFreemanParameters((element + 5) %8, diP);
              iPtemp = iP + diP;

	      if ( hasGoodLevel( I, iPtemp )) {
		element = (element + 5) %8 ;      // turn diag down
	      }
	      else {
		computeFreemanParameters((element + 4) %8, diP);
                iPtemp = iP + diP;

		if ( hasGoodLevel( I, iPtemp )) {
		  element = (element + 4) %8 ;      // turn down
		}
		else {
		  computeFreemanParameters((element + 3) %8, diP);
                  iPtemp = iP + diP;

		  if ( hasGoodLevel( I, iPtemp )) {
		    element = (element + 3) %8 ;      // turn diag right down
		  }
		  else {
		    // No neighbor with a good level
		    //
		    return false;
		  }
		}
	      }
	    }
	  }
	}
      }
    }
  }
  else {
    return false;
  }
  return true;
}


/*!
  Check if the pixel iP is in the image and has
  a good level to belong to the edge.

  \param I : Image.
  \param iP : Pixel to test

  \return true : If the pixel iP is in the area and
  has a value greater than 0.

  \return false : Otherwise
*/
bool vpMeNurbs::hasGoodLevel(const vpImage<unsigned char>& I,
			  const vpImagePoint iP) const
{
  if( !isInImage( I, iP ) )
    return false;

  if( I(vpMath::round(iP.get_i()),vpMath::round(iP.get_j())) > 0)
  {
    return true;
  }
  else
  {
    return false;
  }
}


/*!
  Test if a pixel is in the image. Points of the border are not considered to
  be in the image.

  \param I : The image.
  \param iP : An image point.

  \return true if the image point \e iP is in the image and false
  otherwise.
*/
bool vpMeNurbs::isInImage(const vpImage<unsigned char>& I, const vpImagePoint iP) const
{
  return (iP.get_i() >= 0 && iP.get_j() >= 0 && iP.get_i() < I.getRows()  && iP.get_j() < I.getCols());
}


/*!
  Gives the displacement corresponding to the value of the parameter \e element.
  
  - If element = 0 diP = (0,1).
  - If element = 1 diP = (1,1).
  - If element = 2 diP = (1,0).
  - If element = 3 diP = (1,-1).
  - If element = 4 diP = (0,-1).
  - If element = 5 diP = (-1,-1).
  - If element = 6 diP = (-1,0).
  - If element = 7 diP = (-1,1).
  
  \param element : the element value(typically given by the method computeFreemanChainElement).
  \param diP : the output parameter which contains the displacement cooresponding to the value of \e element. 
*/
void
vpMeNurbs::computeFreemanParameters( unsigned int element, vpImagePoint &diP)
{
  /*
           5  6  7
            \ | /
             \|/ 
         4 ------- 0
             /|\
            / | \
           3  2  1
  */
  switch(element) {
  case 0: // go right
    diP.set_ij(0,1);
    break;

  case 1: // go right top
    diP.set_ij(1,1);
    break;

  case 2: // go top
    diP.set_ij(1,0);
    break;

  case 3:
    diP.set_ij(1,-1);
    break;

  case 4:
    diP.set_ij(0,-1);
    break;

  case 5:
    diP.set_ij(-1,-1);
    break;

  case 6:
    diP.set_ij(-1,0);
    break;

  case 7:
    diP.set_ij(-1,1);
    break;
  }
}


/*!
  Check if the point is far enough from the image edges
  
  \param I : The image.
  \param iP : An image point.
  
  \return true if the point iP is at least 20 pixels far from the image edeges. 
*/
bool
vpMeNurbs::farFromImageEdge(const vpImage<unsigned char> I, const vpImagePoint iP)
{
  int row = I.getRows();
  int col = I.getCols();
  return (iP.get_i() < row - 20 && iP.get_j() < col - 20 && iP.get_i() > 20 && iP.get_j() > 20);
}
