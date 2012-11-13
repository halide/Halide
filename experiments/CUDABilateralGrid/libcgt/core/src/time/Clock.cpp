#include "time/Clock.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

Clock::Clock()
{
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency( &frequency );
	m_liFrequency = ( float )( frequency.QuadPart );
}

int64 Clock::getCounterValue() const
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter( &counter );
	return counter.QuadPart;
}

float Clock::getFrequency() const
{
	return m_liFrequency;
}

float Clock::convertIntervalToMillis( int64 interval ) const
{
	float seconds = ( ( float )interval ) / m_liFrequency;
	return( seconds * 1000.f );
}

int64 Clock::convertMillisToCounterInterval( float millis ) const
{
	float seconds = millis / 1000.f;
	float counts = seconds * m_liFrequency;
	return( ( int64 )counts );
}

#else

Clock::Clock()
{
	m_liFrequency = 1000000.f;
}

int64 Clock::getCounterValue() const
{
	timeval timeVal;
	timezone timeZone;

	gettimeofday( &timeVal, &timeZone );

	long int seconds = timeVal.tv_sec;
	long int microseconds = timeVal.tv_usec;

	// return in microseconds
	return( 1000000 * seconds + microseconds );
}

float Clock::getFrequency() const
{
	return m_liFrequency;
}

float Clock::convertIntervalToMillis( int64 interval ) const
{
	float seconds = ( ( float )interval ) / m_liFrequency;
	return( seconds * 1000.f );
}

int64 Clock::convertMillisToCounterInterval( float millis ) const
{
	float seconds = millis / 1000.f;
	float counts = seconds * m_liFrequency;
	return( ( int64 )counts );
}

#endif
