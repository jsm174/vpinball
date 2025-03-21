// license:GPLv3+

#pragma once

#include <cfloat>

// 2D vector
class alignas(8) Vertex2D
{
public:
   float x;
   float y;

   constexpr Vertex2D() {}
   constexpr Vertex2D(const float _x, const float _y) : x(_x), y(_y) {}

   void Set(const float a, const float b) { x = a; y = b; }
   void SetZero() { Set(0.f, 0.f); }

   Vertex2D operator+ (const Vertex2D& v) const
   {
      return {x + v.x, y + v.y};
   }
   Vertex2D operator- (const Vertex2D& v) const
   {
      return {x - v.x, y - v.y};
   }
   Vertex2D operator- () const
   {
      return {-x, -y};
   }

   Vertex2D& operator+= (const Vertex2D& v)
   {
      x += v.x;
      y += v.y;
      return *this;
   }
   Vertex2D& operator-= (const Vertex2D& v)
   {
      x -= v.x;
      y -= v.y;
      return *this;
   }

   Vertex2D operator* (const float s) const
   {
      return {s*x, s*y};
   }
   friend Vertex2D operator* (const float s, const Vertex2D& v)
   {
      return {s*v.x, s*v.y};
   }
   Vertex2D operator/ (const float s) const
   {
      const float invs = 1.0f / s;
      return {x*invs, y*invs};
   }

   Vertex2D& operator*= (const float s)
   {
      x *= s;
      y *= s;
      return *this;
   }
   Vertex2D& operator/= (const float s)
   {
      const float invs = 1.0f / s;
      x *= invs;
      y *= invs;
      return *this;
   }

   float Dot(const Vertex2D &pv) const
   {
      return x*pv.x + y*pv.y;
   }

   float LengthSquared() const
   {
      return x*x + y*y;
   }

   float Length() const
   {
      return sqrtf(x*x + y*y);
   }

   void Normalize()
   {
      const float oneoverlength = 1.0f / Length();
      x *= oneoverlength;
      y *= oneoverlength;
   }

   void NormalizeSafe()
   {
      const float lengthsqr = x*x + y*y;
      if (lengthsqr <= FLT_MIN)
         return;
      const float oneoverlength = 1.0f / sqrtf(lengthsqr);
      x *= oneoverlength;
      y *= oneoverlength;
   }

   bool IsZero() const
   {
      return fabsf(x) <= FLT_MIN && fabsf(y) <= FLT_MIN;
   }
};


// 3D vector
class Vertex3Ds
{
public:
   float x, y, z;

   constexpr Vertex3Ds() {}
   constexpr Vertex3Ds(const float _x, const float _y, const float _z) : x(_x), y(_y), z(_z) {}

   void Set(const float a, const float b, const float c) { x = a; y = b; z = c; }
   void SetZero() { Set(0.f, 0.f, 0.f); }

   Vertex3Ds operator+ (const Vertex3Ds& v) const
   {
      return {x + v.x, y + v.y, z + v.z};
   }
   Vertex3Ds operator- (const Vertex3Ds& v) const
   {
      return {x - v.x, y - v.y, z - v.z};
   }
   Vertex3Ds operator- () const
   {
      return {-x, -y, -z};
   }

   Vertex3Ds& operator+= (const Vertex3Ds& v)
   {
      x += v.x;
      y += v.y;
      z += v.z;
      return *this;
   }
   Vertex3Ds& operator-= (const Vertex3Ds& v)
   {
      x -= v.x;
      y -= v.y;
      z -= v.z;
      return *this;
   }

   Vertex3Ds operator* (const float s) const
   {
      return {s*x, s*y, s*z};
   }
   friend Vertex3Ds operator* (const float s, const Vertex3Ds& v)
   {
      return {s*v.x, s*v.y, s*v.z};
   }
   Vertex3Ds operator/ (const float s) const
   {
      const float invs = 1.0f / s;
      return {x*invs, y*invs, z*invs};
   }

   Vertex3Ds& operator*= (const float s)
   {
      x *= s;
      y *= s;
      z *= s;
      return *this;
   }
   Vertex3Ds& operator/= (const float s)
   {
      const float invs = 1.0f / s;
      x *= invs;
      y *= invs;
      z *= invs;
      return *this;
   }

   bool operator==(const Vertex3Ds& v) const { return (x==v.x) && (y==v.y) && (z==v.z); }

   void Normalize()
   {
      const float oneoverlength = 1.0f / sqrtf(x*x + y*y + z*z);
      x *= oneoverlength;
      y *= oneoverlength;
      z *= oneoverlength;
   }
   void Normalize(const float scalar)
   {
      const float oneoverlength = scalar / sqrtf(x*x + y*y + z*z);
      x *= oneoverlength;
      y *= oneoverlength;
      z *= oneoverlength;
   }

   void NormalizeSafe()
   {
      const float lengthsqr = x*x + y*y + z*z;
      if (lengthsqr <= FLT_MIN)
         return;
      const float oneoverlength = 1.0f / sqrtf(lengthsqr);
      x *= oneoverlength;
      y *= oneoverlength;
      z *= oneoverlength;
   }

   template <class VecIn> float Dot(const VecIn& pv) const
   {
      return x*pv.x + y*pv.y + z*pv.z;
   }

   float LengthSquared() const
   {
      return x*x + y*y + z*z;
   }

   float Length() const
   {
      return sqrtf(x*x + y*y + z*z);
   }

   bool IsZero() const
   {
      return fabsf(x) <= FLT_MIN && fabsf(y) <= FLT_MIN && fabsf(z) <= FLT_MIN;
   }

   // access the x/y components as a 2D vector
   Vertex2D& xy()               { return *(reinterpret_cast<Vertex2D*>(&x)); }
   const Vertex2D& xy() const   { return *(reinterpret_cast<const Vertex2D*>(&x)); }
};

#define vec3 Vertex3Ds

inline Vertex3Ds CrossProduct(const Vertex3Ds &pv1, const Vertex3Ds &pv2)
{
   return {pv1.y * pv2.z - pv1.z * pv2.y,
           pv1.z * pv2.x - pv1.x * pv2.z,
           pv1.x * pv2.y - pv1.y * pv2.x};
}

inline Vertex3Ds GetRotatedAxis(const float angle, const Vertex3Ds &axis, const Vertex3Ds &temp)
{
   Vertex3Ds u = axis;
   u.Normalize();

   const float sinAngle = sinf((float)(M_PI / 180.0)*angle);
   const float cosAngle = cosf((float)(M_PI / 180.0)*angle);
   const float oneMinusCosAngle = 1.0f - cosAngle;

   Vertex3Ds rotMatrixRow0, rotMatrixRow1, rotMatrixRow2;

   rotMatrixRow0.x = u.x*u.x + cosAngle*(1.f - u.x*u.x);
   rotMatrixRow0.y = u.x*u.y*oneMinusCosAngle - sinAngle*u.z;
   rotMatrixRow0.z = u.x*u.z*oneMinusCosAngle + sinAngle*u.y;

   rotMatrixRow1.x = u.x*u.y*oneMinusCosAngle + sinAngle*u.z;
   rotMatrixRow1.y = u.y*u.y + cosAngle*(1.f - u.y*u.y);
   rotMatrixRow1.z = u.y*u.z*oneMinusCosAngle - sinAngle*u.x;

   rotMatrixRow2.x = u.x*u.z*oneMinusCosAngle - sinAngle*u.y;
   rotMatrixRow2.y = u.y*u.z*oneMinusCosAngle + sinAngle*u.x;
   rotMatrixRow2.z = u.z*u.z + cosAngle*(1.f - u.z*u.z);

   return {temp.Dot(rotMatrixRow0), temp.Dot(rotMatrixRow1), temp.Dot(rotMatrixRow2)};
}

void RotateAround(const Vertex3Ds &pvAxis, Vertex3D_NoTex2 * const pvPoint, int count, float angle);
void RotateAround(const Vertex3Ds &pvAxis, Vertex3Ds * const pvPoint, int count, float angle);
Vertex3Ds RotateAround(const Vertex3Ds &pvAxis, const Vertex2D &pvPoint, float angle);

// uniformly distributed vector over sphere
inline Vertex3Ds sphere_sample(const float u, const float v) // u,v in [0..1), poles on y-axis
{
   const float phi = v * (float)(2.0 * M_PI);
   const float z = 1.0f - (u + u);
   const float r = sqrtf(1.0f - z * z);
   return {cosf(phi) * r, z, sinf(phi) * r};
}

// uniformly distributed vector over hemisphere
inline Vertex3Ds hemisphere_sample(const float u, const float v) // u,v in [0..1), returns y-up
{
   const float phi = v * (float)(2.0 * M_PI);
   const float cosTheta = 1.0f - u;
   const float sinTheta = sqrtf(1.0f - cosTheta * cosTheta);
   return {cosf(phi) * sinTheta, cosTheta, sinf(phi) * sinTheta};
}

// cosine distributed vector over hemisphere
inline Vertex3Ds cos_hemisphere_sample(const float u, const float v) // u,v in [0..1), returns y-up
{
   const float phi = v * (float)(2.0 * M_PI);
   const float cosTheta = sqrtf(1.0f - u);
   const float sinTheta = sqrtf(u);
   return {cosf(phi) * sinTheta, cosTheta, sinf(phi) * sinTheta};
}

// rotate vec from world space (y-up, upper hemisphere only) to local space (defined f.e. by the normal of a surface and its implicit/orthogonal tangents),
// can be useful for orienting a hemisphere to a normal and the like (only if orientation of tangents can be arbitrary -> e.g. AO, diffuse) (see Moeller/Hughes)
inline Vertex3Ds rotate_to_vector_upper(const Vertex3Ds &vec, const Vertex3Ds &normal)
{
   /*const float c = Vertex3Ds(0,1,0).Dot(normal);
   if(fabsf(c) < 0.999f)
   {
   const Vertex3Ds v = CrossProduct(Vertex3Ds(0,1,0),normal);
   const float h = (1.0f-c)/(v.Dot(v));
   return mul(vec, Vertex3Ds(c+h*v.xyz.x*v.xyz.x, h*v.xyz.x*v.xyz.y+v.xyz.z, h*v.xyz.x*v.xyz.z-v.xyz.y),
   Vertex3Ds(h*v.xyz.x*v.xyz.y-v.xyz.z, c+h*v.xyz.y*v.xyz.y, h*v.xyz.y*v.xyz.z+v.xyz.x),
   Vertex3Ds(h*v.xyz.x*v.xyz.z+v.xyz.y, h*v.xyz.y*v.xyz.z-v.xyz.x, c+h*v.xyz.z*v.xyz.z));
   }
   else
   return (c < 0.0f) ? -vec : vec;*/
   if (normal.y > -0.99999f)
   {
      const float h = 1.0f / (1.0f + normal.y);
      const float hz = h*normal.z;
      const float hzx = hz*normal.x;
      return {vec.x * (normal.y + hz*normal.z) + vec.y * normal.x - vec.z * hzx,
              vec.y * normal.y - vec.x * normal.x - vec.z * normal.z,
              vec.y * normal.z - vec.x * hzx + vec.z * (normal.y + h*normal.x*normal.x)};
   }
   else
      return -vec;
}

// rotate vec from world space (y-up, full sphere) to local space (defined f.e. by the normal of a surface and its implicit/orthogonal tangents),
// can be useful for orienting a hemisphere to a normal and the like (only if orientation of tangents can be arbitrary -> e.g. AO, diffuse) (see Moeller/Hughes)
inline Vertex3Ds rotate_to_vector_full(const Vertex3Ds &vec, const Vertex3Ds &normal)
{
   /*const float c = Vertex3Ds(0,1,0).Dot(normal);
   if(fabsf(c) < 0.999f)
   {
   const Vertex3Ds v = CrossProduct(Vertex3Ds(0,1,0),normal);
   const float h = (1.0f-c)/(v.Dot(v));
   return mul(vec, Vertex3Ds(c+h*v.xyz.x*v.xyz.x, h*v.xyz.x*v.xyz.y+v.xyz.z, h*v.xyz.x*v.xyz.z-v.xyz.y),
   Vertex3Ds(h*v.xyz.x*v.xyz.y-v.xyz.z, c+h*v.xyz.y*v.xyz.y, h*v.xyz.y*v.xyz.z+v.xyz.x),
   Vertex3Ds(h*v.xyz.x*v.xyz.z+v.xyz.y, h*v.xyz.y*v.xyz.z-v.xyz.x, c+h*v.xyz.z*v.xyz.z));
   }
   else
   return (c < 0.0f) ? -vec : vec;*/
   if (fabsf(normal.y) <= 0.99999f)
   {
      const float xx = normal.x*normal.x;
      const float zz = normal.z*normal.z;
      const float h = (1.0f - normal.y) / (xx + zz);
      const float hzx = h*normal.z*normal.x;
      return {vec.x * (normal.y + h*zz) + vec.y * normal.x - vec.z * hzx,
              vec.y * normal.y - vec.x * normal.x - vec.z * normal.z,
              vec.y * normal.z - vec.x * hzx + vec.z * (normal.y + h*xx)};
   }
   else
      return (normal.y < 0.0f) ? -vec : vec;
}

class alignas(16) Vertex4D final
{
public:
    float x, y, z, w;

    constexpr Vertex4D() {}
    constexpr Vertex4D(const float _x, const float _y, const float _z, const float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

#define vec4 Vertex4D

class bool4 final
{
public:
    union
    {
        struct {
            bool x, y, z, w;
        };
        unsigned int xyzw;
    };

    constexpr bool4() {}
    constexpr bool4(const bool _x, const bool _y, const bool _z, const bool _w) : x(_x), y(_y), z(_z), w(_w) {}
};

class bool2 final
{
public:
    bool x, y;

    constexpr bool2() {}
    constexpr bool2(const bool _x, const bool _y) : x(_x), y(_y) {}
};

class alignas(8) int2 final
{
public:
    int x, y;

    constexpr int2() {}
    constexpr int2(const int _x, const int _y) : x(_x), y(_y) {}
};

class short2 final
{
public:
    short x, y;

    constexpr short2() {}
    constexpr short2(const short _x, const short _y) : x(_x), y(_y) {}
};

namespace plog
{
Record& operator<<(Record& record, const Vertex2D& pt);
Record& operator<<(Record& record, const Vertex3Ds& pt);
Record& operator<<(Record& record, const Vertex4D& pt);
}
