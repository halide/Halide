#ifndef PERFORMANCE_COLLECTOR_H
#define PERFORMANCE_COLLECTOR_H

#include <QString>
#include <QHash>

#include "Clock.h"

class PerformanceCollector
{
public:

	PerformanceCollector();

	// register an event for performance collection
	// its event counter starts of as reset
	void registerEvent( QString name );

	// unregister an event for performance collection
	void unregisterEvent( QString name );

	// resets the statistics for an event
	void resetEvent( QString name );

	// call each time an event starts
	void beginEvent( QString name );

	// call each time an event ends
	void endEvent( QString name );

	// returns the average time spent on an event
	// over all beginEvent/endEvent pairs
	float averageTimeMilliseconds( QString name );

private:

	Clock m_clock;
	QHash< QString, int64 > m_eventStartTimes;	
	QHash< QString, int64 > m_eventTotalElapsedTime;
	QHash< QString, int > m_eventCounts;

};

#endif // PERFORMANCE_COLLECTOR_H
