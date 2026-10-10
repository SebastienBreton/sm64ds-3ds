#pragma once
#include <cmath>
#include <algorithm>
namespace native_mesh {
// Port stereo policy only. This does not modify the camera/view matrix.
// Called once per camera publication, never once per vertex or HELD redraw.
class Convergence {
 float value_=1.f;bool valid_=false,pending_cut_=true;
public:
 void cut(){valid_=false;pending_cut_=true;}
 void reset(){valid_=false;pending_cut_=true;value_=1.f;}
 float update(float camera_distance,float projection_w_scale){
  if(!std::isfinite(camera_distance)||!std::isfinite(projection_w_scale)||camera_distance<=0.f||projection_w_scale==0.f)return value_;
  const float target=std::clamp(camera_distance*std::fabs(projection_w_scale),.125f,256.f);
  if(!valid_||pending_cut_){value_=target;valid_=true;pending_cut_=false;}
  else value_+=std::clamp((target-value_)*.125f,-.5f,.5f);
  return value_;
 }
 bool valid()const{return valid_;}
 float value()const{return value_;}
};
}
