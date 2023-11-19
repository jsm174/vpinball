#pragma once

#include "gpuprofiler.h"
#include "renderer/RenderDevice.h"
#include "renderer/Texture.h"
#include "backGlass.h"

class ModelViewProj
{
public:
   enum FlipMode { NONE, FLIPX, FLIPY };
   ModelViewProj(const unsigned int nEyes = 1) : m_nEyes(nEyes) {}

   void SetFlip(const FlipMode flip) { m_dirty = true; m_flip = flip; }
   void SetModel(const Matrix3D& Model) { MarkDirty(Model, m_matModel); m_matModel = Model; }
   void SetView(const Matrix3D& view) { MarkDirty(view, m_matView); m_matView = view; }
   void SetProj(const unsigned int index, const Matrix3D& proj) { MarkDirty(proj, m_matProj[index]); m_matProj[index] = proj; }

   const Matrix3D& GetModel() const { return m_matModel; }
   const Matrix3D& GetView() const { return m_matView; }
   const Matrix3D& GetProj(const unsigned int eye) const { return m_matProj[eye]; }
   const Matrix3D& GetModelView() const { Update(); return m_matModelView; }
   const Matrix3D& GetModelViewInverse() const { Update(); return m_matModelViewInverse; }
   const Matrix3D& GetModelViewInverseTranspose() const { Update(); return m_matModelViewInverseTranspose; }
   const Matrix3D& GetModelViewProj(const unsigned int eye) const { Update(); return m_matModelViewProj[eye]; }
   const Vertex3Ds& GetViewVec() const { Update(); return m_viewVec; }

   const unsigned int m_nEyes;

private:
   void MarkDirty(const Matrix3D& newMat, const Matrix3D& oldMat)
   {
      if (!m_dirty)
         m_dirty = memcmp(oldMat.m, newMat.m, 4 * 4 * sizeof(float)) != 0;
   }

   void Update() const
   {
      if (m_dirty)
      {
         m_dirty = false;
         m_matModelView = m_matModel * m_matView;
         memcpy(m_matModelViewInverse.m, m_matModelView.m, 4 * 4 * sizeof(float));
         m_matModelViewInverse.Invert();
         memcpy(m_matModelViewInverseTranspose.m, m_matModelViewInverse.m, 4 * 4 * sizeof(float));
         m_matModelViewInverseTranspose.Transpose();
         switch (m_flip)
         {
         case NONE:
         {
            for (unsigned int eye = 0; eye < m_nEyes; eye++)
               m_matModelViewProj[eye] = m_matModelView * m_matProj[eye];
            break;
         }
         case FLIPX:
         {
            static const Matrix3D flipx(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
            for (unsigned int eye = 0; eye < m_nEyes; eye++)
               m_matModelViewProj[eye] = m_matModelView * m_matProj[eye] * flipx;
            break;
         }
         case FLIPY:
         {
            static const Matrix3D flipy(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
            for (unsigned int eye = 0; eye < m_nEyes; eye++)
               m_matModelViewProj[eye] = m_matModelView * m_matProj[eye] * flipy;
            break;
         }
         }
         Matrix3D temp, viewRot;
         temp = m_matView;
         temp.Invert();
         temp.GetRotationPart(viewRot);
         viewRot.MultiplyVector(Vertex3Ds(0, 0, 1), m_viewVec);
         m_viewVec.NormalizeSafe();
      }
   }

   Matrix3D m_matModel;
   Matrix3D m_matView;
   Matrix3D m_matProj[6];

   mutable bool m_dirty = true;
   FlipMode m_flip = NONE;
   mutable Matrix3D m_matModelView;
   mutable Matrix3D m_matModelViewInverse;
   mutable Matrix3D m_matModelViewInverseTranspose;
   mutable Matrix3D m_matModelViewProj[6];
   mutable Vertex3Ds m_viewVec;
};

enum VRPreviewMode
{
   VRPREVIEW_DISABLED,
   VRPREVIEW_LEFT,
   VRPREVIEW_RIGHT,
   VRPREVIEW_BOTH
};

class Pin3D
{
public:
   Pin3D(PinTable* const table, const bool fullScreen, const int width, const int height, const int colordepth, int& refreshrate, VideoSyncMode& syncMode, const float AAfactor, const StereoMode stereo3D);
   ~Pin3D();

   void InitLayout(const float xpixoff = 0.f, const float ypixoff = 0.f);

   void TransformVertices(const Vertex3D_NoTex2 * const __restrict rgv, const WORD * const __restrict rgi, const int count, Vertex2D * const __restrict rgvout) const;
   void TransformVertices(const Vertex3Ds* const __restrict rgv, const WORD* const __restrict rgi, const int count, Vertex2D* const __restrict rgvout) const;

   Vertex3Ds Unproject(const Vertex3Ds& point);
   Vertex3Ds Get3DPointFrom2D(const POINT& p);

   void PrepareFrame();

   void DrawBackground();

   ModelViewProj& GetMVP() { return *m_mvp; }
   const ModelViewProj& GetMVP() const { return *m_mvp; }

   void InitLights();

   BackGlass* m_backGlass = nullptr;

   void UpdateBasicShaderMatrix(const Matrix3D& objectTrafo = Matrix3D::MatrixIdentity());
   void UpdateBallShaderMatrix();

   // Ball rendering

   vector<Light*> m_ballReflectedLights;
   MeshBuffer* m_ballMeshBuffer = nullptr;
   MeshBuffer* m_ballTrailMeshBuffer = nullptr;
#ifdef DEBUG_BALL_SPIN
   MeshBuffer* m_ballDebugPoints = nullptr;
#endif
   int m_ballTrailMeshBufferPos = 0;
   bool m_trailForBalls = false;
   float m_ballTrailStrength = 0.5f;
   bool m_disableLightingForBalls = false;
   bool m_overwriteBallImages = false;
   Texture* m_ballImage = nullptr;
   Texture* m_decalImage = nullptr;

   // Post processing
   void PrepareVideoBuffers();
   void SetScreenOffset(const float x, const float y); // set render offset in screen coordinates, e.g., for the nudge shake
   void Bloom();
   void SSRefl();
   Vertex2D m_ScreenOffset = Vertex2D(0.f, 0.f); // for screen shake effect during nudge
   bool m_bloomOff;
   bool m_ss_refl;
   int m_FXAA; // =FXAASettings
   int m_sharpen; // 0=off, 1=CAS, 2=bilateral CAS
   VRPreviewMode m_vrPreview;
   bool m_vrPreviewShrink = false;

private:
   PinTable* const m_table;

   StereoMode m_stereo3D;

   ModelViewProj* m_mvp = nullptr; // Store the active Model / View / Projection

   #ifdef ENABLE_SDL
   RenderTarget* m_envRadianceTexture = nullptr;
   #else
   BaseTexture* m_envRadianceTexture = nullptr;
   #endif

   // Data members
public:
   CGpuProfiler m_gpu_profiler;

   RenderDevice* m_pd3dPrimaryDevice = nullptr;

   Texture m_pinballEnvTexture; // loaded from Resources
   Texture m_builtinEnvTexture; // loaded from Resources
   Texture m_aoDitherTexture;   // loaded from Resources

   Texture* m_envTexture = nullptr;

   // free-camera-mode-fly-around parameters
   Vertex3Ds m_cam = Vertex3Ds(0.f, 0.f, 0.f);
   float m_inc = 0.f;

   ViewPort m_viewPort; // Viewport of the screen output (different from render size for VR, anaglyph, superscaling,...)
   float m_AAfactor;

   void UpdateBAMHeadTracking();                 // #ravarcade: UpdateBAMHeadTracking will set proj/view matrix to add BAM view and head tracking
};