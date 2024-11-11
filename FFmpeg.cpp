#include "FFmpeg.h"
#include <chrono>
#include <cstdint>
#include <thread>
//-----------------------------------------------------------------------------
FFmpeg::FFmpeg() {
    avdevice_register_all();
    pFormatCtx = NULL;
    pCodecCtx = NULL;
    pFrame = NULL;
    pFrameRGB = NULL;
    videoStreamIndex = -1;
    av_log_set_level(AV_LOG_QUIET);
    img_convert_ctx = NULL;
    videoType = FFMPEG_VIDEO;
    Init();
}
//-----------------------------------------------------------------------------
void FFmpeg::Init() {
    start = 0;
    elapsed = 0;
    doAudio = false; // temporary until working ok again..
    videoStatus = LOADING;
    videoStreamIndex = -1;
}
//-----------------------------------------------------------------------------
FFmpeg::~FFmpeg() {
        deInit();
}
//-----------------------------------------------------------------------------
int FFmpeg::loadVideoFile(const char* fname,bool isDevice) {
    videoType = FFMPEG_VIDEO;
    filename = std::string(fname);
    if (filename == "") {
        videoStatus = FAILED;
        return -1;
    }
    if (filename.size() > 5) {
        auto filestart = filename.substr(0, 6);
        if (filestart == "video=" || isDevice) {
            videoType = FFMPEG_DEVICE;
        }
        if (filestart == "udp://" || filestart == "tcp://" || filestart == "http:/" || filestart == "https:" || filestart == "file:/" || filestart == "rtsp:/" || filestart == "wmsp:/" || filestart == "mmsh:/") {
            videoType = FFMPEG_STREAM;
        }
    }
    pFormatCtx = NULL;
    pFormatCtx = avformat_alloc_context();
    
    AVDictionary* options = nullptr;
    if (videoType == FFMPEG_DEVICE) {
        AVInputFormat* pAVInputFormat = const_cast<AVInputFormat*>( av_find_input_format("dshow"));
        av_dict_set(&options, "framerate", "20", 0);
        pFormatCtx->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;
        if (avformat_open_input(&pFormatCtx, filename.c_str(), pAVInputFormat, &options) != 0) {
            printf("Failed to open Video Device: %s\n", filename.c_str());
            videoStatus = FAILED;
            return -1;
        }
    }
    if (videoType == FFMPEG_STREAM) {
        printf("Connecting:%s\n", filename.c_str());
        pFormatCtx->flags = AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS | AVFMT_FLAG_NONBLOCK;
        pFormatCtx->format_probesize = 2048;
        pFormatCtx->max_analyze_duration = 8 * AV_TIME_BASE;  //8sec
        av_dict_set(&options, "threads", "auto", 0);
        av_dict_set(&options, "fflags", "nobuffer", 0);
        av_dict_set(&options, "fflags", "fastseek", 0);
        av_dict_set(&options, "max_delay", "0", 0);
        av_dict_set(&options, "sync", "ext", 0);
        av_dict_set(&options, "flags", "low_delay", 0);
        av_dict_set(&options, "ignore_editlist", "1", 0);
        if (int oresult = avformat_open_input(&pFormatCtx, filename.c_str(), NULL, &options) != 0) {
            printf("Failed to open Video Stream: %s\n", filename.c_str());
            char a[AV_ERROR_MAX_STRING_SIZE] = { 0 };
            av_make_error_string(a, AV_ERROR_MAX_STRING_SIZE, oresult);
            videoStatus = FAILED;
            printf("%s\n", a);
            return 1; // try reconnect..
        }
    }
    if (videoType == FFMPEG_VIDEO) {
        av_dict_set(&options, "threads", "auto", 0);
        av_dict_set(&options, "hwaccel", "auto", 0);
        if (int oresult = avformat_open_input(&pFormatCtx, filename.c_str(), NULL, &options) != 0) {
            printf("Failed to open Video File: %s\n", filename.c_str());
            char a[AV_ERROR_MAX_STRING_SIZE] = { 0 };
            av_make_error_string(a, AV_ERROR_MAX_STRING_SIZE, oresult);
            printf("%s\n", a);
            videoStatus = FAILED;
            return -1;
        }
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("Failed to retrieve stream information\n");
        avformat_close_input(&pFormatCtx);
        videoStatus = FAILED;
        return -1;
    }
    for (int i = 0; i < pFormatCtx->nb_streams;i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
        }
        else if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
        }
    }

    if (videoStreamIndex == -1) {
        printf("Failed to find video stream\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }

    const AVCodec* pCodec= avcodec_find_decoder(pFormatCtx->streams[videoStreamIndex]->codecpar->codec_id);
    if (pCodec == NULL) {
        printf("Failed to find codec\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx) {
        printf("cannot get 'AVCodecContext'\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }   

    if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStreamIndex]->codecpar) < 0) {
        printf("Failed to copy codec parameters\n");
    }

    av_dict_free(&options);

    pCodecCtx->thread_count = 0;
    pCodecCtx->codec_id = pCodec->id;

    if (use_multithreading) {
        pCodecCtx->thread_count = 0;
        if (pCodec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
            pCodecCtx->thread_type = FF_THREAD_FRAME;
        else if (pCodec->capabilities & AV_CODEC_CAP_SLICE_THREADS)
            pCodecCtx->thread_type = FF_THREAD_SLICE;
        else
            pCodecCtx->thread_count = 1; //don't use multithreading
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("Failed to open codec\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }

    pFrame = av_frame_alloc();
    if (pFrame == NULL) {
        printf("Failed to allocate frame\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }
    pFrameRGB = av_frame_alloc();
    if (pFrameRGB == NULL) {
        printf("Failed to allocate copy frame\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }
   
    frameRate = static_cast<double>(pFormatCtx->streams[videoStreamIndex]->avg_frame_rate.num) / static_cast<double>(pFormatCtx->streams[videoStreamIndex]->avg_frame_rate.den);
    frameInterval = (1.0 / frameRate);
    frameWidth = pCodecCtx->width;
    frameHeight = pCodecCtx->height;
    pFrame->width = frameWidth;
    pFrame->height = frameHeight;
    pFrame->format = pCodecCtx->pix_fmt;
    pFrameRGB->width = frameWidth;
    pFrameRGB->height = frameHeight;
    pFrameRGB->format = AV_PIX_FMT_RGBA;// pCodecCtx->pix_fmt;
    if (frameHeight == 0 || frameWidth == 0) {
        printf("invalid frame size\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }
    size_t align = av_cpu_max_align();
   
    int ret = av_image_alloc(pFrameRGB->data, pFrameRGB->linesize, frameWidth, frameHeight, AV_PIX_FMT_RGBA, static_cast<int>(align)); //pFrame->pict_type
    if (ret < 0) {
        printf("Cannot Allocate Frame\n");
        videoStatus = FAILED;
        deInit();
        return -1;
    }
    
    img_convert_ctx = sws_getCachedContext(img_convert_ctx,frameWidth, frameHeight, pCodecCtx->pix_fmt, frameWidth, frameHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
   
    eof = false;
    videoStatus = LOADED;
    lastFrameTime = std::chrono::steady_clock::now();
    return 0;
}
//-----------------------------------------------------------------------------
void FFmpeg::loadVideoFileAsync(const std::string& fname, bool IsDevice) {

    loadThread = std::thread{
        [=]() {
            loadVideoFile(fname.c_str(), IsDevice);
        }
    };
    loadThread.detach();
}
//-----------------------------------------------------------------------------
void FFmpeg::flushBuffers() {
    avcodec_flush_buffers(pCodecCtx);
    avformat_flush(pFormatCtx);
}
//-----------------------------------------------------------------------------
static void logging(const char* fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
//-----------------------------------------------------------------------------
uint64_t FFmpeg::getElapsed() {
    if (videoStatus == PLAYING) {
        uint64_t ctime = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        return elapsed = ctime - start;
    }
    return 0;
}
//-----------------------------------------------------------------------------
int FFmpeg::readFrame() {
    if (videoStatus == EXITING || videoStatus == LOADING) {
        return 1;
    }
    int response;
    AVPacket packet;
    while ((response = av_read_frame(pFormatCtx, &packet)) >= 0 && ((videoStatus == PLAYING) || (videoStatus == LOADED))) {
        if (packet.stream_index == videoStreamIndex && pFormatCtx->streams[videoStreamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {

            response = avcodec_send_packet(pCodecCtx, &packet);
            if (response < 0) {
                printf("Error sending packet to decoder.\n");
                av_packet_unref(&packet);
                return 1;
            }

            response = avcodec_receive_frame(pCodecCtx, pFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                av_packet_unref(&packet);
                continue;
            }
            else if (response < 0) {
                printf("Error receiving frame from decoder.\n");
                av_packet_unref(&packet);
                return 1;
            }
            sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, frameHeight, pFrameRGB->data, pFrameRGB->linesize);
            av_packet_unref(&packet);
            return 0;
        }
        av_packet_unref(&packet);
    }

    if (looping && !videoStatus != EXITING) {
        seekTo(0);
        return readFrame();
    }
    return -1;
}
//-----------------------------------------------------------------------------
bool FFmpeg::readFirstFrame() {
    videoStatus = LOADED;
    seekTo(0);   // Ensure we're at the beginning
    AVPacket packet;
    int response;
    if ((response = av_read_frame(pFormatCtx, &packet)) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            response = avcodec_send_packet(pCodecCtx, &packet);
            if (response < 0) {
                printf("Error sending first packet to decoder.\n");
                av_packet_unref(&packet);
                return false;
            }

            response = avcodec_receive_frame(pCodecCtx, pFrame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                av_packet_unref(&packet);
                return false;
            }
            else if (response < 0) {
                printf("Error receiving first frame from decoder.\n");
                av_packet_unref(&packet);
                return false;
            }

            sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);

            av_packet_unref(&packet);
            return true;
        }
        av_packet_unref(&packet);
    }

    return false;
}
//-----------------------------------------------------------------------------
bool FFmpeg::getFrameData(std::vector<uint8_t>& data) {
    if (videoStatus == EXITING) {
        return false;
    }
    std::lock_guard<std::mutex> lock(textureMutex);
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsedTime = now - lastFrameTime;
    
    if ((elapsedTime.count() >= frameInterval) || (videoType == FFMPEG_STREAM))
    {
        int rf = readFrame();
        if (rf != 0) { // finished or error
            stop();
            return false;
        }
        if (frameHeight == 0 || frameWidth == 0) {
            stop();
            return false;
        }
        //flip and copy 
        for (int y = 0; y < frameHeight; ++y) {
            memcpy(data.data() + (y * frameWidth * 4),
                pFrameRGB->data[0] + ((frameHeight - y - 1) * pFrameRGB->linesize[0]), frameWidth * 4);
        }
        if (videoType == FFMPEG_STREAM) {
            lastFrameTime = now;
        }
        else {
            lastFrameTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(frameInterval));
            if (elapsedTime.count() >= 2 * frameInterval) {
                lastFrameTime = now;
            }
        }
        return true;
    }
    else {
        for (int y = 0; y < frameHeight; ++y) {
            memcpy(data.data() + (y * frameWidth * 4),
                pFrameRGB->data[0] + ((frameHeight - y - 1) * pFrameRGB->linesize[0]), frameWidth * 4);
        }
        return true;
    }
    return false;
}
//-----------------------------------------------------------------------------
void FFmpeg::play() {
    videoStatus = PLAYING;
}
//-----------------------------------------------------------------------------
void FFmpeg::pause() {
    videoStatus = LOADED;
}
//-----------------------------------------------------------------------------
void FFmpeg::stop() {
    videoStatus = LOADED;
    seekTo(0);
}
//-----------------------------------------------------------------------------
bool FFmpeg::isPlaying() const {
    return videoStatus == PLAYING;
}
//-----------------------------------------------------------------------------
void FFmpeg::enableLooping(bool loop) {
    looping = loop;
}
//-----------------------------------------------------------------------------
void FFmpeg::enableAudio(bool audio) {
    doAudio = audio;
}
//-----------------------------------------------------------------------------
bool FFmpeg::isLooping() const {
    return looping;
}
//-----------------------------------------------------------------------------
bool FFmpeg::isLoaded() const {
    return videoStatus == LOADED || videoStatus == PLAYING;
}
//-----------------------------------------------------------------------------
bool FFmpeg::isFailed() const {
    return videoStatus == FAILED;
}
//-----------------------------------------------------------------------------
void FFmpeg::Exit() {
    videoStatus = EXITING;
}
//-----------------------------------------------------------------------------
void FFmpeg::seekTo(int timestamp) {
    if (pCodecCtx) {
        av_seek_frame(pFormatCtx, videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(pCodecCtx);
    }
}
//-----------------------------------------------------------------------------
void FFmpeg::deInit() {
    videoStatus == EXITING;
    pause();
    if (pFrameRGB != nullptr) {
        av_freep(&pFrameRGB->data[0]);
        av_frame_free(&pFrameRGB);
    }
    if (pFrame != nullptr) {
        av_frame_free(&pFrame);
    }
    if (img_convert_ctx != nullptr) {
        sws_freeContext(img_convert_ctx);
    }
    if (pFormatCtx != nullptr) {
        avformat_close_input(&pFormatCtx);
    }
}
//-----------------------------------------------------------------------------
