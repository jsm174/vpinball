#pragma once


#include "B2SBaseBox.h"
#include "../classes/Sound.h"
#include "../utils/Timer.h"

namespace B2SLegacy {

class B2SReelBox : public B2SBaseBox
{
public:
   B2SReelBox(VPXPluginAPI* vpxApi, B2SData* pB2SData);
   virtual ~B2SReelBox();

   void OnPaint(VPXRenderContext2D* const ctx) override;

   int GetSetID() const { return m_setID; }
   void SetSetID(int setID) { m_setID = setID; }
   string GetReelType() const { return m_szReelType; }
   void SetReelType(const string& reelType);
   string GetSoundName() const { return m_szSoundName; }
   void SetSoundName(const string& soundName) { m_szSoundName = soundName; }
   Sound* GetSound() const { return m_pSound; }
   void SetSound(Sound* pSound) { m_pSound = pSound; }
   eScoreType GetScoreType() const { return m_scoreType; }
   void SetScoreType(eScoreType scoreType) { m_scoreType = scoreType; }
   string GetGroupName() const { return m_szGroupName; }
   void SetGroupName(const string& groupName) { m_szGroupName = groupName; }
   bool IsIlluminated() const { return m_illuminated; }
   void SetIlluminated(bool illuminated);
   int GetValue() const { return m_value; }
   void SetValue(int value, bool refresh = false);
   void SetText(int text, bool animateReelChange = true);
   int GetCurrentText() const { return m_currentText; }
   int GetRollingInterval() const { return m_rollingInterval; }
   void SetRollingInterval(int rollingInterval);
   bool IsInReelRolling() { return m_intermediates2go <= 0; }
   bool IsInAction() const { return m_pTimer->IsEnabled(); }

private:
   void ReelAnimationTimerTick(Timer* pTimer);
   string ConvertValue(int value);
   string ConvertText(int text);

   Timer* m_pTimer;
   const int cTimerInterval = 50;
   bool m_led;
   int m_length;
   string m_initValue;
   string m_szReelIndex;
   int m_intermediates;
   int m_intermediates2go;
   int m_setID;
   string m_szReelType;
   string m_szSoundName;
   Sound* m_pSound;
   eScoreType m_scoreType;
   string m_szGroupName;
   bool m_illuminated;
   int m_value;
   int m_currentText;
   int m_text;
   int m_rollingInterval;

   int m_firstintermediatecount;
};

}
