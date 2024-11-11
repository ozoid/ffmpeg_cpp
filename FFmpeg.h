#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <queue>
extern "C" {
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libavutil/imgutils.h>
	#include <libavutil/opt.h>
    #include <libavutil/log.h>
	#include <libavutil/cpu.h>
	#include <libavdevice/avdevice.h>
	#include <libswscale/swscale.h>
	#include <libswresample/swresample.h>
}

enum FFVideoType {
	FFMPEG_VIDEO,
	FFMPEG_STREAM,
	FFMPEG_DEVICE,
	FFMPEG_IMAGE,
};
enum FFVideoStatus {
	LOADING,
	LOADED,
	FAILED,
	PLAYING,
	LOOPING,
	EXITING
};
class FFmpeg
{
public:
	FFmpeg();
	~FFmpeg();
	
	int loadVideoFile(const char* filename, bool isDevice);
	void loadVideoFileAsync(const std::string& filename, bool IsDevice);
	int readFrame();
	void play();
	void pause();
	void stop();
	bool isPlaying() const;
	void enableLooping(bool loop);
	void enableAudio(bool audio);
	bool isLooping() const;
	bool isLoaded() const;
	bool isFailed() const;
	bool readFirstFrame();
	void seekTo(int timestamp = 0);
	uint64_t getElapsed();
	bool getFrameData(std::vector<uint8_t>& data);
	void flushBuffers();
	void Exit();
	double				frameRate;  // e.g. 25fps
	int					frameWidth, frameHeight;
	double				frameInterval; // e.g. 0.04;
	std::mutex			textureMutex;
	FFVideoType			videoType;
	std::atomic<FFVideoStatus> videoStatus{ LOADING };
private:
        void Init();
	void deInit();
	//common
	std::chrono::steady_clock::time_point lastFrameTime;
	uint64_t start;
	uint64_t elapsed;
	std::string filename;
	
	
	bool doAudio;
	bool use_multithreading = true;
	std::thread loadThread;

	bool eof = false;
	bool looping = false ;

	//video
	int					videoStreamIndex;
	
	AVFormatContext*	pFormatCtx;
	AVCodecContext*		pCodecCtx;
	AVFrame*			pFrame; // raw frame
	AVFrame*			pFrameRGB; //converted frame
	struct SwsContext*	img_convert_ctx;

	//audio
	int audioStreamIndex;

};

