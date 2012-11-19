#ifndef QGAMELOOP_H
#define QGAMELOOP_H

#include <QObject>
#include <time/Clock.h>

class QGameLoop : public QObject
{
	Q_OBJECT

public:

	// Creates a new game loop
	// periodMillis is the desired period for one frame in milliseconds
	// nDelaysPerYield is the number of times a cycle can run overtime before yielding the thread
	// iMaxFrameSkips is the maximum number of frames that can be skipped without rendering
	QGameLoop( float periodMillis = 16, int nDelaysPerYield = 16, int iMaxFrameSkips = 5, QObject* parent = 0 );

	virtual ~QGameLoop();

	// starts the game loop
	void start();

	// starts the game loop and run as fast as possible
	// does NOT sleep in order to respect the desired frame period
	// useful for benchmarking
	void startNoSleep();

	// returns pause state
	bool isPaused();

	void setFramePeriod( float millis );

public slots:

	// kills the game loop
	void stop();	

	// sets the game loop's pause state
	void pause();

	// sets the game loop's pause state
	void unpause();

	// sets the game loop's pause state
	void setPaused( bool b );

	// toggles the game loop's pause state
	void togglePaused();

protected:	

	// update the state
	// called approximately once every period
	virtual void updateState();

	// draws the updated state
	// called approximately once every period
	virtual void draw();

private:

	bool m_bRunning;
	bool m_bIsPaused;

	int m_nDelaysPerYield;
	int m_iMaxFrameSkips;

	int64 m_startTime;
	int64 m_period;
	Clock m_clock;
};

#endif // QGAMELOOP_H
