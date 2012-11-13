#include "time/CrossPlatformSleep.h"

#include "math/Arithmetic.h"

// static
void CrossPlatformSleep::sleep( float seconds )
{
	unsigned long microseconds = Arithmetic::roundToInt( seconds * 1000000 );
	QThread::sleep( microseconds );
}

// static
void CrossPlatformSleep::sleep( unsigned long seconds )
{
	QThread::sleep( seconds );
}

// static
void CrossPlatformSleep::msleep( unsigned long milliseconds )
{
	QThread::msleep( milliseconds );
}

// static
void CrossPlatformSleep::usleep( unsigned long microseconds )
{
	QThread::usleep( microseconds );
}

// static
void CrossPlatformSleep::yieldThread()
{
	QThread::usleep( 0 );
}
