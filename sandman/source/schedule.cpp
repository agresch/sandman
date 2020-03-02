#include "schedule.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "control.h"
#include "logger.h"
#include "sound.h"
#include "timer.h"

#define DATADIR		AM_DATADIR
#define CONFIGDIR	AM_CONFIGDIR

// Constants
//


// Types
//

// A schedule event.
struct ScheduleEvent
{
	// Delay in seconds before this entry occurs (since the last).
	unsigned int	m_DelaySec;
	
	// The control action to perform at the scheduled time.
	ControlAction	m_ControlAction;
};

// Locals
//

// Whether the system is initialized.
static bool s_ScheduleInitialized = false;

// This will contain the schedule once it has been parsed in.
static std::vector<ScheduleEvent> s_ScheduleEvents;

// The current spot in the schedule.
static unsigned int s_ScheduleIndex = UINT_MAX;

// The time the delay for the next event began.
static Time s_ScheduleDelayStartTime;

// Functions
//

template<class T>
T const& Min(T const& p_A, T const& p_B)
{
	return (p_A < p_B) ? p_A : p_B;
}

// Skip whitespace.
// 
// p_InputString:	The string whose whitespace will be skipped.
// 
// returns:			A pointer to the next non-whitespace character.
// 
static char* SkipWhitespace(char* p_InputString)
{
	char* l_OutputString = p_InputString;
	
	while (isspace(*l_OutputString) != 0)
	{
		l_OutputString++;
	}
	
	return l_OutputString;
}
		
// Load the schedule from a file.
// 
static bool ScheduleLoad()
{
	// Open the schedule file.
	auto* l_ScheduleFile = fopen(CONFIGDIR "sandman.sched", "r");
	
	if (l_ScheduleFile == nullptr)
	{
		return false;
	}
	
	// First, we have to find the first action in the schedule.
	bool l_HaveSeenStart = false;
	
	// Read each line in turn.
	static constexpr unsigned int const l_LineBufferCapacity = 128;
	char l_LineBuffer[l_LineBufferCapacity];
	
	fpos_t l_FirstActionFilePosition;
	
	while (fgets(l_LineBuffer, l_LineBufferCapacity, l_ScheduleFile) != nullptr)
	{
		// Skip comments.
		if (l_LineBuffer[0] == '#')
		{
			continue;
		}
	
		// Skip whitespace.
		char* l_LineText = SkipWhitespace(l_LineBuffer);
		
		// Until we see the start command, don't begin counting.
		if (l_HaveSeenStart == false) 
		{
			static char const* s_StartText = "start";
			if (strncmp(l_LineText, s_StartText, strlen(s_StartText)) == 0)
			{
				l_HaveSeenStart = true;
				
				// Save the file position so we can re-parse from here.
				fgetpos(l_ScheduleFile, &l_FirstActionFilePosition);
			}
			
			continue;
		}

		// Once we see the end command, stop counting.
		static char const* s_EndText = "end";
		if (strncmp(l_LineText, s_EndText, strlen(s_EndText)) == 0)
		{
			break;
		}
	}
		
	// Now we parse in the actual events.
	
	// Jump back to the first action.
	if (fsetpos(l_ScheduleFile, &l_FirstActionFilePosition) < 0)
	{
		// Close the file.
		fclose(l_ScheduleFile);
		return false;
	}
	
	while (fgets(l_LineBuffer, l_LineBufferCapacity, l_ScheduleFile) != nullptr)
	{
		// Skip comments.
		if (l_LineBuffer[0] == '#')
		{
			continue;
		}
	
		// Skip whitespace.
		char* l_LineText = SkipWhitespace(l_LineBuffer);
		
		// Once we see the end command, stop.
		static char const* s_EndText = "end";
		if (strncmp(l_LineText, s_EndText, strlen(s_EndText)) == 0)
		{
			break;
		}

		// The delay is followed by a comma.
		char* l_Separator = strchr(l_LineText, ',');
		
		if (l_Separator == nullptr)
		{
			continue;
		}
		
		// For now, modify the string to split it.
		auto const* l_DelayString = l_LineText;
		l_LineText = l_Separator + 1;
		(*l_Separator) = '\0';
		
		// Skip whitespace.
		l_LineText = SkipWhitespace(l_LineText);
		
		// The control name is also followed by a comma.
		l_Separator = strchr(l_LineText, ',');
		
		if (l_Separator == nullptr)
		{
			continue;
		}
		
		// For now, modify the string to split it.
		auto const* l_ControlNameString = l_LineText;
		l_LineText = l_Separator + 1;
		(*l_Separator) = '\0';
		
		// Skip whitespace.
		l_LineText = SkipWhitespace(l_LineText);
		
		// There should be nothing after the direction.
		
		// Build the event here.
		ScheduleEvent l_Event;
		
		// Parse the delay.
		l_Event.m_DelaySec = atoi(l_DelayString);
		
		// Parse the direction.
		auto l_Action = Control::ACTION_STOPPED;
		
		static char const* const l_UpString = "up";
		static char const* const l_DownString = "down";
		
		if (strncmp(l_LineText, l_UpString, strlen(l_UpString)) == 0)
		{
			l_Action = Control::ACTION_MOVING_UP;
		}
		else if (strncmp(l_LineText, l_DownString, strlen(l_DownString)) == 0)
		{
			l_Action = Control::ACTION_MOVING_DOWN;
		}
		
		// Validate it.
		if (l_Action == Control::ACTION_STOPPED)
		{
			LoggerAddMessage("\"%s\" is not a valid control direction.  This entry will be ignored.", 
				l_LineText);
			continue;
		}
		
		l_Event.m_ControlAction = ControlAction(l_ControlNameString, l_Action);
		
		// Keep it.
		s_ScheduleEvents.push_back(l_Event);
	}
	
	// Close the file.
	fclose(l_ScheduleFile);
		
	return true;
}

// Write the loaded schedule to the logger.
//
static void ScheduleLogLoaded()
{
	// Now write out the schedule.
	LoggerAddMessage("The following schedule is loaded:");
	
	for (auto const& l_Event : s_ScheduleEvents)
	{
		// Split the delay into multiple units.
		auto l_DelaySec = l_Event.m_DelaySec;
		
		auto const l_DelayHours = l_DelaySec / 3600;
		l_DelaySec %= 3600;
		
		auto const l_DelayMin = l_DelaySec / 60;
		l_DelaySec %= 60;
		
		auto const* l_ActionText = (l_Event.m_ControlAction.m_Action == Control::ACTION_MOVING_UP) ? 
			"up" : "down";
			
		// Print the event.
		LoggerAddMessage("\t+%01ih %02im %02is -> %s, %s", l_DelayHours, l_DelayMin, 
			l_DelaySec, l_Event.m_ControlAction.m_ControlName, l_ActionText);
	}
	
	LoggerAddMessage("");
}

// Initialize the schedule.
//
void ScheduleInitialize()
{	
	s_ScheduleIndex = UINT_MAX;
	
	s_ScheduleEvents.clear();
	
	LoggerAddMessage("Initializing the schedule...");

	// Parse the schedule.
	if (ScheduleLoad() == false)
	{
		LoggerAddMessage("\tfailed");
		return;
	}
	
	LoggerAddMessage("\tsucceeded");
	LoggerAddMessage("");
	
	// Log the schedule that just got loaded.
	ScheduleLogLoaded();
	
	s_ScheduleInitialized = true;
}

// Uninitialize the schedule.
// 
void ScheduleUninitialize()
{
	if (s_ScheduleInitialized == false)
	{
		return;
	}
	
	s_ScheduleInitialized = false;
	
	s_ScheduleEvents.clear();
}

// Start the schedule.
//
void ScheduleStart()
{
	// Make sure it's initialized.
	if (s_ScheduleInitialized == false)
	{
		return;
	}
	
	// Make sure it's not running.
	if (ScheduleIsRunning() == true)
	{
		return;
	}
	
	s_ScheduleIndex = 0;
	TimerGetCurrent(s_ScheduleDelayStartTime);
	
	// Queue the sound.
	SoundAddToQueue(DATADIR "audio/sched_start.wav");
	
	LoggerAddMessage("Schedule started.");
}

// Stop the schedule.
//
void ScheduleStop()
{
	// Make sure it's initialized.
	if (s_ScheduleInitialized == false)
	{
		return;
	}
	
	// Make sure it's running.
	if (ScheduleIsRunning() == false)
	{
		return;
	}
	
	s_ScheduleIndex = UINT_MAX;
	
	// Queue the sound.
	SoundAddToQueue(DATADIR "audio/sched_stop.wav");
	
	LoggerAddMessage("Schedule stopped.");
}

// Determine whether the schedule is running.
//
bool ScheduleIsRunning()
{
	return (s_ScheduleIndex != UINT_MAX);
}

// Process the schedule.
//
void ScheduleProcess()
{
	// Make sure it's initialized.
	if (s_ScheduleInitialized == false)
	{
		return;
	}
	
	// Running?
	if (ScheduleIsRunning() == false)
	{
		return;
	}

	// Get elapsed time since delay start.
	Time l_CurrentTime;
	TimerGetCurrent(l_CurrentTime);

	auto const l_ElapsedTimeSec = TimerGetElapsedMilliseconds(s_ScheduleDelayStartTime, l_CurrentTime) / 
		1000.0f;

	// Time up?
	auto& l_Event = s_ScheduleEvents[s_ScheduleIndex];
	
	if (l_ElapsedTimeSec < l_Event.m_DelaySec)
	{
		return;
	}
	
	// Move to the next event.
	auto const l_ScheduleEventCount = static_cast<unsigned int>(s_ScheduleEvents.size());
	s_ScheduleIndex = (s_ScheduleIndex + 1) % l_ScheduleEventCount;
	
	// Set the new delay start time.
	TimerGetCurrent(s_ScheduleDelayStartTime);
	
	// Sanity check the event.
	if (l_Event.m_ControlAction.m_Action >= Control::NUM_ACTIONS)
	{
		LoggerAddMessage("Schedule moving to event %i.", s_ScheduleIndex);
		return;
	}

	// Try to find the control to perform the action.
	auto* l_Control = l_Event.m_ControlAction.GetControl();
	
	if (l_Control == nullptr) {
		
		LoggerAddMessage("Schedule couldn't find control \"%s\". Moving to event %i.", 
			l_Event.m_ControlAction.m_ControlName, s_ScheduleIndex);
		return;
	}
		
	// Perform the action.
	l_Control->SetDesiredAction(l_Event.m_ControlAction.m_Action, Control::MODE_TIMED);
	
	LoggerAddMessage("Schedule moving to event %i.", s_ScheduleIndex);
}
