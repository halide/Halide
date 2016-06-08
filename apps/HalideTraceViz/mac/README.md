# HalideTraceViz
## mPerpetuo, inc.
Jaime Rios

### What is this?
A Xcode project illustrating the needed components to successfully render a Halide schedule to a mp4 movie.

### Example usage
There is a VisualizeRunPipeline.sh script that shows the usage of the HalideTraceViz application. Examine the script and ask Jaime any questions you may have.

### Requirements
Things you need for this to run:

* AOT generator app
* App to run the filter
* Binaries all in the same directory

### Additional dependencies
* 7-zip, required to decompress ffmpeg distribution
* ffmpeg binary, available at: http://evermeet.cx/ffmpeg/ffmpeg-3.0.2.7z

There is a script, DownloadRequiredFiles.sh, in the Scripts directory, that will download the ffmpeg binary for you; you can use this to get the binary and place it into the correct directory.