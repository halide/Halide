#pragma once

#include <windows.h>
#include <XInput.h>

#include <QObject>

#include <common/BasicTypes.h>

// Simple event-driven wrapper around an Xbox 360 gamepad
// Periodically call sampleState() (i.e. in an event loop)
// to take samples of the state
//
// if the state has changed, buttonStateChanged() will emit
class XboxController : public QObject
{
	Q_OBJECT

public:

	XboxController( int userIndex = 0, QObject* parent = 0 );

	int userIndex();
	bool isConnected();
	XINPUT_STATE getState();
	void setVibration( ushort leftMotor, ushort rightMotor );

	// samples the state of the controller
	// if there are any changes compared to the internally latched state
	// emits a signal
	void sampleState();

	// returns true if button went from up -> down
	static bool buttonPressed( ushort button, ushort changes, ushort state );

	// returns true if button went from down -> up
	static bool buttonReleased( ushort button, ushort changes, ushort state );

signals:

	// emitted when the button states have changed
	// changes is a bit mask of which buttons have been changed
	// state indicates the current state
	void buttonStateChanged( ushort changes, ushort state );

private:

	bool m_isFirst;
	XINPUT_STATE m_latchedState;

	int m_userIndex;
};