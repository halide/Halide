#if 0

#include "video/FFMPEGVideo.h"

#include <cassert>

#include "math/Arithmetic.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
FFMPEGVideo* FFMPEGVideo::fromFile( const char* filename )
{
	// one time registration
	if( !( FFMPEGVideo::s_bInitialized ) )
	{
		av_register_all();
		FFMPEGVideo::s_bInitialized = true;
	}

	int retVal;

	AVFormatContext* pFormatContext;
    AVCodecContext* pCodecContext;
    AVCodec* pCodec;
	AVFrame* pFrameRaw;
	AVFrame* pFrameRGB;	

	// Open the file and examine the header
	// populates pFormatContext
	retVal = av_open_input_file
	(
		&pFormatContext, // output context
		filename, // filename
		NULL, // format, NULL --> auto-detect, otherwise, forces file format
		0, // buffer size, 0 --> auto-select
		NULL // format options, NULL --> auto-detect, otherwise force decode options
	);

	if( retVal == 0 ) // if succeeded
	{
		// Retrieve stream information
		// populates pFormatContext->streams with data

		retVal = av_find_stream_info( pFormatContext );
		if( retVal >= 0 ) // if succeeded
		{
			// TODO: let the user select which stream
			// Find the first video stream
			uint i = 0;
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
			// load its codec context
			if( videoStreamIndex > -1 )
			{
				// get a pointer to the codec context for the video stream
				pCodecContext = pFormatContext->streams[ videoStreamIndex ]->codec;

				// find a codec for the codec context
				pCodec = avcodec_find_decoder( pCodecContext->codec_id );
				if( pCodec != NULL )
				{
					// ok we found a codec, try opening it
					retVal = avcodec_open( pCodecContext, pCodec );
					if( retVal >= 0 )
					{
						// Allocate a frame for the incoming data						
						pFrameRaw = avcodec_alloc_frame();
						if( pFrameRaw != NULL )
						{
							// Allocate another for RGB
							pFrameRGB = avcodec_alloc_frame();
							if( pFrameRGB != NULL )
							{
								// Note: PixelFormats are in avutil.h
								// Note: flags are in swscale.h
								SwsContext* pSWSContext = sws_getContext
								(
									pCodecContext->width, pCodecContext->height, // source width and height
									pCodecContext->pix_fmt, // source format
									pCodecContext->width, pCodecContext->height, // destination width and height
									PIX_FMT_RGB24, // destination format
									SWS_POINT, // flags
									NULL, // source filter, NULL --> default
									NULL, // destination filter, NULL --> default
									NULL // filter parameters, NULL --> default
								);

								if( pSWSContext != NULL )
								{
									FFMPEGVideo* video = new FFMPEGVideo
									(
										pFormatContext,
										videoStreamIndex,
										pCodecContext,
										pFrameRaw,
										pFrameRGB,
										pSWSContext
									);

									if( video != NULL )
									{
										return video;										
									}
									else
									{
										fprintf( stderr, "Out of memory allocating video object!\n" );
									}

									sws_freeContext( pSWSContext );
								}
								else
								{
									fprintf( stderr, "Error creating RGB conversion context!\n" );
								}

								av_free( pFrameRGB );
							}
							else
							{
								fprintf( stderr, "Error allocating RGB frame!\n" );
							}
							
							av_free( pFrameRaw );
						}
					}
					else
					{
						fprintf( stderr, "Error opening codec!\n" );
					}
				}
				else
				{
					fprintf( stderr, "Unsupported codec!\n" );
				}
			}			
			else
			{
				fprintf( stderr, "File contains no video streams!\n" );
			}
		}
		else
		{
			fprintf( stderr, "Error parsing stream information!\n" );
		}

		// close the video file in case of failure
		av_close_input_file( pFormatContext );
	}
	else
	{
		fprintf( stderr, "Error opening %s!\n", filename );
	}
	
	assert( false );
	return NULL;	
}

// virtual
FFMPEGVideo::~FFMPEGVideo()
{
	sws_freeContext( m_pSWSContext );
	av_free( m_pFrameRGB );
	av_free( m_pFrameRaw );
	avcodec_close( m_pCodecContext );
	av_close_input_file( m_pFormatContext );
}

// virtual
int64 FFMPEGVideo::numFrames()
{
	return m_nFrames;	
}

// virtual
float FFMPEGVideo::framePeriodMilliseconds()
{
	return( 1000.f * framePeriodSeconds() );
}

// virtual
float FFMPEGVideo::framePeriodSeconds()
{
	return m_framePeriodSeconds;
}

// virtual
int FFMPEGVideo::width()
{
	return m_width;
}

// virtual
int FFMPEGVideo::height()
{
	return m_height;
}

// virtual
int FFMPEGVideo::bytesPerFrame()
{
	return m_nBytesPerFrame;	
}

// virtual
int64 FFMPEGVideo::getNextFrameIndex()
{
	return m_nextFrameIndex;
}

// virtual
bool FFMPEGVideo::setNextFrameIndex( int64 frameIndex )
{
	// if frameIndex is out of range, then return false
	if( frameIndex < 0 || frameIndex >= m_nFrames )
	{
#if _WIN32		
		fprintf( stderr, "Cannot seek to frame %I64d, frameIndex must be between 0 and %I64d\n", frameIndex, m_nFrames );
#else
		fprintf( stderr, "Cannot seek to frame %lld, frameIndex must be between 0 and %lld\n", frameIndex, m_nFrames );
#endif
		return false;
	}

	// else if it's going to be the next frame anyway
	// then do nothing
	if( frameIndex == m_nextFrameIndex )
	{
		return true;
	}	

	// always seek to the keyframe right before t
	int seekFlags = AVSEEK_FLAG_BACKWARD;

	// tell ffmpeg to seek
	int retVal = av_seek_frame( m_pFormatContext, m_videoStreamIndex, frameIndex, seekFlags );
	if( retVal < 0 )
	{
#if _WIN32		
		fprintf( stderr, "ffmpeg error seeking to frame: %I64d\n", frameIndex );
#else
		fprintf( stderr, "ffmpeg error seeking to frame: %lld\n", frameIndex );
#endif
		return false;
	}

	// seek was successful, flush codec internal buffers
	avcodec_flush_buffers( m_pCodecContext );
	m_nextFrameIndex = frameIndex;
	return true;
}

// virtual
bool FFMPEGVideo::getNextFrame( unsigned char* dataOut )
{
	if( m_nextFrameIndex >= m_nFrames )
	{
		return false;
	}

	// TODO: can potentially accelerate this by using m_pCodexContext->hurry_up = 1
	int64 t;
	
	bool decodeSucceeded = decodeNextFrame( &t );
	while( decodeSucceeded && ( t < m_nextFrameIndex ) )
	{
		decodeSucceeded = decodeNextFrame( &t );
	}

	// if the loop was successful
	// then t = m_nextFrameIndex
	if( decodeSucceeded )
	{
		// convert the decoded frame to RGB
		convertDecodedFrameToRGB( dataOut );

		++m_nextFrameIndex;			
		return true;
	}
	else
	{
		return false;
	}	
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

FFMPEGVideo::FFMPEGVideo( AVFormatContext* pFormatContext, int videoStreamIndex,
						 AVCodecContext* pCodecContext,
						 AVFrame* pFrameRaw,
						 AVFrame* pFrameRGB,
						 SwsContext* pSWSContext ) :

m_pFormatContext( pFormatContext ),
m_videoStreamIndex( videoStreamIndex ),
m_pCodecContext( pCodecContext ),
m_pFrameRaw( pFrameRaw ),
m_pFrameRGB( pFrameRGB ),
m_pSWSContext( pSWSContext ),

m_width( pCodecContext->width ),
m_height( pCodecContext->height ),
m_nFrames( m_pFormatContext->streams[ m_videoStreamIndex ]->duration ),

m_nextFrameIndex( 0 )

{
	m_nBytesPerFrame = avpicture_get_size( PIX_FMT_RGB24, width(), height() );

	AVRational framePeriod = m_pFormatContext->streams[ m_videoStreamIndex ]->time_base;
	m_framePeriodSeconds = static_cast< float >( av_q2d( framePeriod ) );
}

bool FFMPEGVideo::decodeNextFrame( int64* decodedFrameIndex )
{
	AVPacket packet;
	bool readFrameSucceeded;	
	bool decodeSucceeded = true;

	int frameFinished = 0; // frameFinished > 0 means the frame is finished
	readFrameSucceeded = ( av_read_frame( m_pFormatContext, &packet ) >= 0 );
	// loop while it can still read (bSucceeded) and
	// the frame is NOT done (frameFinished == 0)
	while( readFrameSucceeded && ( frameFinished == 0 ) )
	{
		// printf( "decodeNextFrame: packet.dts = %I64d, packet.pts = %I64d\n", packet.dts, packet.pts );

		// is this a packet from the video stream we selected?
		if( packet.stream_index == m_videoStreamIndex )
		{
			// if so, then decode it
			int decodeReturnVal = avcodec_decode_video( m_pCodecContext, m_pFrameRaw, &frameFinished, 
				packet.data, packet.size );
			decodeSucceeded = ( decodeReturnVal > 0 );

			// we failed in decoding the video
			if( !decodeSucceeded )
			{
#if _WIN32
				fprintf( stderr, "ffmpeg error decoding video frame: %I64d\n", m_nextFrameIndex );
#else
				fprintf( stderr, "ffmpeg error decoding video frame: %lld\n", m_nextFrameIndex );
#endif
				// always free the packet that was allocated by av_read_frame
				av_free_packet( &packet );
				return false;
			}

			if( decodedFrameIndex != NULL )
			{
				*decodedFrameIndex = packet.pts; // ffmpeg uses 0-based frame indices
			}
		}

		// always free the packet that was allocated by av_read_frame
		av_free_packet( &packet );

		// if the frame isn't finished, then read another packet
		if( frameFinished == 0 )
		{
			readFrameSucceeded = ( av_read_frame( m_pFormatContext, &packet ) >= 0 );
		}
	};

	if( !readFrameSucceeded )
	{
#if _WIN32
		fprintf( stderr, "ffmpeg error reading next packet on frame: %I64d\n", m_nextFrameIndex );
#else
		fprintf( stderr, "ffmpeg error reading next packet on frame: %lld\n", m_nextFrameIndex );
#endif
		return false;
	}	
	return true;
}

void FFMPEGVideo::convertDecodedFrameToRGB( unsigned char* rgbOut )
{
	// associate buffer with pFrameRGB
	avpicture_fill( ( AVPicture* )m_pFrameRGB,
		rgbOut,
		PIX_FMT_RGB24,
		width(), height() );

	sws_scale
	(
		m_pSWSContext, // converter
		m_pFrameRaw->data, m_pFrameRaw->linesize, // source data and stride
		0, height(), // starting y and height
		m_pFrameRGB->data, m_pFrameRGB->linesize
	);
}

bool FFMPEGVideo::isDecodedFrameKey()
{
	return( m_pFrameRaw->key_frame == 1 );
}

// static
bool FFMPEGVideo::s_bInitialized = false;

#endif