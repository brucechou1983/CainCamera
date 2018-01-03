//
// Created by Administrator on 2018/1/3.
//

#include"jni.h"
#include <string>
#include <malloc.h>
#include "common_encoder.h"
#include "encoder_params.h"
#include "native_log.h"

using namespace std;


extern "C" {

/**
 * 初始化编码器
 * @param env
 * @param obj
 * @param videoPath_    视频路径
 * @param previewWidth  预览宽度
 * @param previewHeight 预览高度
 * @param videoWidth    录制视频宽度
 * @param videoHeight   录制视频高度
 * @param frameRate     帧率
 * @param bitRate       视频比特率
 * @param audioBitRate  音频比特率
 * @param audioSampleRate  音频采样频率
 * @return
 */
JNIEXPORT jint
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_initMediaRecorder
        (JNIEnv *env, jclass obj, jstring videoPath, jint previewWidth, jint previewHeight,
         jint videoWidth, jint videoHeight, jint frameRate, jint bitRate,
         jint audioBitRate, jint audioSampleRate);

/**
 * 开始录制
 * @param env
 * @param obj
 * @return
 */
JNIEXPORT void
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_startRecorder(JNIEnv *env, jclass obj);

/**
 * 发送YUV数据进行编码
 * @param env
 * @param obj
 * @param yuvArray
 * @return
 */
JNIEXPORT jint
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_sendYUVFrame
        (JNIEnv *env, jclass obj, jobjectArray yuvArray);

/**
 * 发送PCM数据进行编码
 * @param env
 * @param obj
 * @param pcmArray
 * @return
 */
JNIEXPORT jint
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_sendPCMFrame
        (JNIEnv *env, jclass obj, jobjectArray pcmArray);

/**
 * 发送停止命令
 * @param env
 * @param obj
 * @return
 */
JNIEXPORT void
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_stopRecorder(JNIEnv *env, jclass obj);

/**
 * 释放资源
 * @param env
 * @param obj
 * @return
 */
JNIEXPORT void
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_release(JNIEnv *env, jclass obj);

} // extern "C"


// 参数保存
EncoderParams *params;
// muxer部分
AVFormatContext *mFormatCtx;
AVOutputFormat *mOutputFormat;
// 视频编码器部分
AVCodec *mVideoCodec;
AVCodecContext *mVideoCodecContext;
AVStream *mVideoStream;
AVFrame *mVideoFrame;
AVPacket *mVideoPacket;
uint8_t  *mVideoOutBuffer;
int mVideoSize;
// 图像转换上下文
SwsContext *img_convert_ctx;

// 音频编码器部分
AVCodec *mAudioCodec;
AVCodecContext *mAudioCodecContext;
AVStream *mAudioStream;
AVFrame *mAudioFrame;
uint8_t  *mAudioOutBuffer;
// 音频转换上下文
SwrContext *samples_convert_ctx;

/**
 * 初始化编码器
 * @param env
 * @param obj
 * @param videoPath_    视频路径
 * @param previewWidth  预览宽度
 * @param previewHeight 预览高度
 * @param videoWidth    录制视频宽度
 * @param videoHeight   录制视频高度
 * @param frameRate     帧率
 * @param bitRate       视频比特率
 * @param audioBitRate  音频比特率
 * @param audioSampleRate  音频采样频率
 * @return
 */
JNIEXPORT jint
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_initMediaRecorder
        (JNIEnv *env, jclass obj, jstring videoPath_, jint previewWidth, jint previewHeight,
         jint videoWidth, jint videoHeight, jint frameRate, jint bitRate,
         jint audioBitRate, jint audioSampleRate) {

    // 配置参数
    const char * videoPath = env->GetStringUTFChars(videoPath_, 0);
    params->mMediaPath = videoPath;
    params->mPreviewWidth = previewWidth;
    params->mPreviewHeight = previewHeight;
    params->mVideoWidth = videoWidth;
    params->mVideoHeight = videoHeight;
    params->mFrameRate = frameRate;
    params->mBitRate = bitRate;
    params->mAudioBitRate = audioBitRate;
    params->mAudioSampleRate = audioSampleRate;

    // 初始化
    av_register_all();

    // 获取格式
    mOutputFormat = av_guess_format(NULL, videoPath, NULL);
    if (mOutputFormat == NULL) {
        LOGE("av_guess_format() error: Could not guess output format for %s\n", videoPath);
        return -1;
    }
    // 创建复用上下文
    mFormatCtx = avformat_alloc_context();
    if (mFormatCtx == NULL) {
        LOGE("avformat_alloc_context() error: Could not allocate format context");
        return -1;
    }
    mFormatCtx->oformat = mOutputFormat;

    // ----------------------------- 视频编码器初始化部分 --------------------------------------
    // 设置视频编码器的ID
    mOutputFormat->video_codec = AV_CODEC_ID_H264;
    // 查找编码器
    mVideoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!mVideoCodec) {
        LOGE("can not find encoder !\n");
        return -1;
    }

    // 创建视频码流
    mVideoStream = avformat_new_stream(mFormatCtx, mVideoCodec);
    if (!mVideoStream) {
        LOGE("avformat_new_stream() error: Could not allocate video stream!\n");
        return -1;
    }

    // 获取编码上下文，并设置相关参数
    mVideoCodecContext = mVideoStream->codec;
    mVideoCodecContext->codec_id = mOutputFormat->video_codec;
    mVideoCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    mVideoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

    mVideoCodecContext->width = params->mVideoWidth;
    mVideoCodecContext->height = params->mVideoHeight;

    mVideoCodecContext->bit_rate = params->mBitRate;
    mVideoCodecContext->gop_size = 30;
    mVideoCodecContext->thread_count = 12;
    mVideoCodecContext->time_base.num = 1;
    mVideoCodecContext->time_base.den = params->mFrameRate;
    mVideoCodecContext->qmin = 10;
    mVideoCodecContext->qmax = 51;
    mVideoCodecContext->max_b_frames = 0;

    // 设置H264的profile
    AVDictionary *param = 0;
    if (mVideoCodecContext->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "tune", "zerolatency", 0);
        av_opt_set(mVideoCodecContext->priv_data, "preset", "ultrafast", 0);
        av_dict_set(&param, "profile", "baseline", 0);
    }

    // 设置码流的timebase
    mVideoStream->time_base.num = 1;
    mVideoStream->time_base.den = params->mFrameRate;

    // 判断视频码流是否存在，打开编码器
    int ret = avcodec_open2(mVideoCodecContext, mVideoCodec, NULL);
    if (ret < 0) {
        LOGE("avcodec_open2() error %d: Could not open video codec.", ret);
        return -1;
    }

    // 创建视频帧
    mVideoFrame = av_frame_alloc();
    if (!mVideoFrame) {
        LOGE("av_frame_alloc() error: Could not allocate video frame.");
        return -1;
    }

    // 创建缓冲
    mVideoOutBuffer = (uint8_t *) av_malloc(avpicture_get_size(
            AV_PIX_FMT_YUV420P, mVideoCodecContext->width, mVideoCodecContext->height));
    avpicture_fill((AVPicture *) mVideoFrame, mVideoOutBuffer, AV_PIX_FMT_YUV420P,
                   mVideoCodecContext->width, mVideoCodecContext->height);


    // ------------------------------ 音频编码初始化部分 --------------------------------------------
    // 设置音频编码格式
    mOutputFormat->audio_codec = AV_CODEC_ID_AAC;

    // 创建音频编码器
    mAudioCodec = avcodec_find_encoder(mOutputFormat->audio_codec);
    if (!mAudioCodec) {
        LOGE("avcodec_find_encoder() error: Audio codec not found.");
        return -1;
    }

    // 创建音频码流
    mAudioStream = avformat_new_stream(mFormatCtx, mAudioCodec);
    if (!mAudioStream) {
        LOGE("avformat_new_stream() error: Could not allocate audio stream.");
        return -1;
    }

    // 获取音频编码上下文
    mAudioCodecContext = mAudioStream->codec;
    mAudioCodecContext->codec_id = mOutputFormat->audio_codec;
    mAudioCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
    mAudioCodecContext->bit_rate = params->mAudioBitRate;
    mAudioCodecContext->sample_rate = params->mAudioSampleRate;
    mAudioCodecContext->channel_layout = AV_CH_LAYOUT_MONO;
    mAudioCodecContext->channels =
            av_get_channel_layout_nb_channels(mAudioCodecContext->channel_layout);
    mAudioCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
    // 设置码率
    mAudioCodecContext->time_base.num = 1;
    mAudioCodecContext->time_base.den = params->mAudioSampleRate;

    // 设置码流的码率
    mAudioStream->time_base.num = 1;
    mAudioStream->time_base.den = params->mAudioSampleRate;

    // 创建音频帧
    mAudioFrame = av_frame_alloc();
    if (!mAudioFrame) {
        LOGE("av_frame_alloc() error: Could not allocate audio frame.");
        return -1;
    }

    // TODO 音频重采样


    // ------------------------------------- 复用器写入文件初始化 ------------------------------------

    // 打开输出文件
    ret = avio_open(&mFormatCtx->pb, videoPath, AVIO_FLAG_WRITE);
    if (ret < 0) {
        LOGE("avio_open error() error %d : Could not open %s", ret, videoPath);
        return -1;
    }

    // 写入文件头
    avformat_write_header(mFormatCtx, NULL);

}

/**
 * 开始录制
 * @param env
 * @param obj
 * @return
 */
JNIEXPORT void
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_startRecorder(JNIEnv *env, jclass obj) {

}

/**
 * 发送YUV数据进行编码
 * @param env
 * @param obj
 * @param yuvData
 * @return
 */
JNIEXPORT jint
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_sendYUVFrame
        (JNIEnv *env, jclass obj, jbyteArray yuvData) {
    jbyte *yuv = env->GetByteArrayElements(yuvData, 0);

    // 发送到编码队列

    env->ReleaseByteArrayElements(yuvData, yuv, 0);
    return 0;
}

/**
 * 发送PCM数据进行编码
 * @param env
 * @param obj
 * @param pcmData
 * @return
 */
JNIEXPORT jint
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_sendPCMFrame
        (JNIEnv *env, jclass obj, jbyteArray pcmData) {
    jbyte *pcm = env->GetByteArrayElements(pcmData, 0);
    // 发送到编码队列

    env->ReleaseByteArrayElements(pcmData, pcm, 0);
    return 0;
}

/**
 * 发送停止命令
 * @param env
 * @param obj
 * @return
 */
JNIEXPORT void
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_stopRecorder(JNIEnv *env, jclass obj) {

}

/**
 * 释放资源
 * @param env
 * @param obj
 * @return
 */
JNIEXPORT void
JNICALL Java_com_cgfay_caincamera_jni_FFmpegHandler_nativeRelease(JNIEnv *env, jclass obj) {

}