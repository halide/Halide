#include "XboxController.h"

#include <cassert>

XboxController::XboxController( int userIndex, QObject* parent ) :

	m_isFirst( true ),
	m_userIndex( userIndex ),
	QObject( parent )

{

}

int XboxController::userIndex()
{
	return m_userIndex;
}

bool XboxController::isConnected()
{
	XINPUT_STATE state;
	DWORD result = XInputGetState( m_userIndex, &state );

	return( result != ERROR_DEVICE_NOT_CONNECTED );
}

XINPUT_STATE XboxController::getState()
{
	XINPUT_STATE state;
	DWORD result = XInputGetState( m_userIndex, &state );

	assert( result == ERROR_SUCCESS );
	(void)result;

	return state;
}

void XboxController::setVibration( ushort leftMotor, ushort rightMotor )
{
	XINPUT_VIBRATION vibration;
	vibration.wLeftMotorSpeed = leftMotor;
	vibration.wRightMotorSpeed = rightMotor;
	XInputSetState( m_userIndex, &vibration );
}

// static
bool XboxController::buttonPressed( ushort button, ushort changes, ushort state )
{
	return( ( changes & button ) && ( state & button ) );
}

// returns true if button went from down -> up
// static
bool XboxController::buttonReleased( ushort button, ushort changes, ushort state )
{
	return( ( changes & button ) && !( state & button ) );
}

void XboxController::sampleState()
{
	XINPUT_STATE state = getState();

	// if it's not the first state
	// then process
	if( !m_isFirst )
	{
		ushort latchedButtons = m_latchedState.Gamepad.wButtons;
		ushort currentButtons = state.Gamepad.wButtons;

		ushort changes = latchedButtons ^ currentButtons;

		if( changes != 0 )
		{
			emit buttonStateChanged( changes, currentButtons );
		}
	}

	// replace previous state
	m_latchedState = state;
	m_isFirst = false;
}