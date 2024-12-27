#pragma once

#include "../common/Window.h"
#include "DMDUtil/DMDUtil.h"

namespace VP {

class DMDWindow : public VP::Window
{
public:
   DMDWindow(const string& szTitle, int z, int x, int y, int w, int h);
   ~DMDWindow();

   bool Init() override;
   void AttachDMD(DMDUtil::DMD* pDMD, int width, int height);
   void DetachDMD();
   bool IsDMDAttached() const { return m_attached; }
   void Render() override;

private:
   DMDUtil::DMD* m_pDMD;
   DMDUtil::RGB24DMD* m_pRGB24DMD;
   int m_size;
   BaseTexture* m_pTexture;
   bool m_attached;
};

}