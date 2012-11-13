#include "common/ProgressReporter.h"

ProgressReporter::ProgressReporter( int nTasks )
{
	initialize( "Working:", nTasks, 1 );
}

ProgressReporter::ProgressReporter( QString prefix, int nTasks )
{
	initialize( prefix, nTasks, 1 );
}

ProgressReporter::ProgressReporter( QString prefix, int nTasks, float reportRatePercent )
{
	initialize( prefix, nTasks, reportRatePercent );
}

QString ProgressReporter::notifyAndGetProgressString()
{
	notifyTaskCompleted();
	return getProgressString();
}

void ProgressReporter::notifyAndPrintProgressString()
{
	notifyTaskCompleted();
	if( m_reportRatePercent < 0 )
	{
		printf( "%s\n", qPrintable( getProgressString() ) );
	}
	else if( isComplete() || percentComplete() > m_nextReportedPercent )
	{
		printf( "%s\n", qPrintable( getProgressString() ) );
		m_nextReportedPercent += m_reportRatePercent;
	}
}

void ProgressReporter::notifyTaskCompleted()
{
	float now = m_stopwatch.millisecondsElapsed();
	float millisecondsForTask = now - m_previousTaskCompletedTime;
	m_previousTaskCompletedTime = now;

	m_totalMillisecondsElapsed += millisecondsForTask;

	if( m_nTasksCompleted < m_nTasks )
	{
		++m_nTasksCompleted;
	}
}        

QString ProgressReporter::getProgressString()
{
	if( numTasksRemaining() <= 0 )
	{
		return QString( "%1 100% [done!]" ).arg( m_prefix );
	}
	else
	{
		QString timeRemainingString;
		if( approximateMillisecondsRemaining() < 1000 )
		{
			timeRemainingString = QString( "%1 ms" ).arg( Arithmetic::roundToInt( approximateMillisecondsRemaining() ) );
		}
		else
		{
			timeRemainingString = QString( "%1 s" ).arg( approximateMillisecondsRemaining() / 1000, 0, 'g', 2 );
		}

		QString timeElapsedString;
		if( m_totalMillisecondsElapsed < 1000 )
		{
			timeElapsedString = QString( "%1 ms" ).arg( m_totalMillisecondsElapsed );
		}
		else
		{
			timeElapsedString = QString( "%1 s" ).arg( m_totalMillisecondsElapsed / 1000.0f, 0, 'g', 2 );
		}

		return QString( "%1 %2% [%3 tasks left (%4), elapsed: %5]" )
			.arg( m_prefix )
			.arg( percentComplete(), 0, 'g', 3 )
			.arg( numTasksRemaining() )
			.arg( timeRemainingString )
			.arg( timeElapsedString );
	}
}

float ProgressReporter::percentComplete()
{
	return 100.0f * Arithmetic::divideIntsToFloat( m_nTasksCompleted, m_nTasks );
}

bool ProgressReporter::isComplete()
{
	return ( m_nTasksCompleted == m_nTasks );
}

int ProgressReporter::numTasksRemaining()
{
	return m_nTasks - m_nTasksCompleted;
}

float ProgressReporter::approximateMillisecondsRemaining()
{
	return numTasksRemaining() * averageMillisecondsPerTask();
}

float ProgressReporter::averageMillisecondsPerTask()
{
	return m_totalMillisecondsElapsed / m_nTasksCompleted;
}

void ProgressReporter::initialize( QString prefix, int nTasks, float reportRatePercent )
{
	if( prefix.endsWith( ":" ) )
	{
		m_prefix = prefix;
	}
	else
	{
		m_prefix = prefix + ":";
	}

	m_nTasks = nTasks;
	m_reportRatePercent = reportRatePercent;

	m_totalMillisecondsElapsed = 0;		
	m_previousTaskCompletedTime = m_stopwatch.millisecondsElapsed();

	m_nTasksCompleted = 0;
}
