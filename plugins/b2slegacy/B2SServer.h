#pragma once

#include "common.h"
#include "forms/FormBackglass.h"
#include "classes/B2SCollectData.h"

namespace B2SLegacy {

class B2SServer
{
public:
   PSC_IMPLEMENT_REFCOUNT()

public:
   B2SServer();
   ~B2SServer();

   void Dispose();
   string GetB2SServerVersion() const;
   double GetB2SBuildVersion() const;
   string GetB2SServerDirectory() const;
   string GetGameName() const;
   void SetGameName(const string& gameName);
   string GetROMName() const;
   string GetB2SName() const;
   void SetB2SName(const string& b2sName);
   string GetTableName() const;
   void SetTableName(const string& tableName);
   void SetWorkingDir(const string& workingDir);
   void SetPath(const string& path);
   void GetGames(const string& gameName) const; // VARIANT return removed for now
   void GetSettings() const; // VARIANT return removed for now
   bool GetRunning() const;
   void SetTimeFence(double timeInS);
   bool GetPause() const;
   void SetPause(bool pause);
   string GetVersion() const;
   string GetVPMBuildVersion() const;
   void Run(int handle); // VARIANT simplified to int
   void Stop();
   bool GetLaunchBackglass() const;
   void SetLaunchBackglass(bool launchBackglass);
   string GetSplashInfoLine() const;
   void SetSplashInfoLine(const string& splashInfoLine);
   bool GetShowFrame() const;
   void SetShowFrame(bool showFrame);
   bool GetShowTitle() const;
   void SetShowTitle(bool showTitle);
   bool GetShowDMDOnly() const;
   void SetShowDMDOnly(bool showDMDOnly);
   bool GetShowPinDMD() const;
   void SetShowPinDMD(bool showPinDMD);
   bool GetLockDisplay() const;
   void SetLockDisplay(bool lockDisplay);
   bool GetDoubleSize() const;
   void SetDoubleSize(bool doubleSize);
   bool GetHidden() const;
   void SetHidden(bool hidden);
   void SetDisplayPosition(int x, int y, int handle); // VARIANT simplified
   void ShowOptsDialog(int handle);
   void ShowPathesDialog(int handle);
   void ShowAboutDialog(int handle);
   void CheckROMS(bool showoptions, int handle);
   bool GetPuPHide() const;
   void SetPuPHide(bool puPHide);
   bool GetHandleKeyboard() const;
   void SetHandleKeyboard(bool handleKeyboard);
   int16_t GetHandleMechanics() const;
   void SetHandleMechanics(int16_t handleMechanics);
   void GetChangedLamps() const; // VARIANT return removed for now
   void GetChangedSolenoids() const; // VARIANT return removed for now
   void GetChangedGIStrings() const; // VARIANT return removed for now
   void GetChangedLEDs(int mask2, int mask1, int mask3, int mask4) const; // VARIANT return removed for now
   
   // Non-const versions for PSC compatibility
   void GetChangedLamps(); // VARIANT return removed for now
   void GetChangedSolenoids(); // VARIANT return removed for now
   void GetChangedGIStrings(); // VARIANT return removed for now
   void GetChangedLEDs(int mask2, int mask1, int mask3, int mask4); // VARIANT return removed for now
   void GetNewSoundCommands() const; // VARIANT return removed for now
   bool GetLamp(int number) const;
   bool GetSolenoid(int number) const;
   bool GetGIString(int number) const;
   bool GetSwitch(int number) const;
   void SetSwitch(int number, bool value);
   int GetMech(int number) const;
   void SetMech(int number, int value);
   void GetGetMech(int number) const; // VARIANT return removed for now
   int GetDip(int number) const;
   void SetDip(int number, int value);
   int GetSolMask(int number) const;
   void SetSolMask(int number, int value);
   int GetRawDmdWidth() const;
   int GetRawDmdHeight() const;
   void GetRawDmdPixels() const; // VARIANT return removed for now
   void GetRawDmdColoredPixels() const; // VARIANT return removed for now
   void GetChangedNVRAM() const; // VARIANT return removed for now
   void GetNVRAM() const; // VARIANT return removed for now
   int GetSoundMode() const;
   void SetSoundMode(int soundMode);
   void B2SSetData(int id, int value);
   void B2SSetData(const string& name, int value);
   void B2SPulseData(int id);
   void B2SPulseData(const string& name);
   void B2SSetPos(int id, int xpos, int ypos);
   void B2SSetPos(const string& name, int xpos, int ypos);
   void B2SSetIllumination(const string& name, int value);
   void B2SSetLED(int digit, int value);
   void B2SSetLED(int digit, const string& text);
   void B2SSetLEDDisplay(int display, const string& text);
   void B2SSetReel(int digit, int value);
   void B2SSetScore(int display, int value);
   void B2SSetScorePlayer(int playerno, int score);
   void B2SSetScorePlayer1(int score);
   void B2SSetScorePlayer2(int score);
   void B2SSetScorePlayer3(int score);
   void B2SSetScorePlayer4(int score);
   void B2SSetScorePlayer5(int score);
   void B2SSetScorePlayer6(int score);
   void B2SSetScoreDigit(int digit, int value);
   void B2SSetScoreRollover(int id, int value);
   void B2SSetScoreRolloverPlayer1(int value);
   void B2SSetScoreRolloverPlayer2(int value);
   void B2SSetScoreRolloverPlayer3(int value);
   void B2SSetScoreRolloverPlayer4(int value);
   void B2SSetCredits(int value);
   void B2SSetCredits(int digit, int value);
   void B2SSetPlayerUp(int value);
   void B2SSetPlayerUp(int id, int value);
   void B2SSetCanPlay(int value);
   void B2SSetCanPlay(int id, int value);
   void B2SSetBallInPlay(int value);
   void B2SSetBallInPlay(int id, int value);
   void B2SSetTilt(int value);
   void B2SSetTilt(int id, int value);
   void B2SSetMatch(int value);
   void B2SSetMatch(int id, int value);
   void B2SSetGameOver(int value);
   void B2SSetGameOver(int id, int value);
   void B2SSetShootAgain(int value);
   void B2SSetShootAgain(int id, int value);
   void B2SStartAnimation(const string& animationname, bool playreverse = false);
   void B2SStartAnimationReverse(const string& animationname);
   void B2SStopAnimation(const string& animationname);
   void B2SStopAllAnimations();
   bool GetB2SIsAnimationRunning(const string& animationname) const;
   void StartAnimation(const string& animationname, bool playreverse = false);
   void StopAnimation(const string& animationname);
   void B2SStartRotation();
   void B2SStopRotation();
   void B2SShowScoreDisplays();
   void B2SHideScoreDisplays();
   void B2SStartSound(const string& soundname);
   void B2SPlaySound(const string& soundname);
   void B2SStopSound(const string& soundname);
   void B2SMapSound(int digit, const string& soundname);

private:
   void TimerElapsed(Timer* pTimer);
   void CheckGetMech(int number, int mech);
   void CheckLamps(); // Simplified - no SAFEARRAY
   void CheckSolenoids();
   void CheckGIStrings();
   void CheckLEDs();
   void MyB2SSetData(int id, int value);
   void MyB2SSetData(const string& groupname, int value);
   void MyB2SSetPos(int id, int xpos, int ypos);
   void MyB2SSetLED(int digit, int value);
   void MyB2SSetLED(int digit, const string& value);
   void MyB2SSetLEDDisplay(int display, const string& szText);
   int GetFirstDigitOfDisplay(int display);
   void MyB2SSetScore(int digit, int value, bool animateReelChange, bool useLEDs = false, bool useLEDDisplays = false, bool useReels = false, int reeltype = 0, eLEDTypes ledtype = eLEDTypes_Undefined);
   void MyB2SSetScore(int digit, int score);
   void MyB2SSetScorePlayer(int playerno, int score);
   void MyB2SStartAnimation(const string& animationname, bool playreverse);
   void MyB2SStopAnimation(const string& animationname);
   void MyB2SStopAllAnimations();
   bool MyB2SIsAnimationRunning(const string& animationname);
   void MyB2SStartRotation();
   void MyB2SStopRotation();
   void MyB2SShowOrHideScoreDisplays(bool visible);
   void MyB2SPlaySound(const string& soundname);
   void MyB2SStopSound(const string& soundname);
   void Startup();
   void ShowBackglassForm();
   void HideBackglassForm();
   void KillBackglassForm();
   int RandomStarter(int top);

   B2SSettings* m_pB2SSettings;
   B2SData* m_pB2SData;
   FormBackglass* m_pFormBackglass;
   bool m_isVisibleStateSet;
   bool m_lastTopVisible;
   bool m_lastSecondVisible;
   int m_lampThreshold;
   int m_giStringThreshold;
   bool m_changedLampsCalled;
   bool m_changedSolenoidsCalled;
   bool m_changedGIStringsCalled;
   bool m_changedLEDsCalled;
   string m_lastRandomStartedAnimation;
   B2SCollectData* m_pCollectLampsData;
   B2SCollectData* m_pCollectSolenoidsData;
   B2SCollectData* m_pCollectGIStringsData;
   B2SCollectData* m_pCollectLEDsData;
   string m_szPath;
   Timer* m_pTimer;

};

}
