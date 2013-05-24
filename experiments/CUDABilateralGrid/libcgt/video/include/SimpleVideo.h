#ifndef SIMPLE_VIDEO_H
#define SIMPLE_VIDEO_H

#ifndef INT64_C
#define INT64_C(val) val##i64
#endif

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>

#include <string>

#include <common/BasicTypes.h>

class SimpleVideo
{
public:

	static SimpleVideo* fromFile( const char* filename );

	virtual ~SimpleVideo();	

	// when buffering up video frames,
	// call this to allocate each new frame for the buffer
	AVFrame* allocateFrame();
	
	// and use this to deallocate
	void deallocateFrame( AVFrame* pFrame );

	// returns true if it got a frame
	// and false at end of stream (pOutput does NOT get modified)
	bool getNextFrame( AVFrame* pOutput );

	bool seekToFrame( int iFrameNumber, AVFrame* pOutput );

	int getWidth();
	int getHeight();

	int64 getNumFrames();
	int getFramePeriodMillis();
	float getFramePeriodSeconds();

	void printFileInfo();

private:

	static bool s_bAllRegistered;
	
	std::string m_strFilename;

	AVPacket packet;
    int frameFinished;

	AVFormatContext* m_pFormatContext;
	int m_iVideoStreamIndex;
    AVCodecContext* m_pCodecContext;
    AVFrame* m_pFrame; 
	uint8_t* m_aBuffer;

	int m_iWidth;
	int m_iHeight;

	SimpleVideo( const char* szFilename,
		AVFormatContext* pFormatContext, int iVideoStreamIndex,
		AVCodecContext* pCodecContext,
		AVFrame* pFrame,
		uint8_t* pBuffer );

	// gets the next frame in the default color space
	bool getNextFrameRaw();
	
	// converts m_pFrame to m_pFrameRGB
	void convertFrameToRGB( AVFrame* pOutput );
};

#endif
