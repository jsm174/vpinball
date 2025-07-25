// license:GPLv3+

#include "core/stdafx.h"
#include "RenderProbe.h"
#include "RenderCommand.h"
#include "Shader.h"
#include "math/bluenoise.h"

const string PLAYFIELD_REFLECTION_RENDERPROBE_NAME = "Playfield Reflections"s;

RenderProbe::~RenderProbe()
{
   assert(m_prerenderRT == nullptr && m_dynamicRT == nullptr); // RenderRelease must be call before destructor
}

int RenderProbe::GetSaveSize() const
{
   size_t size = 0;
   size += 2 * sizeof(int) + sizeof(int); // TYPE
   size += 2 * sizeof(int) + sizeof(int) + m_name.length(); // NAME
   size += 2 * sizeof(int) + sizeof(int); // RBAS
   size += 2 * sizeof(int) + sizeof(vec4); // RPLA
   size += 2 * sizeof(int) + sizeof(int); // RMOD
   size += 2 * sizeof(int) + sizeof(int); // RLMP
   size += 2 * sizeof(int); // ENDB
   return (int)size;
}

HRESULT RenderProbe::SaveData(IStream* pstm, HCRYPTHASH hcrypthash, const bool saveForUndo)
{
   BiffWriter bw(pstm, hcrypthash);
   bw.WriteInt(FID(TYPE), (int)m_type);
   bw.WriteString(FID(NAME), m_name);
   bw.WriteInt(FID(RBAS), m_roughness);
   bw.WriteStruct(FID(RPLA), (void*)&m_reflection_plane, sizeof(vec4));
   bw.WriteInt(FID(RMOD), (int)m_reflection_mode);
   bw.WriteBool(FID(RLMP), m_disableLightReflection);
   bw.WriteTag(FID(ENDB));
   return S_OK;
}

HRESULT RenderProbe::LoadData(IStream* pstm, PinTable* ppt, int version, HCRYPTHASH hcrypthash, HCRYPTKEY hcryptkey)
{
   BiffReader br(pstm, this, ppt, version, hcrypthash, hcryptkey);
   br.Load();
   return S_OK;
}

bool RenderProbe::LoadToken(const int id, BiffReader* const pbr)
{
   switch (id)
   {
   case FID(TYPE): pbr->GetInt(&m_type); break;
   case FID(NAME): pbr->GetString(m_name); break;
   case FID(RBAS): pbr->GetInt(m_roughness); break;
   case FID(RPLA): pbr->GetStruct(&m_reflection_plane, sizeof(vec4)); break;
   case FID(RMOD): pbr->GetInt(&m_reflection_mode); break;
   case FID(RLMP): pbr->GetBool(m_disableLightReflection); break;
   }
   return true;
}

void RenderProbe::SetName(const string& name)
{
   m_name = name;
}

void RenderProbe::RenderSetup(Renderer* renderer)
{
   assert(m_rd == nullptr);
   m_renderer = renderer;
   m_rd = renderer->m_renderDevice;
   m_rdState = new RenderDeviceState(m_rd);
   MarkDirty();
}

void RenderProbe::RenderRelease()
{
   assert(m_rd != nullptr);
   m_renderer = nullptr;
   m_rd = nullptr;
   delete m_rdState;
   m_rdState = nullptr;
   delete m_prerenderRT;
   m_prerenderRT = nullptr;
   delete m_dynamicRT;
   m_dynamicRT = nullptr;
   delete m_blurRT;
   m_blurRT = nullptr;
}

void RenderProbe::MarkDirty()
{ 
   m_dirty = true;
   m_reflection_clip_bounds.x = m_reflection_clip_bounds.y = m_reflection_clip_bounds.z = m_reflection_clip_bounds.w = FLT_MAX;
}

void RenderProbe::PreRenderStatic()
{
   if (m_type == PLANE_REFLECTION)
   {
      PreRenderStaticReflectionProbe();
   }
}

RenderTarget* RenderProbe::Render(const unsigned int renderMask)
{
   assert(m_rd != nullptr);
   const bool isStaticOnly = renderMask & Renderer::STATIC_ONLY;
   const bool isDynamicOnly = renderMask & Renderer::DYNAMIC_ONLY;
   const bool isReflectionPass = renderMask & Renderer::REFLECTION_PASS;

   // Probes are rendered and used in screen space therefore, they can't be recursively used (e.g. reflections
   // of reflections). Beside this, some properties are not cached (clip plane,...) and would break if
   // we would allow this. We simply return nullptr to disable this behavior.
   if (m_rendering || isReflectionPass)
      return nullptr;

   switch (m_type)
   {
   case PLANE_REFLECTION:
   {
      const ReflectionMode mode = min(m_reflection_mode, g_pplayer->m_renderer->GetMaxReflectionMode());
      if (mode == REFL_NONE || (isStaticOnly && (mode == REFL_BALLS || mode == REFL_DYNAMIC)) || (isDynamicOnly && (mode == REFL_STATIC)))
         return nullptr;
      if (m_dirty)
      {
         m_dirty = false;
         RenderReflectionProbe(renderMask);
      }
      return m_dynamicRT;
   }
   case SCREEN_SPACE_TRANSPARENCY:
   {
      if (m_dirty)
      {
         m_dirty = false;
         RenderScreenSpaceTransparency();
      }
      return m_dynamicRT;
   }
   default: assert(false); return nullptr;
   }
}

void RenderProbe::ExtendAreaOfInterest(const float xMin, const float xMax, const float yMin, const float yMax)
{
   if (m_reflection_clip_bounds.x == FLT_MAX)
   {
      m_reflection_clip_bounds.x = xMin;
      m_reflection_clip_bounds.y = yMin;
      m_reflection_clip_bounds.z = xMax;
      m_reflection_clip_bounds.w = yMax;
   }
   else
   {
      m_reflection_clip_bounds.x = min(m_reflection_clip_bounds.x, xMin);
      m_reflection_clip_bounds.y = min(m_reflection_clip_bounds.y, yMin);
      m_reflection_clip_bounds.z = max(m_reflection_clip_bounds.z, xMax);
      m_reflection_clip_bounds.w = max(m_reflection_clip_bounds.w, yMax);
   }
}

void RenderProbe::ApplyAreaOfInterest(RenderPass* pass)
{
   if (pass == nullptr)
   {
      pass = m_finalPass;
      if (pass == nullptr)
         return;
   }
   if (m_type == SCREEN_SPACE_TRANSPARENCY)
   {
      // Since we don't know where the sampling will occur, we only apply AOI to the blur passes, 
      // with a small arbitrary margin to account for IOR shift
      if (pass == m_copyPass)
         return;
      constexpr float margin = 0.05f;
      pass->m_areaOfInterest.x = m_reflection_clip_bounds.x - margin;
      pass->m_areaOfInterest.y = m_reflection_clip_bounds.y - margin;
      pass->m_areaOfInterest.z = m_reflection_clip_bounds.z + margin;
      pass->m_areaOfInterest.w = m_reflection_clip_bounds.w + margin;
   }
   else
   {
      // TODO we should enlarge the AOI to account for the blur kernel size
      // We do not apply to pass with just a clear command to avoid sampling artefacts
      if (!(pass->m_dependencies.empty() && pass->m_commands.size() == 1 && pass->m_commands[0]->IsFullClear(false)))
         pass->m_areaOfInterest = m_reflection_clip_bounds;
   }
   for (RenderPass* subpass : pass->m_dependencies)
      ApplyAreaOfInterest(subpass);
}


//////////////////////////////////////////////////////////////////////////////////////////////////
// Roughness implementation, using downscaling and bluring

int RenderProbe::GetRoughnessDownscale(const int roughness) const
{
   if (m_type == SCREEN_SPACE_TRANSPARENCY)
      return 1; // No downscaling since this is not supported by hardware for depth buffer
   return roughness < 7 ? 1 : 2;
}

void RenderProbe::ApplyRoughness(RenderTarget* probe, const int roughness)
{
   assert(0 <= roughness && roughness <= 12);
   if (roughness > 0)
   {
      if (m_blurRT == nullptr)
         m_blurRT = probe->Duplicate(probe->m_name+".Blur");
      // The kernel sizes were chosen by reverse engineering the blur shader. So there's a part of guess here.
      // The blur shader seems to mix binomial & gaussian distributions, with a kernel size which does not directly match the pascal triangle size.
      // Ideally this should be a gaussian distribution's sigma, scaled by the render height against a reference height.
      if (m_type == SCREEN_SPACE_TRANSPARENCY)
      {
         // FIXME adjust the kernels to have as many as there are roughness levels
         constexpr float kernel[] = { 0.f, 7.f, 9.f, 11.f, 13.f, 15.f, 19.f, 23.f, 27.f, 39.f, 39.f, 39.f, 39.f };
         m_rd->DrawGaussianBlur(probe, m_blurRT, probe, kernel[roughness]);
      }
      else
      {
         constexpr float kernel[] = { 0.f, 7.f, 9.f, 13.f, 15.f, 19.f, 23.f, 13.f, 15.f, 19.f, 23.f, 27.f, 39.f };
         m_rd->DrawGaussianBlur(probe, m_blurRT, probe, kernel[roughness]);
      }
   }
   m_finalPass = m_rd->GetCurrentPass();
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Screen copy

void RenderProbe::RenderScreenSpaceTransparency()
{
   RenderPass* const renderedPass = m_rd->GetCurrentPass();
   if (m_dynamicRT == nullptr)
   {
      const int downscale = GetRoughnessDownscale(m_roughness);
      const int w = renderedPass->m_rt->GetWidth() / downscale, h = renderedPass->m_rt->GetHeight() / downscale;
      m_dynamicRT = new RenderTarget(m_rd, renderedPass->m_rt->m_type, m_name, w, h, renderedPass->m_rt->GetColorFormat(), true, 1, "Failed to create refraction render target", nullptr);
   }
   m_rd->m_basicShader->SetTextureNull(SHADER_tex_refraction);
   m_rd->SetRenderTarget(m_name, m_dynamicRT, false);
   m_rd->BlitRenderTarget(renderedPass->m_rt, m_dynamicRT, true, true);
   ApplyRoughness(m_dynamicRT, m_roughness);
   m_copyPass = m_rd->GetCurrentPass();
   m_rd->SetRenderTarget(renderedPass->m_name, renderedPass->m_rt, false);
   m_rd->GetCurrentPass()->AddPrecursor(renderedPass);
   renderedPass->m_name += '-';
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Reflection plane

void RenderProbe::GetReflectionPlane(vec4& plane) const
{
   plane.x = m_reflection_plane.x;
   plane.y = m_reflection_plane.y;
   plane.z = m_reflection_plane.z;
   plane.w = m_reflection_plane.w;
}

void RenderProbe::SetReflectionPlane(const vec4& plane) {
   m_reflection_plane = plane;
}

void RenderProbe::GetReflectionPlaneNormal(vec3& normal) const
{
   normal.x = m_reflection_plane.x;
   normal.y = m_reflection_plane.y;
   normal.z = m_reflection_plane.z;
}

void RenderProbe::SetReflectionMode(ReflectionMode mode) 
{
   assert(m_reflection_mode == mode || m_dynamicRT == nullptr); // Reflection mode may not be changed between RenderSetup/RenderRelease
   m_reflection_mode = mode;
}

void RenderProbe::PreRenderStaticReflectionProbe()
{
   // For dynamic reflection mode, in static camera mode, we prerender static elements (like for main view) to get better antialiasing and overall performance
   if (min(m_reflection_mode, g_pplayer->m_renderer->GetMaxReflectionMode()) != REFL_DYNAMIC)
      return;

   RenderPass* const previousRT = m_rd->GetCurrentPass();

   if (m_prerenderRT == nullptr)
   {
      int w, h;
      m_renderer->GetRenderSizeAA(w, h);
      const int downscale = GetRoughnessDownscale(m_roughness);
      w /= downscale;
      h /= downscale;
      m_prerenderRT = new RenderTarget(m_rd, m_renderer->IsStereo() ? SurfaceType::RT_STEREO : SurfaceType::RT_DEFAULT, m_name + ".Stat", w, h, m_renderer->GetRenderFormat(), true, 1, "Failed to create plane reflection static render target", nullptr);
   }

   RenderTarget* accumulationSurface = m_prerenderRT->Duplicate("Accumulation"s);

   //#define STATIC_PRERENDER_ITERATIONS_KOROBOV 7.0 // for the (commented out) lattice-based QMC oversampling, 'magic factor', depending on the the number of iterations!
   // loop for X times and accumulate/average these renderings
   // NOTE: iter == 0 MUST ALWAYS PRODUCE an offset of 0,0!
   unsigned int nTris = m_rd->m_curDrawnTriangles;
   int n_iter = STATIC_PRERENDER_ITERATIONS - 1;
   for (int iter = n_iter; iter >= 0; --iter) // just do one iteration if in dynamic camera/light/material tweaking mode
   {
      m_rd->m_curDrawnTriangles = 0;

      float u1 = xyLDBNbnot[iter * 2]; //      (float)iter*(float)(1.0                                /STATIC_PRERENDER_ITERATIONS);
      float u2 = xyLDBNbnot[iter * 2 + 1]; //fmodf((float)iter*(float)(STATIC_PRERENDER_ITERATIONS_KOROBOV/STATIC_PRERENDER_ITERATIONS), 1.f);
      // the following line implements filter importance sampling for a small gauss (i.e. less jaggies as it also samples neighboring pixels) -> but also potentially more artifacts in compositing!
      gaussianDistribution(u1, u2, 0.5f, 0.0f); //!! first 0.5 could be increased for more blur, but is pretty much what is recommended
      // sanity check to be sure to limit filter area to 3x3 in practice, as the gauss transformation is unbound (which is correct, but for our use-case/limited amount of samples very bad)
      assert(u1 > -1.5f && u1 < 1.5f);
      assert(u2 > -1.5f && u2 < 1.5f);
      // Last iteration MUST set a sample offset of 0,0 so that final depth buffer features 'correctly' centered pixel sample
      assert(iter != 0 || (u1 == 0.f && u2 == 0.f));

      // Setup Camera,etc matrices for each iteration, applying antialiasing offset
      g_pplayer->m_renderer->InitLayout(u1, u2);

      m_rd->SetRenderTarget("PreRender Reflection"s, m_prerenderRT, false);
      m_rd->ResetRenderState();
      m_rd->Clear(clearType::TARGET | clearType::ZBUFFER, 0x00000000);
      DoRenderReflectionProbe(true, false, false);

      // Rendering is done to the static render target then accumulated to accumulationSurface
      // We use the framebuffer mirror shader which copies a weighted version of the bound texture
      m_rd->SetRenderTarget("PreRender Accumulate Reflection"s, accumulationSurface);
      m_rd->AddRenderTargetDependency(m_prerenderRT);
      m_rd->ResetRenderState();
      m_rd->SetRenderState(RenderState::ALPHABLENDENABLE, RenderState::RS_TRUE);
      m_rd->SetRenderState(RenderState::SRCBLEND, RenderState::ONE);
      m_rd->SetRenderState(RenderState::DESTBLEND, RenderState::ONE);
      m_rd->SetRenderState(RenderState::BLENDOP, RenderState::BLENDOP_ADD);
      m_rd->SetRenderState(RenderState::ZENABLE, RenderState::RS_FALSE);
      m_rd->SetRenderState(RenderState::ZWRITEENABLE, RenderState::RS_FALSE);
      m_rd->SetRenderState(RenderState::CULLMODE, RenderState::CULL_NONE);
      if (iter == STATIC_PRERENDER_ITERATIONS - 1)
         m_rd->Clear(clearType::TARGET, 0x00000000);
      m_rd->m_FBShader->SetTechnique(SHADER_TECHNIQUE_fb_mirror);
      m_rd->m_FBShader->SetVector(
         SHADER_w_h_height, (float)(1.0 / (double)m_prerenderRT->GetWidth()), (float)(1.0 / (double)m_prerenderRT->GetHeight()), (float)((double)STATIC_PRERENDER_ITERATIONS), 1.0f);
      m_rd->m_FBShader->SetTexture(SHADER_tex_fb_unfiltered, m_prerenderRT->GetColorSampler());
      m_rd->DrawFullscreenTexturedQuad(m_rd->m_FBShader);
      m_rd->m_FBShader->SetTextureNull(SHADER_tex_fb_unfiltered);

      m_rd->SubmitRenderFrame(); // Submit to avoid stacking up all prerender passes in a huge render frame
   }
   m_rd->m_curDrawnTriangles += nTris;

   // copy back weighted antialiased color result to the static render target, keeping depth untouched
   m_rd->SetRenderTarget("PreRender Store Reflection"s, m_prerenderRT);
   m_rd->BlitRenderTarget(accumulationSurface, m_prerenderRT, true, false);
   m_rd->AddEndOfFrameCmd([accumulationSurface]() { delete accumulationSurface; });
   if (previousRT)
   {
      m_rd->SetRenderTarget(previousRT->m_name, previousRT->m_rt);
      previousRT->m_name += '-';
   }
   else
      m_rd->SetRenderTarget(string(), nullptr);
}

void RenderProbe::RenderReflectionProbe(const unsigned int renderMask)
{
   assert(m_rd != nullptr);
   const bool isStaticOnly = renderMask & Renderer::STATIC_ONLY;
   const bool isDynamicOnly = renderMask & Renderer::DYNAMIC_ONLY;
   assert((renderMask & Renderer::REFLECTION_PASS) == 0);
   const ReflectionMode mode = min(m_reflection_mode, g_pplayer->m_renderer->GetMaxReflectionMode());

   if (mode == REFL_NONE
   || (isStaticOnly && (mode == REFL_BALLS || mode == REFL_DYNAMIC))
   || (isDynamicOnly && (mode == REFL_STATIC)))
      return;

   m_rendering = true;
   RenderPass* const previousRT = m_rd->GetCurrentPass();
   if (m_dynamicRT == nullptr)
   {
      int w, h;
      m_renderer->GetRenderSizeAA(w, h);
      const int downscale = GetRoughnessDownscale(m_roughness);
      w /= downscale;
      h /= downscale;
      m_dynamicRT = new RenderTarget(m_rd, m_renderer->IsStereo() ? SurfaceType::RT_STEREO : SurfaceType::RT_DEFAULT, m_name + ".Dyn", w, h, m_renderer->GetRenderFormat(), true, 1, "Failed to create plane reflection dynamic render target", nullptr);
   }
   m_rd->SetRenderTarget(m_name, m_dynamicRT);
   m_rd->ResetRenderState();
   if (isDynamicOnly && mode == REFL_DYNAMIC)
      m_rd->BlitRenderTarget(m_prerenderRT, m_dynamicRT, true, true);
   else
      m_rd->Clear(clearType::TARGET | clearType::ZBUFFER, 0x00000000);
   const bool render_static = !isDynamicOnly;
   const bool render_balls = !isStaticOnly && (mode == REFL_BALLS || mode >= REFL_STATIC_N_BALLS);
   const bool render_dynamic = !isStaticOnly && (mode >= REFL_STATIC_N_DYNAMIC);
   DoRenderReflectionProbe(render_static, render_balls, render_dynamic);
   ApplyRoughness(m_dynamicRT, m_roughness);
   m_rd->SetRenderTarget(previousRT->m_name, previousRT->m_rt);
   previousRT->m_name += '-';
   m_rendering = false;
}

void RenderProbe::DoRenderReflectionProbe(const bool render_static, const bool render_balls, const bool render_dynamic)
{
   m_rd->ResetRenderState();
   m_rd->CopyRenderStates(true, *m_rdState);

   const unsigned int prevRenderMask = g_pplayer->m_renderer->m_render_mask;
   g_pplayer->m_renderer->m_render_mask |= Renderer::REFLECTION_PASS;
   if (m_disableLightReflection)
      g_pplayer->m_renderer->m_render_mask |= Renderer::DISABLE_LIGHTMAPS;

   // Set the clip plane to only render objects above the reflection plane (do not reflect what is under or the plane itself)
   Vertex3Ds n(m_reflection_plane.x, m_reflection_plane.y, m_reflection_plane.z);
   n.Normalize();
   vec4 clip_plane(n.x, n.y, n.z, m_reflection_plane.w);
   m_rd->SetClipPlane(clip_plane);
   m_rd->SetRenderState(RenderState::CLIPPLANEENABLE, RenderState::RS_TRUE);
   // Reverse cull mode since we multiply by a reversing matrix (mirror also has a reversing matrix)
   m_rd->SetRenderState(RenderState::CULLMODE, g_pplayer->m_ptable->m_tblMirrorEnabled ? RenderState::CULL_CCW : RenderState::CULL_CW);
   m_rd->SetDefaultRenderState();
   m_rd->m_basicShader->SetTextureNull(SHADER_tex_reflection);

   // Flip camera
   Matrix3D viewMat, initialViewMat;
   viewMat = g_pplayer->m_renderer->GetMVP().GetView();
   memcpy(&initialViewMat.m[0][0], &viewMat.m[0][0], 4 * 4 * sizeof(float));
   viewMat = Matrix3D::MatrixPlaneReflection(n, m_reflection_plane.w) * viewMat;
   g_pplayer->m_renderer->GetMVP().SetView(viewMat);

   if (render_static || render_dynamic)
      g_pplayer->m_renderer->UpdateBasicShaderMatrix();
   if (render_balls)
      g_pplayer->m_renderer->UpdateBallShaderMatrix();

   if (render_static)
      g_pplayer->m_renderer->DrawStatics();
   if (render_dynamic)
      g_pplayer->m_renderer->DrawDynamics(false);
   else if (render_balls)
      g_pplayer->m_renderer->DrawDynamics(true);

   // Restore initial render states and camera
   g_pplayer->m_renderer->m_render_mask = prevRenderMask;
   m_rd->CopyRenderStates(false, *m_rdState);
   m_rd->SetDefaultRenderState();
   g_pplayer->m_renderer->GetMVP().SetView(initialViewMat);
}
