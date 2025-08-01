#include "../common.h"

#include "B2SBaseBox.h"

namespace B2SLegacy {

B2SBaseBox::B2SBaseBox(VPXPluginAPI* vpxApi, B2SData* pB2SData) :
   Control(vpxApi)
{
   m_pB2SData = pB2SData;
   m_type = eType_2_NotDefined;
   m_romid = 0;
   m_romidtype = eRomIDType_NotDefined;
   m_romidvalue = 0;
   m_rominverted = false;
   m_rectangleF = { 0.0f, 0.0f, 0.0f, 0.0f };
   m_startDigit = 0;
   m_digits = 0;
   m_hidden = false;
}

}
