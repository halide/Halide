#ifndef IMAGE_DIRECTORY_VIDEO_H
#define IMAGE_DIRECTORY_VIDEO_H

#include <cassert>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFileInfo>

#include "common/Reference.h"
#include "common/ReferenceCountedArray.h"
#include "imageproc/Image3ub.h"
#include "imageproc/Image4f.h"
#include "video/IVideo.h"
#include "color/QImageUtils.h"
#include "imageproc/FormatConversion.h"
#include "math/MathUtils.h"


#include <QThread>

// TODO: move this into another file eventually
template< typename T >
class ImageLoaderThread : public QThread
{
public:

	// starting this thread will simply load all the images in the list 'filenames'
	// and write it into outputVector
	// the first image will be loaded at index = 'offset' and sequentially thereafter
	ImageLoaderThread( QStringList filenames )
	{
		m_filenames = filenames;
	}

	QVector< Reference< T > > frames()
	{
		return m_frames;
	}

protected:

	void run()
	{
		int nFiles = m_filenames.size();
		for( int i = 0; i < nFiles; ++i )
		{
			QString filename = m_filenames.at( i );
			m_frames.append( new T( filename ) );
		}
	}

private:

	QStringList m_filenames;
	int m_offset;
	QVector< Reference< T > > m_frames;

};

// A video from a directory of images
template< typename T >
class ImageDirectoryVideo : public IVideo
{
public:

	// TODO: make it return IVideo after fixing the IVideo interface
	static Reference< ImageDirectoryVideo< T > > fromDirectory( QString directoryName, float framePeriodMilliseconds, int nThreads );

	virtual int64 numFrames();
	virtual float framePeriodMilliseconds();
	virtual float framePeriodSeconds();

	virtual int width();
	virtual int height();
	virtual int bytesPerFrame();

	virtual int64 getNextFrameIndex();
	virtual void setNextFrameIndex( int64 frameIndex );

	virtual bool getNextFrame( UnsignedByteArray dataOut );

	virtual Reference< T > getNextFrame();

private:

	ImageDirectoryVideo( QStringList filenames, int framePeriodMilliseconds, int nThreads );

	float m_framePeriodMilliseconds;
	int m_nextFrameIndex;

	int m_width;
	int m_height;

	QVector< Reference< T > > m_frames;
};

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
template< typename T >
Reference< ImageDirectoryVideo< T > > ImageDirectoryVideo< T >::fromDirectory( QString directoryName,
																	float framePeriodMilliseconds,
																	int nThreads )
{
	// check if it's a directory
	QFileInfo qfi( directoryName );
	if( !( qfi.isDir() ) )
	{
		fprintf( stderr, "%s is not a directory.\n", qPrintable( directoryName ) );
		assert( false );
		return NULL;
	}

	// ok, it's a directory, create it and take a look
	// get all the filenames that are images, in sorted order
	QStringList nameFilters;
	nameFilters << "*.png" << "*.bmp" << "*.ppm" << "*.pgm" << "*.jpg";

	QDir directory( directoryName );	
	directory.setSorting( QDir::Name );

	QStringList filenames = directory.entryList( nameFilters, QDir::Files );
	if( filenames.size() < 1 )
	{
		fprintf( stderr, "No valid images found in %s.\n", qPrintable( directoryName ) );
		assert( false );
		return NULL;
	}

	// append the directoryname to all the files
	for( int i = 0; i < filenames.size(); ++i )
	{
		filenames[ i ] = directoryName + "/" + filenames[ i ];
	}

	return new ImageDirectoryVideo< T >( filenames, framePeriodMilliseconds, nThreads );
}

// virtual
template< typename T >
int64 ImageDirectoryVideo< T >::numFrames()
{
	return m_frames.size();
}

// virtual
template< typename T >
float ImageDirectoryVideo< T >::framePeriodMilliseconds()
{
	return m_framePeriodMilliseconds;
}

// virtual
template< typename T >
float ImageDirectoryVideo< T >::framePeriodSeconds()
{
	return( m_framePeriodMilliseconds / 1000.f );
}

// virtual
template< typename T >
int ImageDirectoryVideo< T >::width()
{
	return m_width;
}

// virtual
template< typename T >
int ImageDirectoryVideo< T >::height()
{
	return m_height;
}

// virtual
template< typename T >
int ImageDirectoryVideo< T >::bytesPerFrame()
{
	return( 3 * width() * height() );
}

// virtual
template< typename T >
int64 ImageDirectoryVideo< T >::getNextFrameIndex()
{
	return m_nextFrameIndex;
}

// virtual
template< typename T >
void ImageDirectoryVideo< T >::setNextFrameIndex( int64 frameIndex )
{
	// TODO: if loop()

	m_nextFrameIndex = static_cast< int >( frameIndex );
	m_nextFrameIndex = MathUtils::clampToRangeInt
		(
		static_cast< int >( frameIndex ),
		0, numFrames()
		);
}

// virtual
template< typename T >
bool ImageDirectoryVideo< T >::getNextFrame( UnsignedByteArray dataOut )
{
	setNextFrameIndex( m_nextFrameIndex + 1 );
	return false;

	/*

	QImageUtils::convertQImageToRGBArray( m_frames[ getNextFrameIndex() ], dataOut );



	// TODO: fix this
	return true;
	*/
}

// virtual
template< typename T >
Reference< T > ImageDirectoryVideo< T >::getNextFrame()
{
	Reference< T > frame = m_frames[ getNextFrameIndex() ];

	setNextFrameIndex( m_nextFrameIndex + 1 );

	return frame;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

template< typename T >
ImageDirectoryVideo< T >::ImageDirectoryVideo( QStringList filenames,
										 int framePeriodMilliseconds,
										 int nThreads ) :

m_framePeriodMilliseconds( framePeriodMilliseconds ),
m_nextFrameIndex( 0 )

{
	int filesPerThread = filenames.size() / nThreads;

	QVector< QStringList > batchedFilenames;
	batchedFilenames.reserve( nThreads );

	for( int threadIndex = 0; threadIndex < nThreads - 1; ++threadIndex )
	{
		int startIndex = threadIndex * filesPerThread;
		QStringList currentBatch = filenames.mid( startIndex, filesPerThread );
		batchedFilenames.append( currentBatch );
	}
	QStringList lastBatch = filenames.mid( ( nThreads - 1 ) * filesPerThread );
	batchedFilenames.append( lastBatch );

	QVector< Reference< ImageLoaderThread< T > > > threads;
	for( int i = 0; i < nThreads; ++i )
	{
		threads.append( new ImageLoaderThread< T >( batchedFilenames[ i ] ) );
	}

	for( int i = 0; i < nThreads; ++i )
	{
		threads[ i ]->start();
	}

	for( int i = 0; i < nThreads; ++i )
	{
		threads[ i ]->wait();
		m_frames += threads[ i ]->frames();
	}

	m_width = m_frames[ 0 ]->width();
	m_height = m_frames[ 0 ]->height();
}

#endif // IMAGE_DIRECTORY_VIDEO_H
