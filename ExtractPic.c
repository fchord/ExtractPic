#include <sys/stat.h>
#include <dirent.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
#include "ExtractPic.h"

static char PIC_PATH[1024];
static char PIC_I_FRAME_PATH[1024];

Queue frame_queue;
Queue jpeg_queue;

pthread_mutex_t frame_mutex;
pthread_mutex_t jpeg_mutex;

pthread_t thread_decoder;
pthread_t thread_outputjpeg;

int processor_num = 0;

/*
	±‡“Î√¸¡Ó
	
	export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib/
	
	gcc ExtractPic.c ExtractPicUtils.c -I/usr/local/include/  -L/usr/local/lib/ -lavcodec  -lavformat -lavdevice -lavfilter -lswscale -lavutil -g -o ExtractPic

	set args 6666.ts
*/
/*
extern void *threadDecode(void *p);
extern void *threadEncode(void *p);
extern void *threadOutput(void *p);
*/
void get_help(void);


int main(int argc, char **argv)
{
	int ret = 0, i = 1;
	char input_file[1024], base_path[1024], index[4], idx[2], processor[1024], *p;
	DIR *dir = NULL;
	FILE *fpCpuInfo = NULL;

	struct timeval tv_begin, tv_end, tv_total_begin, tv_total_end;
	long long dmx_time = 0, dec_time = 0, enc_time = 0, sav_time = 0, total_time = 0;

	FfmpegParamter *pFfmpegParamter = NULL;
	AVFormatContext *pFormatCtx = NULL;
	
	pFfmpegParamter = malloc(sizeof(FfmpegParamter));
	memset(pFfmpegParamter, 0, sizeof(FfmpegParamter));
	
	printf(" === ExtractPic ");
	printf("v%d", VERSION);
	printf(" === \n");
	int ch;
	opterr = 0;
	
	if(argc <= 1)
	{
		printf("Please drag the media file onto this program.\n");
		get_help();
		sleep(10);
		return -1;
	}
	else if(argc == 2 && (strlen(argv[1]) > 3))
	{
		printf("[%s, %d]\n", __FUNCTION__, __LINE__);
	
		printf("input file: %s\n", argv[1]);
		memset(input_file, 0, sizeof(input_file));
		strcat(input_file, argv[1]);
		pFfmpegParamter->di = 0;
		pFfmpegParamter->parity = 0;
	}
	else
	{		
		printf("[%s, %d]\n", __FUNCTION__, __LINE__);
		while((ch = getopt(argc, argv, "i:d:p:")) != -1)
		switch(ch)
		{
			case 'i':
				printf("input file: %s\n", optarg);
				memset(input_file, 0, sizeof(input_file));
				strcat(input_file, optarg);
				break;
			case 'd':
				printf("deinterlace: %s\n", optarg);
				if(0 <= atoi(optarg) && atoi(optarg) <= 2)
					pFfmpegParamter->di = atoi(optarg);
				else
					pFfmpegParamter->di = 0;
				break;
			case 'p':
				printf("parity of field picture: %s\n", optarg);
				if(0 <= atoi(optarg) && atoi(optarg) <= 2)
					pFfmpegParamter->parity = atoi(optarg);
				else
					pFfmpegParamter->parity = 0;
				break;
			case 'h':
				get_help();
				//sleep(10);
				break;
		   default:
			  get_help();
		}
	//printf("optopt +%c\n", optopt);
	}

	if(input_file[0] == '\0')
	{
		printf("Please input a media file.\n\n");
		//get_help();
		return 0;
	}

	fpCpuInfo = popen("cat /proc/cpuinfo | grep processor", "r");
	if(NULL != fpCpuInfo)
	{
		memset(processor, 0, sizeof(processor));
		fread(processor, 1, 1023, fpCpuInfo);
		p = processor;
		while(1)
		{
			if((p = strstr(p, "processor")) != NULL)
			{
				processor_num++;
				p++;
			}
			else
			{
				break;
			}
			
		}
	}
	else
	{
		/*∂¡cpuinfo ß∞‹¡À*/
		processor_num = 2;
	}
	printf("processor_num = %d\n", processor_num);
	if(processor_num > MAXTHREADNUM)
		processor_num = MAXTHREADNUM;
	else if(processor_num < 1)
		processor_num  = 1;
	

	av_log_set_flags(AV_LOG_TRACE);
	av_register_all();
	avcodec_register_all();
	avdevice_register_all();
	avfilter_register_all();
	avformat_network_init();

	ret = avformat_open_input(&pFfmpegParamter->pFormatCtx, input_file, NULL, NULL);
	if(ret != 0)
	{
		printf("open %s fail! ret = %d.\n", input_file, ret);
		return -1; // Couldn't open file		 
	}

	if (avformat_find_stream_info(pFfmpegParamter->pFormatCtx, NULL) < 0)
	{
		return -1;
	}	
	av_dump_format(pFfmpegParamter->pFormatCtx, 0, input_file, 0);

	memset(PIC_PATH, 0, 1024);
	strcpy(PIC_PATH, input_file);
	strcat(PIC_PATH, "-pic");
	dir = opendir(PIC_PATH);
	if(NULL != dir)
	{
		closedir(dir);
		memset(base_path, 0, 1024);
		strcpy(base_path, PIC_PATH);
		i = 1;
		memset(idx, 0, 2);
		strcpy(idx, "1");
		do
		{
			memset(PIC_PATH, 0, 1024);
			strcpy(PIC_PATH, base_path);
			strcat(PIC_PATH, "(");
			strcat(PIC_PATH, idx);
			strcat(PIC_PATH, ")");
			dir = opendir(PIC_PATH);
			if(NULL != dir)
			{
				closedir(dir);
				i++;
				idx[0]++;
			}
			else
			{
				break;
			}
		}
		while(1);
	}
	mkdir(PIC_PATH, S_IRWXU|S_IRWXG|S_IRWXO);
	
	memset(PIC_I_FRAME_PATH, 0, 1024);
	strcpy(PIC_I_FRAME_PATH, input_file);
	strcat(PIC_I_FRAME_PATH, "-pic_I_frame");
	dir = opendir(PIC_I_FRAME_PATH);
	if(NULL != dir)
	{
		closedir(dir);
		memset(base_path, 0, 1024);
		strcpy(base_path, PIC_I_FRAME_PATH);
		i = 1;
		memset(idx, 0, 2);
		strcpy(idx, "1");
		do
		{
			memset(PIC_I_FRAME_PATH, 0, 1024);
			strcpy(PIC_I_FRAME_PATH, base_path);
			strcat(PIC_I_FRAME_PATH, "(");
			strcat(PIC_I_FRAME_PATH, idx);
			strcat(PIC_I_FRAME_PATH, ")");
			dir = opendir(PIC_I_FRAME_PATH);
			if(NULL != dir)
			{
				closedir(dir);
				i++;
				idx[0]++;
			}
			else
			{
				break;
			}
		}
		while(1);
	}
	mkdir(PIC_I_FRAME_PATH, S_IRWXU|S_IRWXG|S_IRWXO);
	strcpy(pFfmpegParamter->pic_path, PIC_PATH);
	strcpy(pFfmpegParamter->pic_i_frame_path, PIC_I_FRAME_PATH);
	printf("pic_path = %s\n", pFfmpegParamter->pic_path);
	printf("pic_i_frame_path = %s\n", pFfmpegParamter->pic_i_frame_path);

	pthread_mutex_init(&frame_mutex, NULL);
	pthread_mutex_init(&jpeg_mutex, NULL);

	initQueue(&frame_queue);
	initQueue(&jpeg_queue);

	ret = pthread_create(&thread_decoder, NULL, (void *)threadDecode, (void *)pFfmpegParamter);
	if(ret > 0)
	{
		printf("start thread_decoder\n");
	}
	for(i = 0; i < processor_num; i++)
	{
		ret = pthread_create(&pFfmpegParamter->thread_encoder[i], NULL, (void *)threadEncode, (void *)pFfmpegParamter);
		//if(ret > 0)
		{
			printf("start thread_encoder[%d]\n", i);
		}
	}
	ret = pthread_create(&thread_outputjpeg, NULL, (void *)threadOutput, (void *)pFfmpegParamter);
	if(ret > 0)
	{
		printf("start thread_outputjpeg\n");
	}
	pthread_join(thread_decoder, NULL);
	for(i = 0; i < processor_num; i++)
		pthread_join(pFfmpegParamter->thread_encoder[i], NULL);
	pthread_join(thread_outputjpeg, NULL);
	
	avformat_close_input(&pFfmpegParamter->pFormatCtx);
	printf(" Quit!\n");
	return 0;

}

void get_help(void)
{
	printf("ExtractPic Help ");
	printf("v%d", VERSION);
	printf("\n");
	printf("Usage:\n");
	printf("-h   get help\n");
	printf("-i   <input_file_name>\n");	
	printf("-d   <deinterlace>\n");
	printf("	 Deinterlacing the input video.\n");
	printf("	 0: No deinterlace\n");
	printf("	 1: Deinterlace. Output one frame for each frame\n");
	printf("	 2: Deinterlace. Output one frame for each field\n");
	printf("-p	 <parity>\n");
	printf("	 The picture field parity.\n");
	printf("	 0: auto.\n");
	printf("	 1: Top field is first\n");
	printf("	 2: Bottom field is first\n");
	printf("");	
	return ;
}
