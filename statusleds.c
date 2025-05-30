/*
 * statusleds.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <vdr/i18n.h>
#include <unistd.h>
#include <getopt.h>
#include <vdr/videodir.h>
#include <vdr/plugin.h>
#include <vdr/interface.h>
#include <vdr/status.h>
#include <vdr/osd.h>
#include <ctype.h>
#include <sys/kd.h>
#include <sys/ioctl.h>

extern char **environ;

static const char *VERSION        = "0.5";
static const char *DESCRIPTION    = "show vdr status over kbd led's and stm32IRstatusled";

class cStatusUpdate : public cThread, public cStatus {
private:
    bool active;
public:
    cStatusUpdate();
    ~cStatusUpdate();
#if VDRVERSNUM >= 10338
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
#else
    virtual void Recording(const cDevice *Device, const char *Name);
#endif
protected:
    virtual void Action(void);
};

class cRecordingPresignal : public cThread {
private:
    bool active;
public:
    void Stop(void);
protected:
    virtual void Action(void);
};

// Global variables that control the overall behaviour:
int iLed = 0;
int iOnDuration = 1;
int iOffDuration = 10;
int iOnPauseDuration = 5; 
bool bPerRecordBlinking = false;
const char * sConsole = "/dev/console";
int iRecordings = 0;
int iConsole = 0;
bool bActive = false;
int iPrewarnBeeps = 0;
int iPrewarnBeepPause = 500;
bool bPrewarnBeep = false;
int iPrewarnBeepTime = 120;
const char * stm32IRstatusled_path = NULL;
int OldLed;
char State;
cString cmd_on;
cString cmd_off;

cStatusUpdate * oStatusUpdate = NULL;
cRecordingPresignal * oRecordingPresignal = NULL;

class cPluginStatusLeds : public cPlugin {
private:
public:
  cPluginStatusLeds(void);
  virtual ~cPluginStatusLeds();
  virtual const char *Version(void) { return VERSION; }
  virtual const char *Description(void) { return tr(DESCRIPTION); }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Start(void);
  virtual void Housekeeping(void);
  virtual void Stop(void);
  virtual const char *MainMenuEntry(void) { return NULL; }
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
};

// --- cMenuSetupStatusLeds -------------------------------------------------------

class cMenuSetupStatusLeds : public cMenuSetupPage {
private:
  int iNewLed;
  int iNewOnDuration;
  int iNewOffDuration;
  int iNewOnPauseDuration;
  int bNewPerRecordBlinking;
  int iNewPrewarnBeeps;
  int iNewPrewarnBeepPause;
  int bNewPrewarnBeep;
  int iNewPrewarnBeepTime;
protected:
  virtual void Store(void);
  void Set(void);
  void Save();
  eOSState ProcessKey(eKeys Key);
public:
  cMenuSetupStatusLeds(void);
  };

cMenuSetupStatusLeds::cMenuSetupStatusLeds(void)
{
  iNewLed = iLed;
  iNewOnDuration = iOnDuration;
  iNewOffDuration = iOffDuration;
  iNewOnPauseDuration = iOnPauseDuration;
  bNewPerRecordBlinking = bPerRecordBlinking;
  iNewPrewarnBeeps = iPrewarnBeeps;
  iNewPrewarnBeepPause = iPrewarnBeepPause;
  bNewPrewarnBeep = bPrewarnBeep;
  iNewPrewarnBeepTime = iPrewarnBeepTime;

  Set();
}

void cMenuSetupStatusLeds::Set(void)
{
  int current = Current();
  Clear();

  static const char * Leds[] =
  {
    tr("Setup.StatusLeds$Scroll"),
    tr("Setup.StatusLeds$Num"),
    tr("Setup.StatusLeds$Caps")
  };

  Add(new cMenuEditBoolItem( tr("Setup.StatusLeds$Prewarn beep"), &bNewPrewarnBeep));
  if (bNewPrewarnBeep)
  {
    Add(new cMenuEditIntItem(  tr("Setup.StatusLeds$    Prewarn time"), &iNewPrewarnBeepTime, 1, 32768));
    Add(new cMenuEditIntItem(  tr("Setup.StatusLeds$    Beeps"), &iNewPrewarnBeeps, 1, 100));
    Add(new cMenuEditIntItem(  tr("Setup.StatusLeds$    Pause"), &iNewPrewarnBeepPause));
  }

  Add(new cMenuEditStraItem( tr("Setup.StatusLeds$LED"), &iNewLed, 3, Leds));
  Add(new cMenuEditBoolItem( tr("Setup.StatusLeds$One blink per recording"), &bNewPerRecordBlinking));

  // Add ioctl() options
  Add(new cMenuEditIntItem( tr("Setup.StatusLeds$    On time (100ms)"), &iNewOnDuration, 1, 99));
  Add(new cMenuEditIntItem( tr("Setup.StatusLeds$    On pause time (100ms)"), &iNewOnPauseDuration, 1, 99));
  Add(new cMenuEditIntItem( tr("Setup.StatusLeds$    Off time (100ms)"), &iNewOffDuration, 1, 99));

  SetCurrent(Get(current));
}

eOSState cMenuSetupStatusLeds::ProcessKey(eKeys Key)
{
  bool bOldPrewarnBeep = bNewPrewarnBeep;

  eOSState state = cMenuSetupPage::ProcessKey(Key);
  if (bOldPrewarnBeep != bNewPrewarnBeep)
  {
    Set();
    Display();
  }

  return state;
}

void cMenuSetupStatusLeds::Save(void)
{
  if (iNewLed != iLed) {
    // disable old, enable new
    ioctl(iConsole, KDGETLED, &State);
    ioctl(iConsole, KDSETLED, State & ~(1 << iLed));
    usleep(100000);
    ioctl(iConsole, KDGETLED, &State);
    ioctl(iConsole, KDSETLED, State | (1 << iNewLed));
  }
  iLed = iNewLed;
  iOnDuration = iNewOnDuration;
  iOffDuration = iNewOffDuration;
  iOnPauseDuration = iNewOnPauseDuration;
  bPerRecordBlinking = bNewPerRecordBlinking;
  bPrewarnBeep = bNewPrewarnBeep;
  iPrewarnBeeps = iNewPrewarnBeeps;
  iPrewarnBeepTime = iNewPrewarnBeepTime;
  iPrewarnBeepPause = iNewPrewarnBeepPause;
}

void cMenuSetupStatusLeds::Store(void)
{
  Save();

  SetupStore("Led", iLed);
  SetupStore("OnDuration", iOnDuration);
  SetupStore("OffDuration", iOffDuration);
  SetupStore("OnPauseDuration", iOnPauseDuration);
  SetupStore("PerRecordBlinking", bPerRecordBlinking);
  SetupStore("PrewarnBeep", bPrewarnBeep);
  SetupStore("PrewarnBeeps", iPrewarnBeeps);
  SetupStore("PrewarnBeepPause", iPrewarnBeepPause);
  SetupStore("PrewarnBeepTime", iPrewarnBeepTime);
}

// --- cPluginStatusLeds ----------------------------------------------------------

cPluginStatusLeds::cPluginStatusLeds(void)
{
  // Initialize any member variables here.
  // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
  // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
}

cPluginStatusLeds::~cPluginStatusLeds()
{
  // Clean up after yourself!
  if (oStatusUpdate)
  {
    delete oStatusUpdate;
    oStatusUpdate = NULL;
  }

  if (oRecordingPresignal)
  {
    delete oRecordingPresignal;
    oRecordingPresignal = NULL;
  }
}

const char *cPluginStatusLeds::CommandLineHelp(void)
{
  // Return a string that describes all known command line options.
  return

"  -l LED, --led=LED                          LED (0: scroll, 1: num, 2: caps)\n"
"  -p, --perrecordblinking                    LED blinks one times per recording\n"
"  -d [on[,off[,pause]]],                     LED blinking timing\n"
"     --duration[=On-Time[,Off-Time[,On-Pause-Time]]]\n"
"  -c console, --console=console              Console LED attached to\n"
"  -w [time,beeps,pause],                     Presignal records\n"
"     --prewarn[=Time,Beeps,Pause]\n" 
"  -i stm32IRstatusled_path, --stm32IRstatusled_path=stm32IRstatusled_path  stm32IRstatusled_path\n"
;
}

bool cPluginStatusLeds::ProcessArgs(int argc, char *argv[])
{
  // Implement command line argument processing here if applicable.
  static struct option long_options[] = {
       { "led",			required_argument,	NULL, 'l' },
       { "duration",		optional_argument,	NULL, 'd' },
       { "perrecordblinking",	no_argument,		NULL, 'p' },
       { "console",		required_argument,	NULL, 'c' },
       { "prewarn",		optional_argument,	NULL, 'w' },
       { "stm32IRstatusled_path", required_argument,	NULL, 'i' },
       { NULL,			no_argument,		NULL, 0 }
     };

  int c;
  while ((c = getopt_long(argc, argv, "l:d:pc:b:w:i:", long_options, NULL)) != -1) {
        switch (c) {
          case 'l':
            iLed = atoi(optarg);
            if (iLed < 0 || iLed > 2)
              iLed = 0;
            break;
          case 'd':
            iOnDuration = 1;
            iOffDuration = 10;
            iOnPauseDuration = 5;
            if (optarg && *optarg)
              sscanf(optarg, "%d,%d,%d", &iOnDuration, &iOffDuration, &iOnPauseDuration);
            break;
          case 'p': 
            bPerRecordBlinking = true;
            break;
          case 'c':
            sConsole = optarg;
            break;
          case 'w':
            bPrewarnBeep = true;
            if (optarg && *optarg)
              sscanf(optarg, "%d,%d,%d", &iPrewarnBeepTime, &iPrewarnBeeps, &iPrewarnBeepPause);
            break;
          case 'i':
            stm32IRstatusled_path = optarg;
            break;
          default:
            return false;
          }
        }
  return true;
}

cStatusUpdate::cStatusUpdate()
{
}

cStatusUpdate::~cStatusUpdate()
{
  if (oStatusUpdate)
  {
    // Perform any cleanup or other regular tasks.
    bActive = false;

    // Stop threads
    oRecordingPresignal->Stop();
  }
}

void cStatusUpdate::Action(void)
{
    dsyslog("Status LED's: Thread started (pid=%d)", getpid());


    // Open console
    iConsole = open(sConsole, 2);
    if (iConsole < 0)
      esyslog("ERROR: Status LED's: Can't open console %s", sConsole);
    else
    {
      bool blinking = false;
      // turn the LED's on at start of VDR
      ioctl(iConsole, KDGETLED, &State);
      ioctl(iConsole, KDSETLED, State | (1 << iLed));
      SystemExec(cmd_on, true);

      for(bActive = true; bActive;)
      {
        OldLed = iLed;
        if (iRecordings > 0)
        {
          //  let the LED's blink, if there's a recording
          if(!blinking){
            blinking = true;
          }
          for(int i = 0; i < (bPerRecordBlinking ? iRecordings : 1) && bActive; i++)
          {
            ioctl(iConsole, KDGETLED, &State);
            ioctl(iConsole, KDSETLED, State | (1 << iLed));
            SystemExec(cmd_on, true);
            usleep(iOnDuration * 100000);

            ioctl(iConsole, KDGETLED, &State);
            ioctl(iConsole, KDSETLED, State & ~(1 << OldLed));
            SystemExec(cmd_off, true);
            usleep(iOnPauseDuration * 100000);
          }
          usleep(iOffDuration * 100000);
        }
        else
        {
          //  turn the LED's on, if there's no recording
          if(blinking){
            ioctl(iConsole, KDGETLED, &State);
            ioctl(iConsole, KDSETLED, State | (1 << iLed));
            SystemExec(cmd_on, true);
            blinking = false;
          }
          sleep(1);
        }
      }
    }
}

bool cPluginStatusLeds::Start(void)
{
    // Start any background activities the plugin shall perform.
    oStatusUpdate = new cStatusUpdate;
    oStatusUpdate->Start();

    oRecordingPresignal = new cRecordingPresignal;
    oRecordingPresignal->Start();

    cmd_on = cString::sprintf("%s -s 1", stm32IRstatusled_path);
    cmd_off = cString::sprintf("%s -s 0", stm32IRstatusled_path);

    return true;
}

void cPluginStatusLeds::Housekeeping(void)
{
}

void cPluginStatusLeds::Stop(void)
{
  // turn the LED's off, when VDR stops
  ioctl(iConsole, KDGETLED, &State);
  ioctl(iConsole, KDSETLED, State & ~(1 << OldLed));
  SystemExec(cmd_off, true);
  close(iConsole);
  dsyslog("Status LED's: Thread ended (pid=%d)", getpid());
}

cMenuSetupPage *cPluginStatusLeds::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return new cMenuSetupStatusLeds;
}

bool cPluginStatusLeds::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.

  if (!strcasecmp(Name, "Led"))
  {
    iLed = atoi(Value);
    if (iLed < 0 || iLed > 2)
      iLed = 0;
  }
  else if (!strcasecmp(Name, "OnDuration"))
  {
    iOnDuration = atoi(Value);
    if (iOnDuration < 0 || iOnDuration > 99)
      iOnDuration = 1;
  }
  else if (!strcasecmp(Name, "OffDuration")) 
  {
    iOffDuration = atoi(Value);
    if (iOffDuration < 0 || iOffDuration > 99)
      iOffDuration = 10;
  }
  else if (!strcasecmp(Name, "OnPauseDuration"))
  {
    iOnPauseDuration = atoi(Value);
    if (iOnPauseDuration < 0 || iOnPauseDuration > 99)
      iOnPauseDuration = 5;
  }
  else if (!strcasecmp(Name, "PerRecordBlinking"))
  {
    bPerRecordBlinking = atoi(Value);
  }
  else if (!strcasecmp(Name, "PrewarnBeep"))
  {
    bPrewarnBeep = atoi(Value);
  }
  else if (!strcasecmp(Name, "PrewarnBeeps"))
  {
    iPrewarnBeeps = atoi(Value);
  }
  else if (!strcasecmp(Name, "PrewarnBeepTime"))
  {
    iPrewarnBeepTime = atoi(Value);
  }
  else if (!strcasecmp(Name, "PrewarnBeepPause"))
  {
    iPrewarnBeepPause = atoi(Value);
  }
  else
    return false;

  return true;
}

#if VDRVERSNUM >= 10338
void cStatusUpdate::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
  if (On)
#else
void cStatusUpdate::Recording(const cDevice *Device, const char *Name)
{
  if (Name)
#endif
    iRecordings++;
  else
    iRecordings--;
}

void cRecordingPresignal::Action(void)
{
  dsyslog("Status LED's: Presignal-Thread started (pid=%d)", getpid());

  time_t LastTime = 0;

  // Observe the timer list
  for(active = true; active;)
  {
    // get next timer
    {
    LOCK_TIMERS_READ;
    const cTimer * NextTimer = Timers->GetNextActiveTimer();

    if (iConsole >= 0 && NextTimer)
    {
      time_t StartTime = NextTimer->StartTime();

      if (LastTime != StartTime)
      {
        // get warn time
        time_t Now = time(NULL);

        // Start signalisation?
        if (StartTime - iPrewarnBeepTime < Now)
        {
          if (bPrewarnBeep)
          {
            for(int i = 0; i < iPrewarnBeeps; i++)
            {
              system("/usr/local/bin/beep");
              usleep(iPrewarnBeepPause * 1000);
            }
          }

          // remember last signaled time
          LastTime = StartTime;
        }
      }
    }
    }

    sleep(1);
  }

  dsyslog("Status LED's: Presignal-Thread ended (pid=%d)", getpid());
}

void cRecordingPresignal::Stop()
{
  active = false;
}

VDRPLUGINCREATOR(cPluginStatusLeds); // Don't touch this!
