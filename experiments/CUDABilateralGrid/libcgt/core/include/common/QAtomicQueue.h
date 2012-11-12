#ifndef Q_ATOMIC_QUEUE_H
#define Q_ATOMIC_QUEUE_H

#include <QQueue>
#include <QSemaphore>

#include "common/ReferenceCountedArray.h"

// An atomic FIFO queue that guarantees atomicity
// for ONE producer thread and ONE consumer thread

template< typename T >
class QAtomicQueue
{
public:

	QAtomicQueue( uint nItems );

	uint bufferSize();

	void enqueue( const T& item );
	T dequeue();

	int available();

private:

	ReferenceCountedArray< T > m_raBuffer;

	QSemaphore m_nSlotsFree; // # slots available for the producer to write to
	QSemaphore m_nSlotsFilled; // # slots written to by producer and not yet read by consumer

	uint m_uiHeadIndex; // where to read from
	uint m_uiTailIndex; // where to write to
};

template< typename T >
QAtomicQueue< T >::QAtomicQueue( uint nItems ) :

	m_nSlotsFree( nItems ),
	m_nSlotsFilled( 0 ),
	m_raBuffer( nItems ),

	m_uiHeadIndex( 0 ),
	m_uiTailIndex( 0 )
{

}

template< typename T >
uint QAtomicQueue< T >::bufferSize()
{
	return m_raBuffer.length();
}

template< typename T >
void QAtomicQueue< T >::enqueue( const T& item )
{
	m_nSlotsFree.acquire();

	// push item onto array
	m_raBuffer[ m_uiTailIndex ] = item;
	m_uiTailIndex = ( m_uiTailIndex + 1 ) % m_raBuffer.length();

	m_nSlotsFilled.release();
}

template< typename T >
T QAtomicQueue< T >::dequeue()
{
	m_nSlotsFilled.acquire();

	T head = m_raBuffer[ m_uiHeadIndex ];
	m_uiHeadIndex = ( m_uiHeadIndex + 1 ) % m_raBuffer.length();

	m_nSlotsFree.release();

	return head;
}

template< typename T >
int QAtomicQueue< T >::available()
{
	return m_nSlotsFilled.available();
}

#endif // Q_ATOMIC_QUEUE_H
