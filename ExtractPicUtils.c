#include <sys/stat.h>
#include <dirent.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include "ExtractPic.h"

#define QUEUELIMIT 10
#define ARGELENGTH 1024

extern Queue frame_queue;
extern Queue jpeg_queue;

extern pthread_mutex_t frame_mutex;
extern pthread_mutex_t jpeg_mutex;

extern int processor_num;



void initQueue(Queue *queue)
{
    //初始化头结点
    queue->front = queue->rear = NULL;
	queue->num = 0;
	return ;
}

int isEmpty(Queue *queue)
{
    return( (NULL == queue->front)&&(NULL == queue->rear)) ? 1 : 0;
}


int insertQueue(Queue *queue, void *pData, enum AVPictureType pict_type)
{
	Node *pNodeTemp = NULL;
	//printf("[%s, %d]\n", __FUNCTION__, __LINE__);
    if(NULL == pData || NULL == queue)
		return -1;
	if(queue->num > QUEUELIMIT)
		return -2;
    
	if(0 == queue->num)
	{
		queue->front = malloc(sizeof(Node));
		queue->rear = queue->front;
		memset(queue->front, 0, sizeof(Node));
		queue->front->p = pData;
		queue->front->pict_type = pict_type;
		queue->front->prev = NULL;
		queue->front->next = NULL;
	}
	else
	{
		pNodeTemp = malloc(sizeof(Node));
		memset(pNodeTemp, 0, sizeof(Node));
		pNodeTemp->p = pData;
		pNodeTemp->pict_type = pict_type;
		pNodeTemp->prev = NULL;
		pNodeTemp->next = queue->front;
		queue->front->prev = pNodeTemp;
		queue->front = pNodeTemp;
	}
	queue->num++;
	return 0;
}

int deleteQueue(Queue *queue, void **pData, enum AVPictureType *pict_type)
{
	//printf("[%s, %d]\n", __FUNCTION__, __LINE__);
    if(NULL == pData || NULL == queue || NULL == pict_type)
		return -2;
	
	if(0 == queue->num)
	{
		*pData = NULL;
		return -1;
	}
	if(1 == queue->num)
	{
		memcpy(pData, &queue->rear->p, sizeof(void *));
		//*pData = queue->rear->p;
		*pict_type = queue->rear->pict_type;
		free(queue->rear);
		queue->rear = NULL;
		queue->front = NULL;
	}
	else
	{
		memcpy(pData, &queue->rear->p, sizeof(void *));
		*pict_type = queue->rear->pict_type;
		queue->rear = queue->rear->prev;
		free(queue->rear->next);
		queue->rear->next = NULL;
	}
	queue->num--;
	return 0;
}


void *threadDecode(void *p)
{
	unsigned int i = 0;
	int videoStream = -1, frameFinished, ret, flag = 0;
	unsigned char args[ARGELENGTH], format_args[ARGELENGTH], yadif_args[ARGELENGTH];
	long long dec_time = 0;
	struct timeval tv_begin, tv_end;
	static int skipped_frame = 0;
	AVPacket *pPacket = NULL;
	AVFrame *pFrame = NULL, *pFrameFiltered = NULL;
	FfmpegParamter *pf = NULL;

	printf("[%s, %d] start\n", __FUNCTION__, __LINE__);
	if(NULL == p)
		return NULL;
	pf = (FfmpegParamter *)p;

	for (i = 0; i < pf->pFormatCtx->nb_streams; i++) 
	{
		if (pf->pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) 
		{
			videoStream = i;
			printf("[%s, %d] videoStream = %d\n", __FUNCTION__, __LINE__, videoStream);
			break;
		}
	}
	if(videoStream == -1)
	{
		printf("No video stream in this file.\n");
		return NULL;
	}
	pf->pCodecCtx = pf->pFormatCtx->streams[videoStream]->codec;
	pf->pCodecCtx->thread_count = 1;

	pf->pCodec = avcodec_find_decoder(pf->pCodecCtx->codec_id);
	if (pf->pCodec == NULL) 
	{
		printf("Unsupported codec!\n");
		return NULL;
	}
	if(avcodec_open2(pf->pCodecCtx, pf->pCodec, NULL) < 0)
	{
		printf("Could not open codec!\n");
		return NULL;
	}
	//pCodecCtx->thread_count = 4;
	//pCodecCtx->thread_type = 2;
	//pCodecCtx->active_thread_type = 1;
	
	printf("[%s, %d]\n", __FUNCTION__, __LINE__);
	pf->pFilterGraph = avfilter_graph_alloc();
	pf->pFilterBuffer = avfilter_get_by_name("buffer");
	//pf->pFilterYadif = avfilter_get_by_name("yadif");
	//pf->pFilterBufferSink = avfilter_get_by_name("buffersink");
	//pf->pFilterFormat = avfilter_get_by_name("format");

    //创建buffer过滤器
    memset(args, 0, sizeof(args));
    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
        pf->pCodecCtx->width, pf->pCodecCtx->height, pf->pCodecCtx->pix_fmt,
        pf->pCodecCtx->time_base.num, pf->pCodecCtx->time_base.den,
        pf->pCodecCtx->sample_aspect_ratio.num, pf->pCodecCtx->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(&(pf->pFilterBufferCtx), avfilter_get_by_name("buffer"), "in", args, NULL, pf->pFilterGraph);
	if(ret < 0)
		printf("[%s, %d]avfilter_graph_create_filter ret = %d\n", __FUNCTION__, __LINE__, ret);

    //创建yadif过滤器
    if(0 != pf->di)
    {
		memset(yadif_args, 0, sizeof(args));
		strcat(yadif_args, "mode=");
		if(1 == pf->di)
			strcat(yadif_args, "send_frame");
		else if(2 == pf->di)
			strcat(yadif_args, "send_field");
		else
			strcat(yadif_args, "send_frame");
		strcat(yadif_args, ":parity=");
		if(0 == pf->parity)
			strcat(yadif_args, "auto");
		else if(1 == pf->parity)
			strcat(yadif_args, "tff");	
		else if(2 == pf->parity)
			strcat(yadif_args, "bff");
		else
			strcat(yadif_args, "auto");
		strcat(yadif_args, ":deint=interlaced");
		printf("[%s, %d]yadif_args: %s\n", __FUNCTION__, __LINE__, yadif_args);
		ret = avfilter_graph_create_filter(&(pf->pFilterYadifCtx) , avfilter_get_by_name("yadif"), "yadif", yadif_args, NULL, pf->pFilterGraph);
		if(ret < 0)
			printf("[%s, %d]avfilter_graph_create_filter ret = %d\n", __FUNCTION__, __LINE__, ret);
    }
	//创建format过滤器
	memset(format_args, 0, sizeof(args));
	strcat(format_args, "pix_fmts=");
	if(AV_PIX_FMT_YUV411P == pix_fmt_map(pf->pCodecCtx->pix_fmt))
		strcat(format_args, "yuv411p");
	else if(AV_PIX_FMT_YUV420P == pix_fmt_map(pf->pCodecCtx->pix_fmt))
		strcat(format_args, "yuv420p");
	else if(AV_PIX_FMT_YUV422P == pix_fmt_map(pf->pCodecCtx->pix_fmt))
		strcat(format_args, "yuv422p");
	else if(AV_PIX_FMT_YUV440P == pix_fmt_map(pf->pCodecCtx->pix_fmt))
		strcat(format_args, "yuv440p");
	else if(AV_PIX_FMT_YUV444P == pix_fmt_map(pf->pCodecCtx->pix_fmt))
		strcat(format_args, "yuv444p");
	else
		strcat(format_args, "yuv420p");
	printf("[%s, %d]format_args: %s\n", __FUNCTION__, __LINE__, format_args);
	ret= avfilter_graph_create_filter(&(pf->pFilterFormatCtx), avfilter_get_by_name("format"), "format", format_args, NULL, pf->pFilterGraph);
	if(ret < 0)
		printf("[%s, %d]avfilter_graph_create_filter ret = %d\n", __FUNCTION__, __LINE__, ret);

    //创建buffersink过滤器
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    ret = avfilter_graph_create_filter(&(pf->pFilterBufferSinkCtx), avfilter_get_by_name("buffersink"), "out", NULL, NULL,pf->pFilterGraph);
	if(ret < 0)
		printf("[%s, %d]avfilter_graph_create_filter ret = %d\n", __FUNCTION__, __LINE__, ret);

	av_opt_set_int_list(pf->pFilterBufferSinkCtx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

	//连接过滤器
	if(0 == pf->di)
	{
		ret = avfilter_link(pf->pFilterBufferCtx, 0, pf->pFilterFormatCtx, 0);
		if(ret < 0)
			printf("[%s, %d]avfilter_link ret = %d\n", __FUNCTION__, __LINE__, ret);
	}
	else
	{
		ret = avfilter_link(pf->pFilterBufferCtx, 0, pf->pFilterYadifCtx, 0);
		if(ret < 0)
			printf("[%s, %d]avfilter_link ret = %d\n", __FUNCTION__, __LINE__, ret);
		ret = avfilter_link(pf->pFilterYadifCtx, 0, pf->pFilterFormatCtx, 0);
		if(ret < 0)
			printf("[%s, %d]avfilter_link ret = %d\n", __FUNCTION__, __LINE__, ret);
	}
    ret = avfilter_link(pf->pFilterFormatCtx, 0, pf->pFilterBufferSinkCtx, 0);
	if(ret < 0)
		printf("[%s, %d]avfilter_link ret = %d\n", __FUNCTION__, __LINE__, ret);

    if ((ret = avfilter_graph_config(pf->pFilterGraph, NULL)) < 0){
		printf("[%s, %d] avfilter_graph_config:%d\n",__FUNCTION__, __LINE__, ret);
    }

	pFrame = av_frame_alloc();
	if(NULL == pFrame)
		return NULL;
	//printf("[%s, %d]\n", __FUNCTION__, __LINE__);
	while(1)
	{
		//sleep(1);
		pPacket = av_packet_alloc();
		av_init_packet(pPacket);
		ret = av_read_frame(pf->pFormatCtx, pPacket);
		//printf("[%s, %d] av_read_frame, ret = %d\n", __FUNCTION__, __LINE__, ret);
		if(ret < 0)
		{
			break;
		}
		
		if(pPacket->stream_index == videoStream) 
		{
			if(NULL == pFrame)
				pFrame = av_frame_alloc();
			gettimeofday(&tv_begin, NULL);
			ret = avcodec_decode_video2(pf->pCodecCtx, pFrame, &frameFinished, pPacket);
			gettimeofday(&tv_end, NULL);
			dec_time += (tv_end.tv_sec*1000*1000 + tv_end.tv_usec) - (tv_begin.tv_sec*1000*1000 + tv_begin.tv_usec);

			//printf("[%s, %d]avcodec_decode_video2 ret = %d\n", __FUNCTION__, __LINE__, ret);
			if(ret < 0)
			{
				printf("[%s, %d] avcodec_decode_video2 ERROR\n", __FUNCTION__, __LINE__);
				break;
			}
			if(frameFinished)
			{
				//printf("[%s, %d] pts = %llu\n", __FUNCTION__, __LINE__, pFrame->pts);
				//FILTER
				
				ret = av_buffersrc_add_frame_flags(pf->pFilterBufferCtx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
				if(ret < 0)
					printf("[%s, %d]av_buffersrc_add_frame_flags ret = %d\n", __FUNCTION__, __LINE__, ret);
				while(1)
				{
					if(NULL == pFrameFiltered)
						pFrameFiltered = av_frame_alloc();
					ret = av_buffersink_get_frame(pf->pFilterBufferSinkCtx, pFrameFiltered);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					{
						break;
					}
					else
					{
						do
						{
							pthread_mutex_lock(&frame_mutex);
							ret = insertQueue(&frame_queue, (void *)pFrameFiltered, pFrameFiltered->pict_type);
							pthread_mutex_unlock(&frame_mutex); 
							if(-2 == ret)
								usleep(10*1000);
						}while(-2 == ret);
						pFrameFiltered = NULL;
					}
				}
				av_frame_free(&pFrame);
				pFrame = NULL;
			}
			else
			{
				skipped_frame++;
			}
		}
		av_packet_free(&pPacket);
	}	
	av_packet_free(&pPacket);
	printf("[%s, %d] skipped_frame = %d\n", __FUNCTION__, __LINE__, skipped_frame);
	for(i = 0; (int)i < skipped_frame; i++)
	{
		pPacket = av_packet_alloc();
		av_init_packet(pPacket);
		pPacket->data = NULL;
		pPacket->size = 0;
		if(NULL == pFrame)
			pFrame = av_frame_alloc();
		frameFinished = 0;
		gettimeofday(&tv_begin, NULL);
		ret = avcodec_decode_video2(pf->pCodecCtx, pFrame, &frameFinished, pPacket);
		gettimeofday(&tv_end, NULL);
		dec_time += (tv_end.tv_sec*1000*1000 + tv_end.tv_usec) - (tv_begin.tv_sec*1000*1000 + tv_begin.tv_usec);
		//printf("[%s, %d]avcodec_decode_video2 ret = %d\n", __FUNCTION__, __LINE__, ret);
		if(ret < 0)
		{
			printf("[%s, %d] avcodec_decode_video2 ERROR\n", __FUNCTION__, __LINE__);
			break;
		}
		if(frameFinished)
		{
			//printf("[%s, %d] pts = %llu\n", __FUNCTION__, __LINE__, pFrame->pts);
			//FILTER
			ret = av_buffersrc_add_frame_flags(pf->pFilterBufferCtx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
			if(ret < 0)
				printf("[%s, %d]av_buffersrc_add_frame_flags ret = %d\n", __FUNCTION__, __LINE__, ret);
			while(1)
			{
				if(NULL == pFrameFiltered)
					pFrameFiltered = av_frame_alloc();
				ret = av_buffersink_get_frame(pf->pFilterBufferSinkCtx, pFrameFiltered);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				{
					break;
				}
				else
				{
					do
					{
						pthread_mutex_lock(&frame_mutex);
						ret = insertQueue(&frame_queue, (void *)pFrameFiltered, pFrameFiltered->pict_type);
						pthread_mutex_unlock(&frame_mutex); 
						if(-2 == ret)
							usleep(10*1000);
					}while(-2 == ret);
					pFrameFiltered = NULL;
				}
			}
			av_frame_free(&pFrame);
			pFrame = NULL;
		}
		else
		{	
			break;
		}
		av_packet_free(&pPacket);
	}
	printf("[%s, %d] out skipped_frame = %d\n", __FUNCTION__, __LINE__, i);

	pf->decFinish = 1;
	/*WARING: encoder quit firstly, and then to close decoder codec context*/
	while(1)
	{
		flag = 1;
		for(i = 0; i < processor_num; i++)
		{
			if(0 == pf->encFinish[i])
				flag = 0;
		}
		if(1 == flag)
			break;
		usleep(10*1000);
	}
	
	avcodec_close(pf->pCodecCtx);
	//usleep(1000*1000);
	printf("[%s, %d] end. dec_time = %dms\n", __FUNCTION__, __LINE__, dec_time/1000);
	return NULL;
}

void threadEncode(void *p)
{
	int threadId, codec_id = AV_CODEC_ID_MJPEG, got_JPEG_output, ret;
	enum AVPictureType pict_type;
	long long enc_time = 0;
	struct timeval tv_begin, tv_end;
	AVPacket *pPacketJPEG;
	AVFrame *pFrame = NULL;
	FfmpegParamter *pf = NULL;
	pthread_t encTid = NULL;

	if(NULL == p)
		return ;
	pf = (FfmpegParamter *)p;

	encTid = pthread_self();
	for(threadId = 0; threadId < processor_num; threadId++)
	{
		if(encTid == pf->thread_encoder[threadId])
			break;
	}
	if(processor_num == threadId)
		return ;

	printf("[%s, %d] start. threadId = %d, encTid = %lld\n", __FUNCTION__, __LINE__, threadId, encTid);

	while(1)
	{
		pthread_mutex_lock(&frame_mutex);
		ret = isEmpty(&frame_queue);
		pthread_mutex_unlock(&frame_mutex); 
		if(0 == ret)
			break;
		usleep(50*1000);
	}
	
	pf->pAVCtxJPEG[threadId] = avcodec_alloc_context3(NULL);
	//pf->pAVCtxJPEG[threadId]->thread_count = 4;
	printf("[%s, %d] threadId = %d\n", __FUNCTION__, __LINE__, threadId);
	switch(pix_fmt_map(pf->pCodecCtx->pix_fmt))
	{
		case AV_PIX_FMT_YUV411P:
			pf->pAVCtxJPEG[threadId]->pix_fmt = AV_PIX_FMT_YUVJ411P;
			break;
		case AV_PIX_FMT_YUV420P:
			pf->pAVCtxJPEG[threadId]->pix_fmt = AV_PIX_FMT_YUVJ420P;
			break;
		case AV_PIX_FMT_YUV422P:
			pf->pAVCtxJPEG[threadId]->pix_fmt = AV_PIX_FMT_YUVJ422P;
			break;
		case AV_PIX_FMT_YUV440P:
			pf->pAVCtxJPEG[threadId]->pix_fmt = AV_PIX_FMT_YUVJ440P;
			break;
		case AV_PIX_FMT_YUV444P:
			pf->pAVCtxJPEG[threadId]->pix_fmt = AV_PIX_FMT_YUVJ444P;
			break;
		default:
			printf("[%s, %d] Not support pix_fmt: %d\n", __FUNCTION__, __LINE__, pix_fmt_map(pf->pCodecCtx->pix_fmt));
			pf->pAVCtxJPEG[threadId]->pix_fmt = AV_PIX_FMT_YUVJ420P;
			break;		
	}	
	pf->pAVCtxJPEG[threadId]->codec_type = AVMEDIA_TYPE_VIDEO;  
	pf->pAVCtxJPEG[threadId]->width = pf->pCodecCtx->width;
	pf->pAVCtxJPEG[threadId]->height = pf->pCodecCtx->height;
	pf->pAVCtxJPEG[threadId]->time_base = (AVRational){1,25};
	pf->pAVCtxJPEG[threadId]->bit_rate = 400000;
	pf->pAVCodec[threadId] = avcodec_find_encoder(codec_id);
	printf("[%s, %d] threadId = %d\n", __FUNCTION__, __LINE__, threadId);
	if(avcodec_open2(pf->pAVCtxJPEG[threadId], pf->pAVCodec[threadId], NULL) < 0)
	{
		printf("Could not open codec\n");
		return ;
	}
	printf("[%s, %d] threadId = %d\n", __FUNCTION__, __LINE__, threadId);
	while(1)
	{		
		//printf("[%s, %d] \n", __FUNCTION__, __LINE__);
		do
		{
			pthread_mutex_lock(&frame_mutex);
			ret = deleteQueue(&frame_queue, (void **)&pFrame, &pict_type);
			pthread_mutex_unlock(&frame_mutex); 
			if(ret < 0 && (1 == pf->decFinish))
			{
				break;
			}
			else if(ret < 0)
			{
				usleep(10*1000);
			}
		}while(ret < 0);
		if(ret < 0 && (1 == pf->decFinish))
		{
			break;
		}
		if(NULL == pFrame)
			continue;
		//printf("[%s, %d] get a frame. pts = %llu, pict_type = %d, width = %d, height = %d.\n", 
		//	__FUNCTION__, __LINE__, pFrame->pkt_pts, pFrame->pict_type, pFrame->width, pFrame->height);
		pPacketJPEG = av_packet_alloc();
		av_init_packet(pPacketJPEG);
		pPacketJPEG->data = NULL; // packet data will be allocated by the encoder
		pPacketJPEG->size = 0;
		got_JPEG_output = 0;
		gettimeofday(&tv_begin, NULL);
		//printf("[%s, %d] \n", __FUNCTION__, __LINE__);
		ret = avcodec_encode_video2(pf->pAVCtxJPEG[threadId] , pPacketJPEG, pFrame, &got_JPEG_output);
		//printf("[%s, %d] avcodec_encode_video2 ret = %d\n", __FUNCTION__, __LINE__, ret);
		gettimeofday(&tv_end, NULL);
		enc_time += (tv_end.tv_sec*1000*1000 + tv_end.tv_usec) - (tv_begin.tv_sec*1000*1000 + tv_begin.tv_usec);
		pPacketJPEG->pts = pFrame->pts;
		if((ret >= 0)&&(got_JPEG_output == 1))
		{
			//printf("[%s, %d] got_JPEG_output\n", __FUNCTION__, __LINE__);
			do
			{
				pthread_mutex_lock(&jpeg_mutex);
				ret = insertQueue(&jpeg_queue, (void *)pPacketJPEG, pict_type);
				pthread_mutex_unlock(&jpeg_mutex); 
				if(-2 == ret)
					usleep(10*1000);
			}while(-2 == ret);
			pPacketJPEG = NULL;
		}
		else
		{
			av_packet_free(&pPacketJPEG);
		}
		av_frame_free(&pFrame);
		pFrame = NULL;

	}
	pf->encFinish[threadId] = 1;
	printf("[%s, %d] end. threadId = %d, enc_time = %dms\n", __FUNCTION__, __LINE__, threadId, enc_time/1000);
	return ;
}

void *threadOutput(void *p)
{
	int ret, flag, i;
	long long sav_time = 0;
	struct timeval tv_begin, tv_end;
	AVPacket *pPacketJPEG;
	enum AVPictureType jpeg_pict_type = 0;
	FfmpegParamter *pf = NULL;

	printf("[%s, %d] start\n", __FUNCTION__, __LINE__);
	if(NULL == p)
		return NULL;
	pf = (FfmpegParamter *)p;

	while(1)
	{
		do
		{
			pthread_mutex_lock(&jpeg_mutex);
			ret = deleteQueue(&jpeg_queue, (void **)&pPacketJPEG, &jpeg_pict_type);
			pthread_mutex_unlock(&jpeg_mutex); 

			/* Have all enc threads quited ?*/
			flag = 1;
			for(i = 0; i < processor_num; i++)
			{
				if(0 == pf->encFinish[i])
					flag = 0;
			}
			
			if(ret < 0 && 1 == flag)
			{
				break;
			}
			else if(ret < 0)
			{
				usleep(10*1000);
			}
		}while(ret < 0);
		if(ret < 0 && 1 == flag)
		{
			break;
		}
		if(NULL == pPacketJPEG)
			continue;
		gettimeofday(&tv_begin, NULL);
		OutputJPEG(pPacketJPEG, jpeg_pict_type, pf->pic_path, pf->pic_i_frame_path);
		gettimeofday(&tv_end, NULL);
		sav_time += (tv_end.tv_sec*1000*1000 + tv_end.tv_usec) - (tv_begin.tv_sec*1000*1000 + tv_begin.tv_usec);
		av_packet_free(&pPacketJPEG);
	}
	printf("[%s, %d] end. sav_time = %dms\n", __FUNCTION__, __LINE__, sav_time/1000);
	return NULL;
}


enum AVPixelFormat pix_fmt_map(enum AVPixelFormat input_fmt)
{
	enum AVPixelFormat output_fmt = AV_PIX_FMT_YUV420P;
	switch(input_fmt)
	{
		case AV_PIX_FMT_YUV411P:
		case AV_PIX_FMT_UYYVYY411:
			output_fmt = AV_PIX_FMT_YUV411P;
			break;
		case AV_PIX_FMT_YUV420P:
		case AV_PIX_FMT_YUVA420P:
		case AV_PIX_FMT_YUV420P16LE:
		case AV_PIX_FMT_YUV420P16BE:
		case AV_PIX_FMT_YUV420P9BE:
		case AV_PIX_FMT_YUV420P9LE:
		case AV_PIX_FMT_YUV420P10BE:
		case AV_PIX_FMT_YUV420P10LE:
		case AV_PIX_FMT_YUVA420P9BE:
		case AV_PIX_FMT_YUVA420P9LE:
		case AV_PIX_FMT_YUVA420P10BE:
		case AV_PIX_FMT_YUVA420P10LE:
		case AV_PIX_FMT_YUVA420P16BE:
		case AV_PIX_FMT_YUVA420P16LE:
		case AV_PIX_FMT_YUV420P12BE:
		case AV_PIX_FMT_YUV420P12LE:
		case AV_PIX_FMT_YUV420P14BE:
		case AV_PIX_FMT_YUV420P14LE:
#if 0
		case AV_PIX_FMT_YUV420P9:
		case AV_PIX_FMT_YUV420P10:
		case AV_PIX_FMT_YUV420P12:
		case AV_PIX_FMT_YUV420P14:
		case AV_PIX_FMT_YUV420P16:
		case AV_PIX_FMT_YUVA420P9:
		case AV_PIX_FMT_YUVA420P10:
		case AV_PIX_FMT_YUVA420P16:
#endif			
			output_fmt = AV_PIX_FMT_YUV420P;
			break;
		case AV_PIX_FMT_YUYV422:
		case AV_PIX_FMT_YUV422P:
		case AV_PIX_FMT_UYVY422:
		case AV_PIX_FMT_YUV422P16LE:
		case AV_PIX_FMT_YUV422P16BE:
		case AV_PIX_FMT_YUV422P10BE:
		case AV_PIX_FMT_YUV422P10LE:
		case AV_PIX_FMT_YUV422P9BE:
		case AV_PIX_FMT_YUV422P9LE:
		case AV_PIX_FMT_YUVA422P:
		case AV_PIX_FMT_YUVA422P9BE:
		case AV_PIX_FMT_YUVA422P9LE:	
		case AV_PIX_FMT_YUVA422P10BE:
		case AV_PIX_FMT_YUVA422P10LE:
		case AV_PIX_FMT_YUVA422P16BE:
		case AV_PIX_FMT_YUVA422P16LE:
		case AV_PIX_FMT_YVYU422:
		case AV_PIX_FMT_YUV422P12BE:
		case AV_PIX_FMT_YUV422P12LE:
		case AV_PIX_FMT_YUV422P14BE:
		case AV_PIX_FMT_YUV422P14LE:
#if 0
		case AV_PIX_FMT_YUV422P9:
		case AV_PIX_FMT_YUV422P10:
		case AV_PIX_FMT_YUV422P12:
		case AV_PIX_FMT_YUV422P14:
		case AV_PIX_FMT_YUV422P16:
		case AV_PIX_FMT_YUVA422P9:
		case AV_PIX_FMT_YUVA422P10:
		case AV_PIX_FMT_YUVA422P16:
#endif			
			output_fmt = AV_PIX_FMT_YUV422P;
			break;
		case AV_PIX_FMT_YUV440P:
		case AV_PIX_FMT_YUV440P10LE:
		case AV_PIX_FMT_YUV440P10BE:
		case AV_PIX_FMT_YUV440P12LE:
		case AV_PIX_FMT_YUV440P12BE:
#if 0
		case AV_PIX_FMT_YUV440P10:
		case AV_PIX_FMT_YUV440P12:
#endif			
			output_fmt = AV_PIX_FMT_YUV440P;
			break;			
		case AV_PIX_FMT_YUV444P:
		case AV_PIX_FMT_YUV444P16LE:
		case AV_PIX_FMT_YUV444P16BE:
		case AV_PIX_FMT_RGB444LE:
		case AV_PIX_FMT_RGB444BE:
		case AV_PIX_FMT_BGR444LE:
		case AV_PIX_FMT_BGR444BE:
		case AV_PIX_FMT_YUV444P9BE:
		case AV_PIX_FMT_YUV444P9LE:
		case AV_PIX_FMT_YUV444P10BE:
		case AV_PIX_FMT_YUV444P10LE:
		case AV_PIX_FMT_YUVA444P:
		case AV_PIX_FMT_YUVA444P9BE:
		case AV_PIX_FMT_YUVA444P9LE:
		case AV_PIX_FMT_YUVA444P10BE:
		case AV_PIX_FMT_YUVA444P10LE:
		case AV_PIX_FMT_YUVA444P16BE:
		case AV_PIX_FMT_YUVA444P16LE:
		case AV_PIX_FMT_YUV444P12BE:
		case AV_PIX_FMT_YUV444P12LE:
		case AV_PIX_FMT_YUV444P14BE:
		case AV_PIX_FMT_YUV444P14LE:
#if 0			
		case AV_PIX_FMT_RGB444:
		case AV_PIX_FMT_BGR444:
		case AV_PIX_FMT_YUV444P9:
		case AV_PIX_FMT_YUV444P10:
		case AV_PIX_FMT_YUV444P12:
		case AV_PIX_FMT_YUV444P14:
		case AV_PIX_FMT_YUV444P16:
		case AV_PIX_FMT_YUVA444P9:
		case AV_PIX_FMT_YUVA444P10:
		case AV_PIX_FMT_YUVA444P16:			
#endif			
			output_fmt = AV_PIX_FMT_YUV444P;
			break;
		default:
			printf("[%s, %d] Not support pix_fmt: %d\n", __FUNCTION__, __LINE__, input_fmt);
			output_fmt  = AV_PIX_FMT_YUV420P;
			break;		
	}
	return output_fmt;
}


int OutputJPEG(AVPacket *pPacketJPEG, enum AVPictureType pict_type, unsigned char *pic_path, unsigned char *pic_i_frame_path)
{	
	static int num = 0;
	FILE *fp = NULL;
	char sPathName[1024] = {0};
	char sJPEGName[1024] = {0};
	
	memset(sPathName, 0, 1024);
	memset(sJPEGName, 0, 1024);
	strcat(sPathName, pic_path);
	strcat(sPathName, "/");
	sprintf(sJPEGName, "F%d_%llu", num, pPacketJPEG->pts);
	if(AV_PICTURE_TYPE_NONE == pict_type)
	{
		strcat(sJPEGName, "_Unknow");
	}
	else if(AV_PICTURE_TYPE_I == pict_type)
	{
		strcat(sJPEGName, "_I");
	}
	else if(AV_PICTURE_TYPE_P == pict_type)
	{
		strcat(sJPEGName, "_P");
	}	
	else if(AV_PICTURE_TYPE_B == pict_type)
	{
		strcat(sJPEGName, "_B");
	}
	else if(AV_PICTURE_TYPE_S == pict_type)
	{
		strcat(sJPEGName, "_S");
	}
	else if(AV_PICTURE_TYPE_SI == pict_type)
	{
		strcat(sJPEGName, "_SI");
	}
	else if(AV_PICTURE_TYPE_SP == pict_type)
	{
		strcat(sJPEGName, "_SP");
	}
	else if(AV_PICTURE_TYPE_BI == pict_type)
	{
		strcat(sJPEGName, "_BI");
	}
	strcat(sJPEGName, ".jpeg");
	strcat(sPathName, sJPEGName);
	printf("%s: %s\n", __FUNCTION__, sPathName);
	fp = fopen(sPathName, "wb");
	fwrite(pPacketJPEG->data, 1, pPacketJPEG->size, fp);
	fclose(fp);

	if(AV_PICTURE_TYPE_I == pict_type || AV_PICTURE_TYPE_SI == pict_type)
	{
		memset(sPathName, 0, 1024);
		memset(sJPEGName, 0, 1024);
		strcat(sPathName, pic_i_frame_path);
		strcat(sPathName, "/");
		sprintf(sJPEGName, "F%d_%llu", num, pPacketJPEG->pts);
		if(AV_PICTURE_TYPE_I == pict_type)
		{
			strcat(sJPEGName, "_I");
		}	
		else if(AV_PICTURE_TYPE_SI == pict_type)
		{
			strcat(sJPEGName, "_SI");
		}
		strcat(sJPEGName, ".jpeg");
		strcat(sPathName, sJPEGName);
		fp = fopen(sPathName, "wb");
		fwrite(pPacketJPEG->data, 1, pPacketJPEG->size, fp);
		fclose(fp);
	}
	num++;

	return 0;
}

