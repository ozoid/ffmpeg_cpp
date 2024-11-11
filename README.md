# FFmpeg C++ implementation

There seems to be a lot of history with FFmpeg libraries, so much so, that there is no clear example using the latest FFmpeg API as far as I can find.
Years worth of stack overflow answers all with a different version of FFmpeg API or subtle differences that failed in newer versions.
This is my working attempt at playing videos and streams using the FFmpeg library in C++.
Tested with FFmpeg version: 6.1.1

First create an instance of the FFmpeg class, then run Init().
The code separates loading a file from the decoding. Loading/connecting of video, streams and webcams can be initiated with loadVideoFile(filename,isDevice), passing in the name of the file or device and setting the isDevice boolean to true if the device is a webcam. The function loadVideoFileAsync(filename,isDevice) will start a thread and load the file or stream returning immediately.

Once a file or stream is loaded isLoaded() will return true and the VideoStatus will be LOADED.
To get a frame of video use the getFrameData(uint8_t& data); function passing in a suitable array to store the image.
The returned image will be in RGBA format.
The getFrameData function will regulate the images in time with the video framerate, calling the function too quckly will return the same image.

Once loaded the video or stream can be controlled with: play(), pause(), stop(), seekTo(timestamp), enableLooping(true). Stop will stop the video and rewind to the beginning.
The video status is reflected in the VideoStatus enum, and isPlaying() isLooping(), isLoaded(), isFailed() functions.
To ensure a video stream is as near to realtime as possible, run the flushBuffers() function after loading and before playing.

Simple Example:

    FFmpeg ff;
    ff.Init();
    ff.loadVideoFile("Terminator2.mp4",false);
    std::vector<uint8_t> image(ff.frameWidth * ff.frameHeight * 4);
    ff.getFrameData(image);
    ...
    ff.deInit();
    
