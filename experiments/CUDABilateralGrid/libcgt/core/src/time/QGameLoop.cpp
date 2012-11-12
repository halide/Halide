#include "time/QGameLoop.h"

#include <cassert>
#include <QApplication>

#include "math/Arithmetic.h"
#include "time/CrossPlatformSleep.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

QGameLoop::QGameLoop( float periodMillis, int nDelaysPerYield, int iMaxFrameSkips, QObject* parent ) :

	m_bRunning( false ),
	m_bIsPaused( false ),

	m_nDelaysPerYield( nDelaysPerYield ),
	m_iMaxFrameSkips( iMaxFrameSkips ),

	QObject( parent )
{
	m_period = m_clock.convertMillisToCounterInterval( periodMillis );
}

// virtual
QGameLoop::~QGameLoop()
{

}

void QGameLoop::start()
{
	int64 beforeTime; // time before update and render
	int64 afterTime; // time after update and render
	int64 timeDiff; // how long it took to update and render
	int64 sleepTime; // if we have time left in the frame, how long to sleep for
	int64 overSleepTime = 0; // we may have overslept, keep track and deduct it from the next frame's sleep time
	int64 excess = 0; // when we take longer to update+render than one frame period, accumulate the *excess* time here
	int nDelays = 0; // the number of times we took longer than a frame period.  When it passes a threshold, yield the thread to not hog the CPU

	m_startTime = m_clock.getCounterValue();
	beforeTime = m_startTime;

	m_bRunning = true;
	QCoreApplication* pApp = QApplication::instance();

	while( m_bRunning )
	{
		if( pApp->hasPendingEvents() )
		{
			pApp->processEvents();
		}

		updateState();
		draw();

		afterTime = m_clock.getCounterValue();
		timeDiff = afterTime - beforeTime;
		sleepTime = ( m_period - timeDiff ) - overSleepTime;		

		if( sleepTime > 0 ) // sleeptime > 0: some time left in this frame (hopefully the usual case)
		{
			float sleepTimeMillis = m_clock.convertIntervalToMillis( sleepTime );
			CrossPlatformSleep::msleep( Arithmetic::roundToInt( sleepTimeMillis ) );
			overSleepTime = ( m_clock.getCounterValue() - afterTime ) - sleepTime;
		}
		else // sleeptime <= 0: the frame took longer than the period allowed
		{
			// store the excess time value
			// (sleeptime is negative, we subtract it to store)
			excess -= sleepTime;
			overSleepTime = 0;

			// since we're already behind, re-render immediately
			// but we don't want to hog the whole CPU
			// instead, each time we take too long, add to 
			++nDelays;
			if( nDelays >= m_nDelaysPerYield )
			{
				CrossPlatformSleep::yieldThread();
				nDelays = 0;
			}
		}

		beforeTime = m_clock.getCounterValue();

		// if the accumulated excess time exceeds one frame period
		// then at least one frame has been lost
		// in this case, don't render, just update the world

		int nFrameSkips = 0;
		while( ( excess > m_period ) && ( nFrameSkips < m_iMaxFrameSkips ) )
		{
			excess -= m_period;
			updateState();
			++nFrameSkips;
		}
#if _DEBUG
		if( nFrameSkips > 0 )
		{
			fprintf( stderr, "dropped %d frames\n", nFrameSkips );
		}
#endif
	}
}

void QGameLoop::startNoSleep()
{
	m_bRunning = true;
	QCoreApplication* pApp = QApplication::instance();

	while( m_bRunning )
	{
		if( pApp->hasPendingEvents() )
		{
			pApp->processEvents();
		}

		updateState();
		draw();
	}
}

bool QGameLoop::isPaused()
{
	return m_bIsPaused;
}

void QGameLoop::setFramePeriod( float millis )
{
	m_period = m_clock.convertMillisToCounterInterval( millis );
}

//////////////////////////////////////////////////////////////////////////
// Public Slots
//////////////////////////////////////////////////////////////////////////

void QGameLoop::stop()
{
	m_bRunning = false;
}

void QGameLoop::pause()
{
	m_bIsPaused = true;
}

void QGameLoop::unpause()
{
	m_bIsPaused = false;
}

void QGameLoop::setPaused( bool b )
{
	m_bIsPaused = b;
}

void QGameLoop::togglePaused()
{
	m_bIsPaused = !m_bIsPaused;
}

//////////////////////////////////////////////////////////////////////////
// Protected
//////////////////////////////////////////////////////////////////////////

// virtual
void QGameLoop::updateState()
{

}

// virtual
void QGameLoop::draw()
{

}
