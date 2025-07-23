#pragma once

#include "Form.h"
#include "../classes/B2SSettings.h"
#include "../classes/B2SData.h"
#include "../utils/Timer.h"


namespace B2SLegacy {

class B2SScreen;

class FormDMD : public Form
{
public:
   FormDMD(B2SData* pB2SData);
   ~FormDMD();

   void OnPaint(RendererGraphics* pGraphics) override;

private:
};

}
