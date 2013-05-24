#include "video/SimpleVideo.h"

#include <cstdlib>

// ========================================
// Public
// ========================================

// static
SimpleVideo* SimpleVideo::fromFile( const char* filename )
{
	bool bSucceeded;

	AVFormatContext* pFormatContext;
    AVCodecContext* pCodecContext;
    AVCodec* pCodec;
    AVFrame* pFrame; 
    int numBytes;
	uint8_t* buffer;

	// register all formats and codecs if not registered
	if( !s_bAllRegistered )
	{
		av_register_all();
		s_bAllRegistered = true;
	}

	// attempt to open video file
	bSucceeded = ( av_open_input_file( &pFormatContext, filename, NULL, 0, NULL ) == 0 );
	if( bSucceeded )
	{
		// retrieve stream information
		bSucceeded = ( av_find_stream_info( pFormatContext ) >= 0 );
		if( bSucceeded )
		{
			// Find the first video stream
			int i = 0;
			int videoStreamIndex = -1;
			while( ( videoStreamIndex == -1 ) && ( i < pFormatContext->nb_streams ) )
			{
				if( pFormatContext->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO )
				{
					videoStreamIndex = i;
				}
				++i;
			}

			// if we found a video stream
			if( videoStreamIndex > -1 )
			{
				// get a pointer to the codec context for the video stream
				pCodecContext = pFormatContext->streams[ videoStreamIndex ]->codec;

				// find the decoder for the video stream
				pCodec = avcodec_find_decoder( pCodecContext->codec_id );
				if( pCodec != NULL )
				{
					// open codec
					bSucceeded = ( avcodec_open( pCodecContext, pCodec ) >= 0 );
					if( bSucceeded )
					{
						// Allocate YUV and RGB video frames
						pFrame = avcodec_alloc_frame();
						if( pFrame != NULL )
						{
							// Determine required buffer size and allocate buffer
							numBytes = avpicture_get_size( PIX_FMT_RGB24, pCodecContext->width, pCodecContext->height );
							buffer = new uint8_t[ numBytes ];							

							SimpleVideo* pOutput = new SimpleVideo( filename, pFormatContext, videoStreamIndex,
								pCodecContext, pFrame, buffer );
							if( pOutput != NULL )
							{
								return pOutput;
							}

							av_free( pFrame );
						}

						// close the codec in case of failure
						avcodec_close( pCodecContext );
					}
				}
			}
		}

		// close the video file in case of failure
		av_close_input_file( pFormatContext );
	}

	return NULL;
}

// virtual
SimpleVideo::~SimpleVideo()
{
	delete[] m_aBuffer;
	av_free( m_pFrame );
	avcodec_close( m_pCodecContext );
	av_close_input_file( m_pFormatContext );
}

AVFrame* SimpleVideo::allocateFrame()
{
	AVFrame* pOutput = avcodec_alloc_frame();

	// Assign appropriate parts of buffer to image planes in pFrameRGB
	avpicture_fill( ( AVPicture* )pOutput, m_aBuffer, PIX_FMT_RGB24, m_pCodecContext->width, m_pCodecContext->height );

	return pOutput;
}

// and use this to deallocate
void SimpleVideo::deallocateFrame( AVFrame* pFrame )
{
	av_free( pFrame );
}

bool SimpleVideo::getNextFrame( AVFrame* pOutput )
{
	if( getNextFrameRaw() )
	{
		convertFrameToRGB( pOutput );
		return true;
	}
	else
	{
		return false;
	}
}

// TODO: seeking back may go beyond edge, same with forward (it might never find a keyframe)
bool SimpleVideo::seekToFrame( int iFrameNumber, AVFrame* pOutput )
{
	if( ( iFrameNumber < 0 ) || ( iFrameNumber >= getNumFrames() ) )
	{
		return false;
	}

	bool bSucceeded = ( av_seek_frame( m_pFormatContext, m_iVideoStreamIndex, iFrameNumber, AVSEEK_FLAG_ANY ) >= 0 );
	if( bSucceeded )
	{
		bSucceeded = getNextFrameRaw();
		if( bSucceeded )
		{
			if( m_pFrame->key_frame == 1 )
			{
				convertFrameToRGB( pOutput );
				return true;
			}
			else
			{
				// seek backwards until I see a keyframe
				int currentFrameNumber = iFrameNumber - 1;
				av_seek_frame( m_pFormatContext, m_iVideoStreamIndex, currentFrameNumber, AVSEEK_FLAG_ANY );
				getNextFrameRaw();
				while( m_pFrame->key_frame == 0 )
				{
					--currentFrameNumber;
					av_seek_frame( m_pFormatContext, m_iVideoStreamIndex, currentFrameNumber, AVSEEK_FLAG_ANY );
					getNextFrameRaw();
				}

				// then read forward until I get back to my frame number
				++currentFrameNumber;
				getNextFrameRaw();
				while( currentFrameNumber < iFrameNumber )
				{
					++currentFrameNumber;
					getNextFrameRaw();
				}

				convertFrameToRGB( pOutput );
				return true;
			}
		}
	}

	return NULL;
}

int SimpleVideo::getWidth()
{
	return m_iWidth;
}

int SimpleVideo::getHeight()
{
	return m_iHeight;
}

int64_t SimpleVideo::getNumFrames()
{
	int64_t numFrames = m_pFormatContext->streams[ m_iVideoStreamIndex ]->duration;
	return numFrames;
}

int SimpleVideo::getFramePeriodMillis()
{
	float seconds = getFramePeriodSeconds();
	return static_cast< int >( seconds * 1000 + 0.5f );
}

float SimpleVideo::getFramePeriodSeconds()
{
	AVRational framePeriod = m_pFormatContext->streams[ m_iVideoStreamIndex ]->time_base;
	float seconds = static_cast< float >( framePeriod.num ) / framePeriod.den;

	return seconds;
}

void SimpleVideo::printFileInfo()
{
	dump_format( m_pFormatContext, 0, m_strFilename.c_str(), false );
}

// ========================================
// Private
// ========================================

bool SimpleVideo::s_bAllRegistered = false;

SimpleVideo::SimpleVideo( const char* szFilename,
						 AVFormatContext* pFormatContext, int iVideoStreamIndex,
						 AVCodecContext* pCodecContext,
						 AVFrame* pFrame,
						 uint8_t* aBuffer ) :

	m_strFilename( szFilename ),
	m_pFormatContext( pFormatContext ),
	m_iVideoStreamIndex( iVideoStreamIndex ),
	m_pCodecContext( pCodecContext ),
	m_pFrame( pFrame ),
	m_aBuffer( aBuffer ),

	m_iWidth( pCodecContext->width ),
	m_iHeight( pCodecContext->height )
{
	
}

// gets the next frame in the default color space
bool SimpleVideo::getNextFrameRaw()
{
	// frameFinished != 0 ==> frame is finished
	int frameFinished = 0;
	bool bSucceeded;
	AVPacket packet;
	
	bSucceeded = ( av_read_frame( m_pFormatContext, &packet ) >= 0 );
	// loop while it can still read (bSucceeded) and the frame is NOT done (frameFinished == 0)
	while( bSucceeded && ( frameFinished == 0 ) )
	{
		// is this a packet from the video stream?
		if( packet.stream_index == m_iVideoStreamIndex )
		{
			// Decode video frame
			avcodec_decode_video( m_pCodecContext, m_pFrame, &frameFinished, 
				packet.data, packet.size );
		}

		// Free the packet that was allocated by av_read_frame
        av_free_packet( &packet );
		bSucceeded = ( av_read_frame( m_pFormatContext, &packet ) >= 0 );
	};

	return( bSucceeded && ( frameFinished != 0 ) );
}

void SimpleVideo::convertFrameToRGB( AVFrame* pOutput )
{
	// frame is finished
	// Convert the image from its native format to RGB
	img_convert( ( AVPicture* )pOutput, PIX_FMT_RGB24, ( AVPicture* )m_pFrame,
		m_pCodecContext->pix_fmt,
		m_pCodecContext->width, m_pCodecContext->height );
}
