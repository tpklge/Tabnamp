#pragma once
#include <cmath>
#include <cstdint>
namespace tabnamp {
class Orientation {
    int candidate_=-1;uint64_t since_=0;bool landscape_=true,committed_=false;
public:
    explicit Orientation(bool landscape=true):landscape_(landscape){}
    bool update(float x,float y,float z,uint64_t now){
        float norm=x*x+y*y+z*z;
        if(!std::isfinite(norm)||norm<.55f||norm>1.6f||std::abs(z)>.75f){candidate_=-1;return false;}
        x=std::abs(x);y=std::abs(y);int next=x>.55f&&x>y*1.35f?1:y>.55f&&y>x*1.35f?0:-1;
        if(next<0){candidate_=-1;return false;}if(next!=candidate_){candidate_=next;since_=now;return false;}
        if(now-since_<500||(committed_&&landscape_==bool(next)))return false;
        landscape_=next;committed_=true;return true;
    }
    bool landscape()const{return landscape_;}
};
}
