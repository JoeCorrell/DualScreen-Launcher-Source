#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <algorithm>

namespace ac2ds::preview {
using Matrix=std::array<double,16>;
using Constants=std::array<float,16>;
using Point=std::array<double,3>;
inline Matrix Multiply(const Matrix& a,const Matrix& b){
    Matrix result{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)result[r*4+c]+=a[r*4+k]*b[k*4+c];
    return result;
}
inline std::optional<Matrix> Inverse(const Matrix& source){
    double rows[4][8]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        if(!std::isfinite(source[r*4+c]))return {};
        rows[r][c]=source[r*4+c];rows[r][c+4]=r==c?1:0;
    }
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;
        for(unsigned r=c+1;r<4;++r)if(std::abs(rows[r][c])>std::abs(rows[pivot][c]))pivot=r;
        if(std::abs(rows[pivot][c])<1e-12)return {};
        for(unsigned i=0;i<8;++i)std::swap(rows[pivot][i],rows[c][i]);
        double divisor=rows[c][c];for(double& value:rows[c])value/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=c){double factor=rows[r][c];for(unsigned i=0;i<8;++i)rows[r][i]-=factor*rows[c][i];}
    }
    Matrix result{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){result[r*4+c]=rows[r][c+4];if(!std::isfinite(result[r*4+c]))return {};}
    return result;
}
inline double Dot(const Point& a,const Point& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Point Cross(const Point& a,const Point& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline std::optional<Point> Normalize(Point point){
    double size=std::sqrt(Dot(point,point));if(!std::isfinite(size) || size<1e-8)return {};
    for(double& value:point)value/=size;return point;
}
// Independent preview camera; never modifies the gameplay camera. Coordinates
// must come from a verified current transform, with the game's Z-up convention.
inline std::optional<Matrix> Camera(Point eye,Point target,double aspect,double fovRadians=.61){
    for(double value:eye)if(!std::isfinite(value) || std::abs(value)>1e7)return {};
    for(double value:target)if(!std::isfinite(value) || std::abs(value)>1e7)return {};
    if(!std::isfinite(aspect) || aspect<.1 || aspect>10 || !std::isfinite(fovRadians) || fovRadians<.1 || fovRadians>2.8)return {};
    auto z=Normalize({target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]});if(!z)return {};
    auto x=Normalize(Cross({0,0,1},*z));if(!x)return {};
    auto y=Cross(*z,*x);
    Matrix view{x->at(0),y[0],z->at(0),0,x->at(1),y[1],z->at(1),0,x->at(2),y[2],z->at(2),0,
        -Dot(*x,eye),-Dot(y,eye),-Dot(*z,eye),1};
    constexpr double nearPlane=.05,farPlane=1000;
    double scale=1/std::tan(fovRadians/2),depth=farPlane/(farPlane-nearPlane);
    Matrix projection{scale/aspect,0,0,0,0,scale,0,0,0,0,depth,1,0,0,-nearPlane*depth,0};
    return Multiply(view,projection);
}
// b35dd0 -> b35790 transposes the matrix. b34b50 maps semantic95 to c0;
// b11a20 uploads four vertex registers. c05e60 supplies identity world matrix
// for the skinned path; its captured c0..3 therefore supply the pass camera.
inline Matrix FromConstants(const Constants& constants){
    Matrix result{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)result[r*4+c]=constants[c*4+r];return result;
}
inline std::optional<Constants> Reframe(const Constants& drawMatrix,const Constants& passCamera,const Matrix& camera){
    auto inverse=Inverse(FromConstants(passCamera));if(!inverse)return {};
    auto result=Multiply(Multiply(FromConstants(drawMatrix),*inverse),camera);Constants constants{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        double value=result[r*4+c];if(!std::isfinite(value) || std::abs(value)>1e12)return {};
        constants[c*4+r]=float(value);
    }
    return constants;
}
}
