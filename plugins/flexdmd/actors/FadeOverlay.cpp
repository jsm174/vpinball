#include "FadeOverlay.h"
#include "Group.h"

namespace Flex {

void FadeOverlay::Draw(Flex::SurfaceGraphics* pGraphics)
{
   Actor::Draw(pGraphics);

   if (GetParent()) {
      SetX(0);
      SetY(0);
      SetWidth(GetParent()->GetWidth());
      SetHeight(GetParent()->GetHeight());
      pGraphics->SetColor(m_color, static_cast<uint8_t>(min(m_alpha * 255.f, 255.f)));
      pGraphics->FillRectangle({ (int)GetX(), (int)GetY(), (int)GetWidth(), (int)GetHeight() });
   }
}

}
