#ifndef Q_DECODER_THREAD_H
#define Q_DECODER_THREAD_H

#include <QSemaphore>
#include <QThread>
#include <QVector>
#include <QMutex>

#include "common/Reference.h"
#include "video/IVideo.h"

class QDecoderThread : public QThread
{
	Q_OBJECT

public:

    QDecoderThread( Reference< IVideo > rVideo,
		uint bufferSize,
		QObject* parent = NULL );

    virtual ~QDecoderThread();

	// returns the next frame and dequeues it
	// and optionally, the index of the frame
	// Pass in NULL to ignore the frame index
	UnsignedByteArray getNextFrame( int64* pFrameIndex = NULL );

public slots:

	void setNextFrameIndex( int64 iFrameIndex );
	void stop();

protected:

	virtual void run();

private:

	void decodeNextFrameIntoBuffer();

	Reference< IVideo > m_rVideo;

	uint m_uiBufferSize;

	QVector< UnsignedByteArray > m_qvBufferedFrames;
	QVector< int > m_qvBufferedFrameIndices;

	QSemaphore m_nSlotsFree; // # slots available for the producer to write to
	QSemaphore m_nSlotsFilled; // # slots written to by producer and not yet read by consumer

	uint m_uiHeadIndex; // where to read from
	uint m_uiTailIndex; // where to write to

	Reference< QMutex > m_rSeekMutex;

	volatile bool m_bRunning;
};

#endif // Q_DECODERTHREAD_H
