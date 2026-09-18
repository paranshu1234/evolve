#include "core/Geometry.h"
#include "core/Project.h"
#include <cmath>
#include <stdexcept>

namespace evolve {
constexpr float Pi = 3.14159265359f;
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 operator*(Vec3 v, float s) { return {v.x*s, v.y*s, v.z*s}; }
Vec3 normalized(Vec3 v) {
    float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    return len > 0.000001f ? v*(1.0f/len) : Vec3{0,1,0};
}
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 baseColor(char base) {
    switch(base) {
    case 'A': return {0.18f,0.85f,0.69f}; case 'T': return {0.96f,0.62f,0.29f};
    case 'G': return {0.48f,0.57f,1.0f}; case 'C': return {0.96f,0.39f,0.57f};
    default: throw std::invalid_argument("Invalid base in geometry.");
    }
}
namespace {
void triangle(SceneMesh& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 na, Vec3 nb, Vec3 nc, Vec3 color) {
    mesh.vertices.insert(mesh.vertices.end(), {{a,na,color},{b,nb,color},{c,nc,color}});
}
void sphere(SceneMesh& mesh, Vec3 center, float radius, Vec3 color) {
    constexpr int rings=8, slices=12;
    const auto point=[](int ring,int slice) {
        float p=Pi*static_cast<float>(ring)/rings,t=2*Pi*static_cast<float>(slice)/slices;
        return Vec3{std::sin(p)*std::cos(t),std::cos(p),std::sin(p)*std::sin(t)};
    };
    for(int r=0;r<rings;++r) for(int s=0;s<slices;++s) {
        Vec3 a=point(r,s),b=point(r+1,s),c=point(r+1,s+1),d=point(r,s+1);
        triangle(mesh,center+a*radius,center+b*radius,center+c*radius,a,b,c,color);
        triangle(mesh,center+a*radius,center+c*radius,center+d*radius,a,c,d,color);
    }
}
void cylinder(SceneMesh& mesh, Vec3 a, Vec3 b, float radius, Vec3 color) {
    Vec3 axis=normalized(b-a);
    Vec3 u=normalized(cross(axis,std::abs(axis.y)>0.95f ? Vec3{1,0,0}:Vec3{0,1,0}));
    Vec3 v=cross(axis,u);
    constexpr int sides=8;
    for(int i=0;i<sides;++i) {
        float t=2*Pi*static_cast<float>(i)/sides,n=2*Pi*static_cast<float>(i+1)/sides;
        Vec3 p=u*std::cos(t)+v*std::sin(t),q=u*std::cos(n)+v*std::sin(n);
        triangle(mesh,a+p*radius,b+p*radius,b+q*radius,p,p,q,color);
        triangle(mesh,a+p*radius,b+q*radius,a+q*radius,p,q,q,color);
    }
}
void helix(SceneMesh& mesh,const std::string& seq,const std::string& baseline,std::size_t selected,float offset,bool reference) {
    const float halfHeight=static_cast<float>(seq.size()-1)*0.31f;
    Vec3 prevA{},prevB{};
    for(std::size_t i=0;i<seq.size();++i) {
        const float angle=static_cast<float>(i)*2*Pi/10.5f;
        Vec3 a={offset+1.6f*std::cos(angle),static_cast<float>(i)*0.62f-halfHeight,1.6f*std::sin(angle)};
        Vec3 b={offset-1.6f*std::cos(angle),a.y,-a.z},mid={offset,a.y,0};
        Vec3 ca=baseColor(seq[i]),cb=baseColor(complement(seq[i]));
        if(reference) { ca=ca*0.5f; cb=cb*0.5f; }
        const bool chosen=!reference && i==selected;
        const bool changed=!reference && seq[i]!=baseline[i];
        if(chosen) { sphere(mesh,a,0.37f,{1.0f,0.94f,0.72f}); sphere(mesh,b,0.37f,{1.0f,0.94f,0.72f}); }
        else { sphere(mesh,a,0.24f,ca); sphere(mesh,b,0.24f,cb); }
        cylinder(mesh,a,mid,chosen ? 0.14f:0.09f,ca);
        cylinder(mesh,mid,b,chosen ? 0.14f:0.09f,cb);
        if(changed) sphere(mesh,mid,0.19f,{1.0f,0.83f,0.32f});
        if(i>0) {
            cylinder(mesh,prevA,a,0.075f,reference ? Vec3{0.18f,0.25f,0.31f}:Vec3{0.31f,0.64f,0.66f});
            cylinder(mesh,prevB,b,0.075f,reference ? Vec3{0.18f,0.25f,0.31f}:Vec3{0.52f,0.45f,0.70f});
        }
        if(!reference) { mesh.pickPoints.push_back({a,i}); mesh.pickPoints.push_back({b,i}); }
        prevA=a;prevB=b;
    }
}
}
SceneMesh buildHelix(const std::string& sequence,const std::string& baseline,std::size_t selected,bool compare,bool showGrid) {
    if(sequence.empty() || sequence.size()!=baseline.size() || sequence.size()>MaxBases)
        throw std::invalid_argument("Invalid helix sequence size.");
    SceneMesh mesh;
    mesh.vertices.reserve(sequence.size()*(compare ? 3000:1500));
    helix(mesh,sequence,baseline,selected,compare ? 2.8f:0.0f,false);
    if(compare) helix(mesh,baseline,baseline,selected,-2.8f,true);
    if(showGrid) {
        float y=-static_cast<float>(sequence.size()-1)*0.31f-0.8f;
        for(int i=-8;i<=8;++i) {
            float f=static_cast<float>(i);
            cylinder(mesh,{-8,y,f},{8,y,f},0.009f,{0.10f,0.18f,0.23f});
            cylinder(mesh,{f,y,-8},{f,y,8},0.009f,{0.10f,0.18f,0.23f});
        }
    }
    return mesh;
}
}
