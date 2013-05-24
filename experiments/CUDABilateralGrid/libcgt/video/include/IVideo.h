#ifndef IVIDEO_H
#define IVIDEO_H

#include <common/BasicTypes.h>
#include <common/ReferenceCountedArray.h>

// interface
class IVideo
{
public:

	virtual int64 numFrames() = 0;
	virtual float framePeriodMilliseconds() = 0;
	virtual float framePeriodSeconds() = 0;

	virtual int width() = 0;
	virtual int height() = 0;

	virtual int bytesPerFrame() = 0;

	// gets the index of the next frame
	virtual int64 getNextFrameIndex() = 0;

	// seeks to frameIndex
	virtual void setNextFrameIndex( int64 frameIndex ) = 0;

	// returns the next frame and advances the next frame index by 1
	// the video owns the memory to the frame data and will be deleted
	// upon the next call to getNextFrame()
	// the data format is RGB RGB RGB ...
	virtual bool getNextFrame( UnsignedByteArray dataOut ) = 0;
	
private:

};

#endif
