
//#include <libavutil/frame.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>

#define VERSION 20170908
#define MAXTHREADNUM 16


#ifndef _EXTRACT_H_
#define _EXTRACT_H_


typedef struct Node
{
	struct Node *prev;
	struct Node *next;
	void *p;
	enum AVPictureType pict_type;
}Node;

typedef struct Queue
{
	struct Node *front;
	struct Node *rear;
	int num;
}Queue;

typedef struct FfmpegParamter
{
	//Option
	int di;
	int parity;
	
	//Dec
	AVFormatContext *pFormatCtx;
	AVCodecContext *pCodecCtx;	
	AVCodec *pCodec;

	//filter
	AVFilterGraph *pFilterGraph;
	AVFilter *pFilterBuffer;
	AVFilter *pFilterYadif;
	AVFilter *pFilterBufferSink;
	AVFilter *pFilterFormat;
	
	AVFilterContext *pFilterBufferCtx;
	AVFilterContext *pFilterYadifCtx;
	AVFilterContext *pFilterBufferSinkCtx;
	AVFilterContext *pFilterFormatCtx;
	
	//Enc
	pthread_t thread_encoder[MAXTHREADNUM];
	AVCodecContext *pAVCtxJPEG[MAXTHREADNUM];
	AVCodec *pAVCodec[MAXTHREADNUM];

	int decFinish, encFinish[MAXTHREADNUM];
	unsigned char pic_path[1024];
	unsigned char pic_i_frame_path[1024];
	
	
}FfmpegParamter;

#if 0
typedef struct FrameNode
{
	struct FrameNode *prev;
	struct FrameNode *next;
	AVFrame *pframe;
}FrameNode;

typedef struct FrameQueue
{
	struct FrameNode *front;
	struct FrameNode *rear;
	
}FrameQueue;
#endif

void initQueue(Queue *queue);
int isEmpty(Queue *queue);
int insertQueue(Queue *queue, void *pData, enum AVPictureType pict_type);
int deleteQueue(Queue *queue, void **pData, enum AVPictureType *pict_type);
void *threadDecode(void *p);
void threadEncode(void *p);
void *threadOutput(void *p);
enum AVPixelFormat pix_fmt_map(enum AVPixelFormat input_fmt);
int OutputJPEG(AVPacket *pPacketJPEG, enum AVPictureType pict_type, unsigned char *pic_path, unsigned char *pic_i_frame_path);



#endif
