#ifndef STOP_WATCH_H
#define STOP_WATCH_H

#include "Clock.h"

class StopWatch
{
public:

	StopWatch();

	void reset();
	float millisecondsElapsed();
	
private:

	Clock m_clock;
	int64 m_iResetTime;

};

#endif // STOP_WATCH_H
