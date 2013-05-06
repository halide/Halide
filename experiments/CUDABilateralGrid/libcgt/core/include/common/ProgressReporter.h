#pragma once

#include <QString>

#include "math/Arithmetic.h"
#include "time/StopWatch.h"

class ProgressReporter
{
public:

	// Construct a new ProgressReporter with the prefix string "Working:"
	// a predetermined number of tasks, and a reportRate of 1%
	ProgressReporter( int nTasks );

	// Construct a new ProgressReporter given a prefix string,
	// a predetermined number of tasks, and a reportRate of 1%
	ProgressReporter( QString prefix, int nTasks );

	ProgressReporter( QString prefix, int nTasks, float reportRatePercent );

	QString notifyAndGetProgressString();
	void notifyAndPrintProgressString();
	void notifyTaskCompleted();

	QString getProgressString();

	float percentComplete();
	bool isComplete();
	int numTasksRemaining();
	float approximateMillisecondsRemaining();
	float averageMillisecondsPerTask();

private:

	void initialize( QString prefix, int nTasks, float reportRatePercent );

	QString m_prefix;
	int m_nTasks;
	float m_reportRatePercent;

	float m_totalMillisecondsElapsed;
	StopWatch m_stopwatch;
	float m_previousTaskCompletedTime;

	float m_nextReportedPercent;
	int m_nTasksCompleted;
};
