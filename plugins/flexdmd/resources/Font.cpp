#include "Font.h"
#include "AssetManager.h"

namespace Flex {

Font::Font(AssetManager* pAssetManager, AssetSrc* pAssetSrc)
{
   m_pBitmapFont = (BitmapFont*)pAssetManager->Open(pAssetSrc);
 
   m_textures = new SDL_Surface*[m_pBitmapFont->GetPageCount()];
   memset((void*)m_textures, 0, sizeof(SDL_Surface*) * m_pBitmapFont->GetPageCount());

   for (int i = 0; i < m_pBitmapFont->GetPageCount(); i++) {
      AssetSrc* pTextureAssetSrc = pAssetManager->ResolveSrc(m_pBitmapFont->GetPage(i)->GetFilename(), pAssetSrc);
      m_textures[i] = (SDL_Surface*)pAssetManager->Open(pTextureAssetSrc);
      pTextureAssetSrc->Release();
   }

   if (pAssetSrc->GetFontBorderSize() > 0) {
      for (int i = 0; i < m_pBitmapFont->GetPageCount(); i++) {
         if (!m_textures[i])
            continue;

         SDL_Surface* const src = SDL_ConvertSurface(m_textures[i], SDL_PIXELFORMAT_RGBA32);

         const int w = src->w;
         const int h = src->h;

         SDL_Surface* const dst = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);

         const ColorRGBA32 outline = SDL_MapSurfaceRGBA(dst, 
            GetRValue(pAssetSrc->GetFontBorderTint()), 
            GetGValue(pAssetSrc->GetFontBorderTint()), 
            GetBValue(pAssetSrc->GetFontBorderTint()), 
            255);

         SDL_LockSurface(src);
         SDL_LockSurface(dst);

         const ColorRGBA32* const pixels_src = (ColorRGBA32*)src->pixels;
               ColorRGBA32* const pixels_dst = (ColorRGBA32*)dst->pixels;

         for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
               uint8_t r, g, b, a;
               SDL_GetRGBA(pixels_src[y * w + x], SDL_GetPixelFormatDetails(src->format), SDL_GetSurfacePalette(src), &r, &g, &b, &a);

               if (a == 0)
                  continue;

               if (x > 0) {
                  if (y > 0)
                     pixels_dst[(y - 1) * w + (x - 1)] = outline;
                  pixels_dst[y * w + (x - 1)] = outline;
                  if (y < h - 1)
                     pixels_dst[(y + 1) * w + (x - 1)] = outline;
               }
               if (y > 0)
                  pixels_dst[(y - 1) * w + x] = outline;
               if (y < h - 1)
                  pixels_dst[(y + 1) * w + x] = outline;
               if (x < w - 1) {
                  if (y > 0)
                     pixels_dst[(y - 1) * w + (x + 1)] = outline;
                  pixels_dst[y * w + (x + 1)] = outline;
                  if (y < h - 1)
                     pixels_dst[(y + 1) * w + (x + 1)] = outline;
               }
            }
         }

         const uint32_t tint_r = GetRValue(pAssetSrc->GetFontTint());
         const uint32_t tint_g = GetGValue(pAssetSrc->GetFontTint());
         const uint32_t tint_b = GetBValue(pAssetSrc->GetFontTint());

         const int n = dst->w * dst->h;
         for (int idx = 0; idx < n; ++idx)
         {
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixels_src[idx], SDL_GetPixelFormatDetails(src->format), SDL_GetSurfacePalette(src), &r, &g, &b, &a);

            if (a == 0)
               continue;

            r = (uint8_t)min((r * tint_r) / 255u, 255u);
            g = (uint8_t)min((g * tint_g) / 255u, 255u);
            b = (uint8_t)min((b * tint_b) / 255u, 255u);

            pixels_dst[idx] = SDL_MapSurfaceRGBA(dst, r, g, b, a);
         }

         SDL_UnlockSurface(src);
         SDL_UnlockSurface(dst);
         SDL_DestroySurface(src);
         SDL_DestroySurface(m_textures[i]);

         m_textures[i] = dst;
      }

      for(const auto& pair : m_pBitmapFont->GetCharacters()) {
         Character* character = pair.second;
         character->SetXAdvance(character->GetXAdvance() + 2);
      }
   }
   else if (pAssetSrc->GetFontTint() != RGB(255, 255, 255)) {
      for (int i = 0; i < m_pBitmapFont->GetPageCount(); i++) {
         if (!m_textures[i])
            continue;

         SDL_Surface* const dst = SDL_ConvertSurface(m_textures[i], SDL_PIXELFORMAT_RGBA32);

         SDL_LockSurface(dst);

         ColorRGBA32* const pixels_dst = (ColorRGBA32*)dst->pixels;

         const uint32_t tint_r = GetRValue(pAssetSrc->GetFontTint());
         const uint32_t tint_g = GetGValue(pAssetSrc->GetFontTint());
         const uint32_t tint_b = GetBValue(pAssetSrc->GetFontTint());

         const int n = dst->w * dst->h;
         for (int idx = 0; idx < n; ++idx) {
            uint8_t r,g,b,a;
            SDL_GetRGBA(pixels_dst[idx], SDL_GetPixelFormatDetails(dst->format), SDL_GetSurfacePalette(dst), &r, &g, &b, &a);

            r = (uint8_t)min((r * tint_r) / 255u, 255u);
            g = (uint8_t)min((g * tint_g) / 255u, 255u);
            b = (uint8_t)min((b * tint_b) / 255u, 255u);

            pixels_dst[idx] = SDL_MapSurfaceRGBA(dst, r, g, b, a);
         }

         SDL_UnlockSurface(dst);
         SDL_DestroySurface(m_textures[i]);

         m_textures[i] = dst;
      }
   }
}

Font::~Font()
{
   assert(m_refCount == 0);
   if (m_pBitmapFont)
   {
      for (int i = 0; i < m_pBitmapFont->GetPageCount(); ++i) {
         if (m_textures[i])
            SDL_DestroySurface(m_textures[i]);
      }

      delete m_pBitmapFont;
   }
}

void Font::DrawCharacter(Flex::SurfaceGraphics* pGraphics, char character, char previousCharacter, float& x, float& y)
{
   if (character == '\n') {
      x = 0;
      y += m_pBitmapFont->GetLineHeight();
   }
   else {
      Character* pCharacter = m_pBitmapFont->GetCharacter(character);
      if (pCharacter) {
         int kerning = m_pBitmapFont->GetKerning(previousCharacter, character);
         if (pGraphics) {
            SDL_Surface* pSource = m_textures[pCharacter->GetTexturePage()];
            if (pSource) {
               SDL_Rect bounds = pCharacter->GetBounds();
               SDL_Point offset = pCharacter->GetOffset();
               SDL_Rect rect = { (int)(x + offset.x + kerning), (int)(y + offset.y), bounds.w, bounds.h };
               pGraphics->DrawImage(pSource, &bounds, &rect);
            }
         }
         x += pCharacter->GetXAdvance() + kerning;
      }
      else if ('a' <= character && character <= 'z' && m_pBitmapFont->GetCharacter(toupper(character))) {
         m_pBitmapFont->SetCharacter(character, m_pBitmapFont->GetCharacter(toupper(character)));
         DrawCharacter(pGraphics, character, previousCharacter, x, y);
      }
      else if (m_pBitmapFont->GetCharacter(' ')) {
         LOGD("Missing character 0x%02X replaced by ' '", character);
         m_pBitmapFont->SetCharacter(character, m_pBitmapFont->GetCharacter(' '));
         DrawCharacter(pGraphics, character, previousCharacter, x, y);
      }
   }
}

SDL_Rect Font::MeasureFont(const string& text)
{
   DrawText_(nullptr, 0, 0, text);
   return m_pBitmapFont->MeasureFont(text);
}

void Font::DrawText_(Flex::SurfaceGraphics* pGraphics, float x, float y, const string& text)
{
   char previousCharacter = ' ';
   for (size_t i = 0; i < text.length(); i++) {
      char character = text[i];
      DrawCharacter(pGraphics, character, previousCharacter, x, y);
      previousCharacter = character;
   }
}

}
