#include "video/QDecoderThread.h"

#include "time/StopWatch.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

QDecoderThread::QDecoderThread( Reference< IVideo > rVideo,
							   uint bufferSize,
							   QObject* parent ) :

	m_rVideo( rVideo ),

	m_uiBufferSize( bufferSize ),

	// ensure that the write thread doesn't clobber the value they just read
	m_nSlotsFree( bufferSize - 1 ),
	m_nSlotsFilled( 0 ),

	m_uiHeadIndex( 0 ),
	m_uiTailIndex( 0 ),

	m_bRunning( false ),

	QThread( parent )
{
	// allocate the arrays and hold onto one reference
	for( uint i = 0; i < bufferSize; ++i )
	{
		m_qvBufferedFrames.append( UnsignedByteArray( rVideo->bytesPerFrame(), 0 ) );
		m_qvBufferedFrameIndices.append( -1 );
	}
}

// virtual
QDecoderThread::~QDecoderThread()
{

}

UnsignedByteArray QDecoderThread::getNextFrame( int64* pFrameIndex )
{
#if _DEBUG
	StopWatch w;
#endif

	m_nSlotsFilled.acquire();

#if _DEBUG
	fprintf( stderr, "Waited %f ms to READ next frame\n", w.millisecondsElapsed() );
#endif

	UnsignedByteArray headFrame = m_qvBufferedFrames[ m_uiHeadIndex ];
	if( pFrameIndex != NULL )
	{
		*pFrameIndex = m_qvBufferedFrameIndices[ m_uiHeadIndex ];
	}

	m_uiHeadIndex = ( m_uiHeadIndex + 1 ) % m_qvBufferedFrames.size();

	m_nSlotsFree.release();

	return headFrame;
}

//////////////////////////////////////////////////////////////////////////
// Public Slots
//////////////////////////////////////////////////////////////////////////

void QDecoderThread::setNextFrameIndex( int64 iFrameIndex )
{
	QMutexLocker locker( m_rSeekMutex );

	// reset buffer
	m_nSlotsFree.release( m_uiBufferSize - m_nSlotsFree.available() - 1 );
	m_nSlotsFilled.acquire( m_nSlotsFilled.available() );

	m_uiHeadIndex = 0;
	m_uiTailIndex = 0;

	for( int i = 0; i < m_qvBufferedFrameIndices.size(); ++i )
	{
		m_qvBufferedFrameIndices[ i ] = -1;
	}

	m_rVideo->setNextFrameIndex( iFrameIndex );
}

void QDecoderThread::stop()
{
	m_bRunning = false;
}

// virtual
void QDecoderThread::run()
{
	m_bRunning = true;
	
	while( m_bRunning ) // can be stopped by another thread
	{
		decodeNextFrameIntoBuffer();
	}
}

void QDecoderThread::decodeNextFrameIntoBuffer()
{
	// lock to ensure that a seek is atomic
	QMutexLocker locker( m_rSeekMutex );
	m_nSlotsFree.acquire();

	// grab the tail frame and write to it
	int64 nextFrameIndex = m_rVideo->getNextFrameIndex();
	m_qvBufferedFrameIndices[ m_uiTailIndex ] = nextFrameIndex;

	UnsignedByteArray tailFrame = m_qvBufferedFrames[ m_uiTailIndex ];
	m_rVideo->getNextFrame( tailFrame );

	// increment tail pointer
	m_uiTailIndex = ( m_uiTailIndex + 1 ) % m_qvBufferedFrames.size();

	m_nSlotsFilled.release();	
}
