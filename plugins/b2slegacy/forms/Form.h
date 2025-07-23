#pragma once

#include "../controls/Control.h"
#include "../utils/RendererGraphics.h"


namespace B2SLegacy {

class B2SData;
class FormWindow;

class Form : public Control
{
public:
   Form(B2SData* pB2SData);
   ~Form();

   void SetWindow(FormWindow* pWindow) { m_pWindow = pWindow; }
   void Show();
   void Hide();
   void SetTopMost(bool topMost) { m_topMost = topMost; }
   bool IsTopMost() const { return m_topMost; }
   void SetGraphics(RendererGraphics* pGraphics) { m_pGraphics = pGraphics; }
   bool Render();

protected:
   B2SData* m_pB2SData = nullptr;

private:
   FormWindow* m_pWindow = nullptr;
   bool m_topMost = false;
   RendererGraphics* m_pGraphics = nullptr;
};

}
