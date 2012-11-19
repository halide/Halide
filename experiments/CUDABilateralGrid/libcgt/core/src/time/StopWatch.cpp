#include "time/StopWatch.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

StopWatch::StopWatch()
{
	reset();
}

void StopWatch::reset()
{
	m_iResetTime = m_clock.getCounterValue();
}

float StopWatch::millisecondsElapsed()
{
	int64 now = m_clock.getCounterValue();
	return m_clock.convertIntervalToMillis( now - m_iResetTime );
}
