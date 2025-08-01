#pragma once

#include "B2SAnimationBase.h"
#include "EntryAction.h"

#include <map>


namespace B2SLegacy {

class Form;
class B2SPictureBox;
class PictureBoxAnimationEntry;

class PictureBoxAnimation : public B2SAnimationBase
{
public:
   PictureBoxAnimation(
      B2SData* pB2SData,
      Form* pForm,
      Form* pFormDMD,
      const string& szName,
      eDualMode dualMode,
      int interval,
      int loops,
      bool startTimerAtVPActivate,
      eLightsStateAtAnimationStart lightsStateAtAnimationStart,
      eLightsStateAtAnimationEnd lightsStateAtAnimationEnd,
      eAnimationStopBehaviour animationStopBehaviour,
      bool lockInvolvedLamps,
      bool hideScoreDisplays,
      bool bringToFront,
      bool randomStart,
      int randomQuality,
      const vector<PictureBoxAnimationEntry*>& entries);
   ~PictureBoxAnimation();

   void Start() override;
   void Stop() override;

private:
   void PictureBoxAnimationTick(Timer* pTimer);
   void LightGroup(const string& szGroupName, bool visible);
   void LightBulb(const string& szBulb, bool visible);
   eLEDTypes GetLEDType();

   Form* m_pForm;
   Form* m_pFormDMD;
   std::map<int, EntryAction*> m_entries;
   int m_loopticker;
   int m_ticker;
   bool m_reachedThe0Point;
   vector<string> m_lightsInvolved;
   std::map<string, bool> m_lightsStateAtStartup;
   VPXTexture m_pMainFormBackgroundImage;
   eLEDTypes m_selectedLEDType;
};

}
