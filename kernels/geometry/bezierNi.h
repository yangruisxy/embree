// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "primitive.h"
#include "bezier1i.h"

namespace embree
{
  template<int M>
    struct BezierNi
  {
    struct Type : public PrimitiveType {
      Type ();
      size_t size(const char* This) const;
      size_t getBytes(const char* This) const;
    };
    static Type type;

  public:

    /* Returns maximum number of stored primitives */
    static __forceinline size_t max_size() { return M; }

    /* Returns required number of primitive blocks for N primitives */
    static __forceinline size_t blocks(size_t N) { return (N+M-1)/M; }

    static __forceinline size_t bytes(size_t N)
    {
      const size_t f = N/M, r = N%M;
      static_assert(sizeof(BezierNi) == 21+25*M, "internal data layout issue");
      return f*sizeof(BezierNi) + (r!=0)*(21 + 25*r);
    }

  public:

    /*! Default constructor. */
    __forceinline BezierNi () {}

    const LinearSpace3fa computeAlignedSpace(Scene* scene, const PrimRef* prims, const range<size_t>& set, const Vec3fa& offset = zero, const float scale = 1.0f)
    {
      Vec3fa axisz(0,0,1);
      Vec3fa axisy(0,1,0);
      uint64_t bestGeomPrimID = -1;
      
      /*! find curve with minimum ID that defines valid direction */
      for (size_t i=set.begin(); i<set.end(); i++)
      {
        const unsigned int geomID = prims[i].geomID();
        const unsigned int primID = prims[i].primID();
        const uint64_t geomprimID = prims[i].ID64();
        if (geomprimID >= bestGeomPrimID) continue;
        NativeCurves* mesh = (NativeCurves*) scene->get(geomID);
        const unsigned vtxID = mesh->curve(primID);
        const Vec3fa v0 = (mesh->vertex(vtxID+0)-offset)*scale;
        const Vec3fa v1 = (mesh->vertex(vtxID+1)-offset)*scale;
        const Vec3fa v2 = (mesh->vertex(vtxID+2)-offset)*scale;
        const Vec3fa v3 = (mesh->vertex(vtxID+3)-offset)*scale;
        const Curve3fa curve(v0,v1,v2,v3);
        const Vec3fa p0 = curve.begin();
        const Vec3fa p3 = curve.end();
        const Vec3fa d0 = curve.eval_du(0.0f);
        //const Vec3fa d1 = curve.eval_du(1.0f);
        const Vec3fa axisz_ = normalize(p3 - p0);
        const Vec3fa axisy_ = cross(axisz_,d0);
        if (sqr_length(p3-p0) > 1E-18f) {
          axisz = axisz_;
          axisy = axisy_;
          bestGeomPrimID = geomprimID;
        }
      }

      if (sqr_length(axisy) > 1E-18) {
        axisy = normalize(axisy);
        Vec3fa axisx = normalize(cross(axisy,axisz));
        return LinearSpace3fa(axisx,axisy,axisz);
      }
      return frame(axisz);
    }

    /*! fill curve from curve list */
    __forceinline void fill(const PrimRef* prims, size_t& begin, size_t _end, Scene* scene)
    {  
      size_t end = min(begin+M,_end);
      N = end-begin;
      const unsigned int geomID0 = prims[begin].geomID();
      this->geomID(N) = geomID0;

      /* encode all primitives */
      BBox3fa bounds = empty;
      for (size_t i=0; i<N; i++)
      {
        const PrimRef& prim = prims[begin+i];
        const unsigned int geomID = prim.geomID(); assert(geomID == geomID0);
        const unsigned int primID = prim.primID();
        bounds.extend(scene->get<NativeCurves>(geomID)->bounds(primID));
      }

      /* calculate offset and scale */
      Vec3fa loffset = bounds.lower;
      float lscale = reduce_min(256.0f/(bounds.size()*sqrt(3.0f)));
      *this->offset(N) = loffset;
      *this->scale(N) = lscale;
      
      /* encode all primitives */
      for (size_t i=0; i<M && begin<end; i++, begin++)
      {
        const PrimRef& prim = prims[begin];
        const unsigned int geomID = prim.geomID();
        const unsigned int primID = prim.primID();
        const LinearSpace3fa space2 = computeAlignedSpace(scene,prims,range<size_t>(begin),loffset,lscale);
        
        const LinearSpace3fa space3(trunc(126.0f*space2.vx),trunc(126.0f*space2.vy),trunc(126.0f*space2.vz));
        const BBox3fa bounds = scene->get<NativeCurves>(geomID)->bounds(loffset,lscale,max(length(space3.vx),length(space3.vy),length(space3.vz)),space3.transposed(),primID);
        
        bounds_vx_x(N)[i] = (short) space3.vx.x;
        bounds_vx_y(N)[i] = (short) space3.vx.y;
        bounds_vx_z(N)[i] = (short) space3.vx.z;
        bounds_vx_lower(N)[i] = (short) clamp(floor(bounds.lower.x),-32767.0f,32767.0f);
        bounds_vx_upper(N)[i] = (short) clamp(ceil (bounds.upper.x),-32767.0f,32767.0f);
        assert(-32767.0f <= floor(bounds.lower.x) && floor(bounds.lower.x) <= 32767.0f);
        assert(-32767.0f <= ceil (bounds.upper.x) && ceil (bounds.upper.x) <= 32767.0f);

        bounds_vy_x(N)[i] = (short) space3.vy.x;
        bounds_vy_y(N)[i] = (short) space3.vy.y;
        bounds_vy_z(N)[i] = (short) space3.vy.z;
        bounds_vy_lower(N)[i] = (short) clamp(floor(bounds.lower.y),-32767.0f,32767.0f);
        bounds_vy_upper(N)[i] = (short) clamp(ceil (bounds.upper.y),-32767.0f,32767.0f);
        assert(-32767.0f <= floor(bounds.lower.y) && floor(bounds.lower.y) <= 32767.0f);
        assert(-32767.0f <= ceil (bounds.upper.y) && ceil (bounds.upper.y) <= 32767.0f);

        bounds_vz_x(N)[i] = (short) space3.vz.x;
        bounds_vz_y(N)[i] = (short) space3.vz.y;
        bounds_vz_z(N)[i] = (short) space3.vz.z;
        bounds_vz_lower(N)[i] = (short) clamp(floor(bounds.lower.z),-32767.0f,32767.0f);
        bounds_vz_upper(N)[i] = (short) clamp(ceil (bounds.upper.z),-32767.0f,32767.0f);
        assert(-32767.0f <= floor(bounds.lower.z) && floor(bounds.lower.z) <= 32767.0f);
        assert(-32767.0f <= ceil (bounds.upper.z) && ceil (bounds.upper.z) <= 32767.0f);
               
        this->primID(N)[i] = primID;
      }
    }

    template<typename BVH, typename Allocator>
      __forceinline static typename BVH::NodeRef createLeaf (BVH* bvh, const PrimRef* prims, const range<size_t>& set, const Allocator& alloc)
    {
      size_t start = set.begin();
      size_t items = BezierNi::blocks(set.size());
      size_t numbytes = BezierNi::bytes(set.size());
      BezierNi* accel = (BezierNi*) alloc.malloc1(numbytes,BVH::byteAlignment);
      for (size_t i=0; i<items; i++) {
        accel[i].fill(prims,start,set.end(),bvh->scene);
      }
      return bvh->encodeLeaf((char*)accel,items);
    };
    
  public:
    
    // 27.6 - 46 bytes per primitive
    unsigned char N;
    unsigned char data[4+25*M+16];

    /*
    struct Layout
    {
      unsigned int geomID;
      unsigned int primID[N];
      
      char bounds_vx_x[N];
      char bounds_vx_y[N];
      char bounds_vx_z[N];
      short bounds_vx_lower[N];
      short bounds_vx_upper[N];
      
      char bounds_vy_x[N];
      char bounds_vy_y[N];
      char bounds_vy_z[N];
      short bounds_vy_lower[N];
      short bounds_vy_upper[N];
      
      char bounds_vz_x[N];
      char bounds_vz_y[N];
      char bounds_vz_z[N];
      short bounds_vz_lower[N];
      short bounds_vz_upper[N];
      
      Vec3f offset;
      float scale;
    };
    */
    
    __forceinline       unsigned int& geomID(size_t N)       { return *(unsigned int*)((char*)this+1); }
    __forceinline const unsigned int& geomID(size_t N) const { return *(unsigned int*)((char*)this+1); }
    
    __forceinline       unsigned int* primID(size_t N)       { return (unsigned int*)((char*)this+5); }
    __forceinline const unsigned int* primID(size_t N) const { return (unsigned int*)((char*)this+5); }
    
    __forceinline       char* bounds_vx_x(size_t N)       { return (char*)((char*)this+5+4*N); }
    __forceinline const char* bounds_vx_x(size_t N) const { return (char*)((char*)this+5+4*N); }
    
    __forceinline       char* bounds_vx_y(size_t N)       { return (char*)((char*)this+5+5*N); }
    __forceinline const char* bounds_vx_y(size_t N) const { return (char*)((char*)this+5+5*N); }
    
    __forceinline       char* bounds_vx_z(size_t N)       { return (char*)((char*)this+5+6*N); }
    __forceinline const char* bounds_vx_z(size_t N) const { return (char*)((char*)this+5+6*N); }
    
    __forceinline       short* bounds_vx_lower(size_t N)       { return (short*)((char*)this+5+7*N); }
    __forceinline const short* bounds_vx_lower(size_t N) const { return (short*)((char*)this+5+7*N); }
    
    __forceinline       short* bounds_vx_upper(size_t N)       { return (short*)((char*)this+5+9*N); }
    __forceinline const short* bounds_vx_upper(size_t N) const { return (short*)((char*)this+5+9*N); }
    
    __forceinline       char* bounds_vy_x(size_t N)       { return (char*)((char*)this+5+11*N); }
    __forceinline const char* bounds_vy_x(size_t N) const { return (char*)((char*)this+5+11*N); }
    
    __forceinline       char* bounds_vy_y(size_t N)       { return (char*)((char*)this+5+12*N); }
    __forceinline const char* bounds_vy_y(size_t N) const { return (char*)((char*)this+5+12*N); }
    
    __forceinline       char* bounds_vy_z(size_t N)       { return (char*)((char*)this+5+13*N); }
    __forceinline const char* bounds_vy_z(size_t N) const { return (char*)((char*)this+5+13*N); }
    
    __forceinline       short* bounds_vy_lower(size_t N)       { return (short*)((char*)this+5+14*N); }
    __forceinline const short* bounds_vy_lower(size_t N) const { return (short*)((char*)this+5+14*N); }
    
    __forceinline       short* bounds_vy_upper(size_t N)       { return (short*)((char*)this+5+16*N); }
    __forceinline const short* bounds_vy_upper(size_t N) const { return (short*)((char*)this+5+16*N); }
    
    __forceinline       char* bounds_vz_x(size_t N)       { return (char*)((char*)this+5+18*N); }
    __forceinline const char* bounds_vz_x(size_t N) const { return (char*)((char*)this+5+18*N); }
    
    __forceinline       char* bounds_vz_y(size_t N)       { return (char*)((char*)this+5+19*N); }
    __forceinline const char* bounds_vz_y(size_t N) const { return (char*)((char*)this+5+19*N); }
    
    __forceinline       char* bounds_vz_z(size_t N)       { return (char*)((char*)this+5+20*N); }
    __forceinline const char* bounds_vz_z(size_t N) const { return (char*)((char*)this+5+20*N); }
    
    __forceinline       short* bounds_vz_lower(size_t N)       { return (short*)((char*)this+5+21*N); }
    __forceinline const short* bounds_vz_lower(size_t N) const { return (short*)((char*)this+5+21*N); }
    
    __forceinline       short* bounds_vz_upper(size_t N)       { return (short*)((char*)this+5+23*N); }
    __forceinline const short* bounds_vz_upper(size_t N) const { return (short*)((char*)this+5+23*N); }
    
    __forceinline       Vec3f* offset(size_t N)       { return (Vec3f*)((char*)this+5+25*N); }
    __forceinline const Vec3f* offset(size_t N) const { return (Vec3f*)((char*)this+5+25*N); }
    
    __forceinline       float* scale(size_t N)       { return (float*)((char*)this+5+25*N+12); }
    __forceinline const float* scale(size_t N) const { return (float*)((char*)this+5+25*N+12); }

    __forceinline       char* end(size_t N)       { return (char*)this+5+25*N+16; }
    __forceinline const char* end(size_t N) const { return (char*)this+5+25*N+16; }
  };

  template<int M>
    typename BezierNi<M>::Type BezierNi<M>::type;

  typedef BezierNi<4> Bezier4i;
  typedef BezierNi<8> Bezier8i;
}
