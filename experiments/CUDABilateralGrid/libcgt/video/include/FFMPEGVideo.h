#if 0
// TODO: refactor this into its own project

#ifndef FFMPEG_VIDEO_H
#define FFMPEG_VIDEO_H

#include "video/IVideo.h"

// stdint.h requires __STDC_CONSTANT_MACROS to be defined
// in order to define INT64_C()
// which is needed by ffmpeg

#if _WIN32
#define __STDC_CONSTANT_MACROS
#endif

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <ffmpeg/swscale.h>

#if _WIN32
#undef __STDC_CONSTANT_MACROS
#endif

// A video loaded using ffmpeg implementing the IVideo interface
class FFMPEGVideo
{
public:

	static FFMPEGVideo* fromFile( const char* filename );
	virtual ~FFMPEGVideo();

	virtual int64 numFrames();
	virtual float framePeriodMilliseconds();
	virtual float framePeriodSeconds();

	virtual int width();
	virtual int height();
	virtual int bytesPerFrame();

	// returns the internal frame counter
	virtual int64 getNextFrameIndex();
	virtual bool setNextFrameIndex( int64 frameIndex );

	// Populates dataOut with the contents of the next frame
	// and increments the internal frame counter
	// returns true if succeeded
	// and false on failure (i.e. at the end of the video stream).
	virtual bool getNextFrame( ubyte* dataOut );

private:

	FFMPEGVideo( AVFormatContext* pFormatContext, int iVideoStreamIndex,
		AVCodecContext* pCodecContext,
		AVFrame* pFrameRaw,
		AVFrame* pFrameRGB,
		SwsContext* pSWSContext );

	// reads the next frame in its internal format and stores it in m_pFrameRaw
	// on success, returns the index of the frame that was decoded
	// returns -1 on failure
	bool decodeNextFrame( int64* decodedFrameIndex );

	// converts the next frame into RGB
	void convertDecodedFrameToRGB( unsigned char* rgbOut );

	bool isDecodedFrameKey();

	// initially false
	// set to true once global ffmpeg initialization is complete
	// (initialized the first time an FFMPEGVideo is created)
	static bool s_bInitialized;

	AVFormatContext* m_pFormatContext;
	int m_videoStreamIndex;
    AVCodecContext* m_pCodecContext;
	AVFrame* m_pFrameRaw;
	AVFrame* m_pFrameRGB;
	SwsContext* m_pSWSContext; // for YUV --> RGB conversion

	// dimensions
	int m_width;
	int m_height;
	int64 m_nFrames;

	// derived units
	int m_nBytesPerFrame;
	float m_framePeriodSeconds;

	int64 m_nextFrameIndex;
};

#endif // FFMPEG_VIDEO_H
#endif