#ifndef CLOCK_H
#define CLOCK_H

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include <common/BasicTypes.h>

class Clock
{
public:

	Clock();

	int64 getCounterValue() const;
	float getFrequency() const;

	float convertIntervalToMillis( int64 interval ) const;
	int64 convertMillisToCounterInterval( float millis ) const;

private:

	float m_liFrequency;
};

#endif // CLOCK_H
