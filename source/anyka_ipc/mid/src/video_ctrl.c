#include "common.h"

#define THRESHOLD_MIN   		10
#define THRESHOLD_MAX   		40

#define WIDTH_SVGA		800
#define HEIGHT_SVGA		600

#define THRESHOLD	35


#define MAX_FRAME_SEND_BUF  50
#define CAMERA_MAX_FRAME_OUT    20
//#define VIDEO_LOW_POWER_SWITCH

struct _ANYKA_VIDEO_THREAD_INFO;
struct _ANYKA_VIDEO_ENCODE_HANDLE;


typedef struct _ANYKA_VIDEO_THREAD_INFO
{
    uint8     run_flag;
    uint8    video_type;
    T_VOID *mydata;
    PA_HTHREAD id;
    T_VOID  *queue_handle;
    sem_t   video_data_mutex;
    struct _ANYKA_VIDEO_THREAD_INFO *next;
    P_VIDEO_DOWITH_CALLBACK *pcallback;
}anyka_video_thread_info, *Panyka_video_thread_info;

typedef struct _ANYKA_VIDEO_ENCODE_HANDLE
{
    uint8    encode_use_flag;    //���������Ƿ��ڹ���
    uint8    video_size_flag;    //720p��VGA��ѡ��TRUEΪ720P
    uint8    frames;
    uint8    index;
    T_VOID  *encode_handle;
    Panyka_video_thread_info pthread_info;
	uint32   last_ts;
	uint8    fixnum;
	uint32   stream_size;
	uint32   reference_ts ;
}anyka_video_encode_handle, *Panyka_video_encode_handle;

typedef struct _ANYKA_VIDEO_CHECK_MOVE_INFO
{
    uint8   run_flag;
    uint16   time_interval;
    uint8   buf_index;
    uint32  time_start;
    T_VOID  *queue_handle;
    sem_t   video_data_mutex;
    T_VOID  *mydata;
    uint8   *video_data[2];
    PA_HTHREAD id;
    video_move_check_notice_callback *pcallback;
    PANYKA_FILTER_VIDEO_CHECK_FUNC *filter_check;
    T_VOID  *encode_handle;
}anyka_video_check_move_info, *Panyka_video_check_move_info;

typedef struct _ANYKA_VIDEO_CTRL_INFO
{
    uint8  run_flag;
    PA_HTHREAD  video_thread;
    int check_frame; 
    pthread_mutex_t     video_ctrl_mutex;
    pthread_mutex_t     move_ctrl_mutex;
	pthread_mutex_t     video_encode_mutex;
    sem_t   video_sem;
    sem_t   camera_sem;
    void *encode_queue;
    Panyka_video_check_move_info pmove_check_handle;  //�ƶ����ľ��
    anyka_video_encode_handle encode_handle[FRAMES_ENCODE_HANDLE_NUM];
}anyka_video_ctrl_info, *Panyka_video_ctrl_info;

T_STM_STRUCT * video_malloc_stream_ram(int size);
void video_free_stream_ram(void *pStream);

Panyka_video_ctrl_info pvideo_ctrl = NULL;


/**
 * NAME         video_get_active_frame
 * @BRIEF	��ȡʵ��֡��
 * @PARAM	int max_frame, ���֡��
 			int cur_frame, ��ǰ֡��
 			int index,
 			
 * @RETURN	int
 * @RETVAL	������
 */

int video_get_active_frame(int max_frame, int cur_frame, int index)
{
    
    if(max_frame <= cur_frame)
    {
        return 1;
    }

    if(index == 0)
    {
        return 0;
    }

    if(((index + 1) * cur_frame / max_frame) > (index * cur_frame / max_frame))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

//#define SAVE_H264_STREAM
#ifdef SAVE_H264_STREAM
static FILE *myfp=NULL;
static T_U32 fsz=0;
void save_h264(T_U8 *data, T_U32 len)
{
    if (!myfp && !fsz)
    {
    	myfp = fopen("/tmp/save.h264", "w+");	
    	printf("------------ start save h264 stream --------------\n");
	}

    if (myfp)
	    fsz += fwrite(data, 1, len, myfp);

    if (fsz > 8*1024*1024 && myfp)
    {
	    fclose(myfp);
	    myfp = NULL;
    	printf("------------ stop save h264 stream --------------\n");
    }
}
#endif

/**
 * NAME         video_encode_all_queue
 * @BRIEF	�������̣߳�������е�CAMERA���ݵı��룬������������
                    �͸�����ע����Ĺ���ģ��
 * @PARAM	pframe_buf
                    size
                   
 
 * @RETURN	void 
 * @RETVAL	
 */

int flag[FRAMES_ENCODE_HANDLE_NUM]={0};
void video_encode_all_queue(void *pframe_buf, long size, unsigned long ts,time_t calendar_time)
{
    int i, IPFrame_type;
    void *pencode_outbuf;
    
    if(NULL == pvideo_ctrl)
    {
        return ;
    }
    		
    pthread_mutex_lock(&pvideo_ctrl->video_ctrl_mutex);

    for(i = 0; i < FRAMES_ENCODE_HANDLE_NUM; i++)
    {
        if(pvideo_ctrl->encode_handle[i].encode_use_flag)
        {
            uint32 cam_frame_rate = camera_get_max_frame();
            uint32 actual_fps = pvideo_ctrl->encode_handle[i].frames;
            
            if(i == FRAMES_ENCODE_PICTURE)
            {
                //ͼƬֻҪһ�ξ�OK��.
                pvideo_ctrl->encode_handle[i].encode_use_flag = 0;
            }

            if(flag[i] || video_get_active_frame(cam_frame_rate, actual_fps, pvideo_ctrl->encode_handle[i].index))
            {
                T_STM_STRUCT *pstream;
                void *encode_buffer;
                int encode_size;
				int error_per_second;
				int actual_frames_interval = 1000 / actual_fps;
				
				if (!ts) {
					flag[i] = 1;
					//anyka_print("%s cur frame video data err\n", __func__);
					goto discard_cur_frame;
				} else {
					flag[i] = 0;
				}

				//��ǰ��ʾ֡����ʵ��֡�ʵ��������֣����ܱ���12.5fps��������������������⴦��.
				switch (cam_frame_rate) {
				case 13:
				case 12:
					error_per_second = (10000 / 125 * 12 - 1000) / (int)actual_fps;
					break;
				default:
					error_per_second = 0;
					break;
				}
				actual_frames_interval += error_per_second;

                /* main chanal */
                if(pvideo_ctrl->encode_handle[i].video_size_flag) {
                    encode_buffer = pframe_buf;
                } else {
                	/*sub chanal */
                    encode_buffer = pframe_buf + size;
                }
                //����
                encode_size = video_encode_frame(pvideo_ctrl->encode_handle[i].encode_handle, 
                								encode_buffer, &pencode_outbuf, &IPFrame_type);
                if(encode_size)
                {
                    pvideo_ctrl->encode_handle[i].stream_size += encode_size;
                    if (IPFrame_type) {
                        anyka_debug("### vsize[%d] = %d kbps ###\n", 
							i, pvideo_ctrl->encode_handle[i].stream_size*8/1024);
                        pvideo_ctrl->encode_handle[i].stream_size = 0;
                    }

                    #ifdef SAVE_H264_STREAM
                    if(i == FRAMES_ENCODE_RECORD)
                        save_h264(pencode_outbuf, encode_size);
                    #endif

					uint32 ts_smoonthed;
							
					/** init each handle first frame time stamps **/
					if(pvideo_ctrl->encode_handle[i].last_ts == 0) {
						pvideo_ctrl->encode_handle[i].last_ts = ts;
						pvideo_ctrl->encode_handle[i].fixnum = 0;
						pvideo_ctrl->encode_handle[i].reference_ts = ts;  
						ts_smoonthed = ts;
					} else if (cam_frame_rate > actual_fps) {
						ts_smoonthed = pvideo_ctrl->encode_handle[i].reference_ts + actual_frames_interval * pvideo_ctrl->encode_handle[i].fixnum;

						if ((ts > pvideo_ctrl->encode_handle[i].last_ts + actual_frames_interval * 2) ||
								(ts_smoonthed > pvideo_ctrl->encode_handle[i].last_ts + actual_frames_interval * 2) ||
								(ts_smoonthed < pvideo_ctrl->encode_handle[i].last_ts + actual_frames_interval / 4)) {
							ts_smoonthed = ts;
						}

						if ((pvideo_ctrl->encode_handle[i].index + 1) >= cam_frame_rate) {
							pvideo_ctrl->encode_handle[i].last_ts = 0;
						} else {
							pvideo_ctrl->encode_handle[i].last_ts = ts;  
						}
					} else {
						ts_smoonthed = ts;
						pvideo_ctrl->encode_handle[i].last_ts = ts;  
					}
					pvideo_ctrl->encode_handle[i].fixnum++;

                    Panyka_video_thread_info head = pvideo_ctrl->encode_handle[i].pthread_info;
                    //�����������ݷ���������������ʵ��
                    while(head)
                    {
                        int try_time = 0;
						while(((pstream = video_malloc_stream_ram(encode_size)) == NULL) && (try_time < 10))
						{
                            pthread_mutex_unlock(&pvideo_ctrl->video_ctrl_mutex);
                            usleep(10*1000);
                            pthread_mutex_lock(&pvideo_ctrl->video_ctrl_mutex);
                            try_time ++;
						}
                        if(pstream) {
                            memcpy(pstream->buf, pencode_outbuf, encode_size);
                            pstream->size = encode_size;
                            pstream->iFrame = IPFrame_type;
                            pstream->calendar_time = calendar_time;

							pstream->timestamp = ts_smoonthed;

                            if(anyka_queue_push(head->queue_handle, (void *)pstream)) {
                                sem_post(&head->video_data_mutex); 
							} else {                                
                                 video_free_stream_ram(pstream);  
                                 video_encode_set_Iframe(pvideo_ctrl->encode_handle[i].encode_handle);
                            }
                        } 
						else
						{
							video_encode_set_Iframe(pvideo_ctrl->encode_handle[i].encode_handle);						
                            anyka_print("[%s:%d] it fails to malloc!\n",__func__, __LINE__);
						}
                        head = head->next;
                    }
                }
            }
discard_cur_frame:
            pvideo_ctrl->encode_handle[i].index ++;
            if(pvideo_ctrl->encode_handle[i].index >= camera_get_max_frame())
            {
                pvideo_ctrl->encode_handle[i].index = 0;
            }
        }
    }
    pthread_mutex_unlock(&pvideo_ctrl->video_ctrl_mutex);
    
}

/**
 * NAME         video_need_encode
 * @BRIEF	�ж��Ƿ���Ҫ����
 * @PARAM	
                    
                   
 
 * @RETURN	1-->ok; 0-->fail 
 * @RETVAL	
 */


uint8 video_need_encode()
{
    int i;

    if(pvideo_ctrl->pmove_check_handle)
    {
        //�ƶ����
        return 1;
    }
    
    for(i = 0; i < FRAMES_ENCODE_HANDLE_NUM; i++)
    {
        if(pvideo_ctrl->encode_handle[i].encode_use_flag)
        {
            return 1;
        }
    }
    return 0;
    
}

typedef struct 
{
	void * pframe;
	time_t  osd_time;
}st_osd_frame;

static void del_uv(unsigned char *pbuf, int width, int height)
{
	int size;
	unsigned char *p_start;

	size = width * height / 2;
	p_start = pbuf + width * height;
	memset(p_start, 0x80, size);
}

/**
 * NAME     video_encode_read_data
 * @BRIEF	�����߳�,��һֱ������,ϵͳ����ʱ,�����ڵȴ�,
                    ���б�����Ҫʱ�ٽ��б��빤��,���û�б�������
                    ���ر�CAMERA,
 * @PARAM	
 * @RETURN	void*
 * @RETVAL	
 */

void* video_encode_read_data(void *arg)
{
	long size;	
	void *pbuf, *cur_frame_info;
	unsigned long ts;
    time_t osd_time;
	int moving_flag = 0;
    st_osd_frame * posd_frame ; 

	anyka_print("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());

    while(1)
    {
        anyka_print("[%s:%d] the thread sleep\n", __func__, __LINE__);
        sem_wait(&pvideo_ctrl->video_sem);
        anyka_print("[%s:%d] the thread wake\n", __func__, __LINE__);
        camera_on();
        //���û�б�������,���˳�
        while(1/*video_need_encode()*/)
        {
            //��һ:�Ȳɼ�ͼ��
            //�ڶ�:��ȷ���Ƿ�Ҫ���ƶ����
            //����:���ж���֡ͼ���Ƿ���Ҫ����
            //����:��ʼ��ͼ����б���

            
    		if((cur_frame_info = camera_getframe()) == NULL)
    		{
                anyka_print("[%s:%d] camera no data\n", __func__, __LINE__);
                usleep(10000);
    			continue;
    		}
    		time(&osd_time);
            camera_get_camera_info(cur_frame_info, (void**)&pbuf, &size, &ts);
			printf("------------size:%d------------------\r\n",size);
			
			if (camera_is_nighttime_mode()) 
				del_uv(pbuf + size, VIDEO_WIDTH_VGA, VIDEO_HEIGHT_VGA);
			camera_do_save_yuv(pbuf, size);

            pthread_mutex_lock(&pvideo_ctrl->move_ctrl_mutex);

            if(pvideo_ctrl->pmove_check_handle && pvideo_ctrl->pmove_check_handle->run_flag)
            {
                if(ts + pvideo_ctrl->pmove_check_handle->time_interval < pvideo_ctrl->pmove_check_handle->time_start )
                {
                    pvideo_ctrl->pmove_check_handle->time_start = ts -1;
                }
                
                if(ts > pvideo_ctrl->pmove_check_handle->time_start && 
                    (anyka_queue_items(pvideo_ctrl->pmove_check_handle->queue_handle) == 0))
                {
					moving_flag = 0;
				}
				else if (moving_flag == 0)
				{
					moving_flag = 1;
				}
				else
				{
					moving_flag = 2;
				}

				if (moving_flag == 0 || moving_flag == 1)
				{
                    T_STM_STRUCT * pstream;
                    int filter_flag = 0;

                    if(pvideo_ctrl->pmove_check_handle->filter_check)
                    {
                        filter_flag = pvideo_ctrl->pmove_check_handle->filter_check();
                    }

					/*
					 * filter_flag = 0, continue to move check; 
					 * filter_flag = 1, do not check but still send move_check signal;
					 * filter_flag = 2, do not check and do not send move_check signal;
					 */
                    if(filter_flag == 0)
                    {
                        pstream = video_malloc_stream_ram(size/4);
                        if(pstream)
                        {
    						pstream->timestamp = ts;
                            pstream->size = size/4;
                            memcpy(pstream->buf, pbuf + size, size/4);
                            if(anyka_queue_push(pvideo_ctrl->pmove_check_handle->queue_handle, (void *)pstream))
                            {
                                sem_post(&pvideo_ctrl->pmove_check_handle->video_data_mutex);
                                pvideo_ctrl->pmove_check_handle->time_start =
								   	ts + pvideo_ctrl->pmove_check_handle->time_interval;
                            }
                            else
                            {
                                video_free_stream_ram(pstream);
                            }
                        }
                    }
                    else if(filter_flag == 1)
                    {
                        if(pvideo_ctrl->pmove_check_handle->pcallback)
                        {
							//call the func: anyka_dection_video_notice_dowith to report message
                            pvideo_ctrl->pmove_check_handle->pcallback(pvideo_ctrl->pmove_check_handle->mydata, 
									filter_flag);
                        }
                    }
                    else
                    {
                    }
                }
            }
            pthread_mutex_unlock(&pvideo_ctrl->move_ctrl_mutex);
            //time stamp
            //encode
            // push queue
            // release sem
       
			// time stamp for all video
			camera_set_osd_context_attr(osd_time);
           
        

		    posd_frame  = malloc( sizeof(st_osd_frame));

			posd_frame->pframe = cur_frame_info;
			posd_frame->osd_time = osd_time;
			
            if(anyka_queue_push(pvideo_ctrl->encode_queue, (void *)posd_frame))
            {
				//send msg to encode
                sem_post(&pvideo_ctrl->camera_sem);
            }
            else
            {
                camera_usebufok(cur_frame_info);
            }

        }

        //���ر�CAMERA���
		while(anyka_queue_not_empty(pvideo_ctrl->encode_queue))
			usleep(200);
		pthread_mutex_lock(&pvideo_ctrl->video_encode_mutex);
        camera_off();
		pthread_mutex_unlock(&pvideo_ctrl->video_encode_mutex);
    }
    
	anyka_print("[%s:%d] This thread is going to exit, Id: %ld\n", __func__, __LINE__, (long int)gettid());
    pthread_exit(NULL);
}


/**
 * NAME         video_encode_data
 * @BRIEF	������Ƶ����
 * @PARAM	void *arg
 			
 * @RETURN	void* 
 * @RETVAL	
 */

void* video_encode_data(void *arg)
{
	anyka_print("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());

    void *pcamera_stream;
	long size;	
	void *pbuf;
	unsigned long ts;		
	st_osd_frame * posd_frame;
    
    while(1)
    {
        sem_wait(&pvideo_ctrl->camera_sem);
		pthread_mutex_lock(&pvideo_ctrl->video_encode_mutex);
        while(anyka_queue_not_empty(pvideo_ctrl->encode_queue))
        {
            posd_frame = (void *)anyka_queue_pop(pvideo_ctrl->encode_queue);
            if(posd_frame)
            {
            	pcamera_stream = posd_frame->pframe;
            	if (pcamera_stream)
            	{
	                camera_get_camera_info(pcamera_stream, (void**)&pbuf, &size, &ts);
	                video_encode_all_queue(pbuf, size, ts, posd_frame->osd_time);
	                
	                //�ͷ�CAMERA ������
	                camera_usebufok(pcamera_stream);
                }
                free(posd_frame);
            }
        }
		pthread_mutex_unlock(&pvideo_ctrl->video_encode_mutex);
    }
	anyka_print("[%s:%d] This thread is going to exit, Id: %ld\n", __func__, __LINE__, (long int)gettid());
}

/**
 * NAME         video_init
 * @BRIEF	 ��ʼ����Ƶ����������Դ,������߳�ע��
                     ��ؽ�����Ϣ
 
 * @PARAM	

 * @RETURN	void
 * @RETVAL	
 */

void* my_thread_sendto_msg(void *arg)
{

	while(1)
	{
		sem_post(&pvideo_ctrl->video_sem);

		printf("send sem\r\n");

		sleep(1);
	}
}

void video_init(void)
{
    //T_ENC_INPUT encode_info;
    struct encode_param encode_info = {0};
    video_setting *pvideo_default_config = anyka_get_sys_video_setting();     
    #ifdef CONFIG_ONVIF_SUPPORT
    Psystem_onvif_set_info ponvif_default_config = anyka_get_sys_onvif_setting();
    #endif
    Panyka_video_encode_handle encode_handle;
    if(NULL == pvideo_ctrl)
    {        
        video_encode_init();
        pvideo_ctrl = (Panyka_video_ctrl_info)malloc(sizeof(anyka_video_ctrl_info));
        memset(pvideo_ctrl, 0, sizeof(anyka_video_ctrl_info));
        pvideo_ctrl->pmove_check_handle = NULL;
        pvideo_ctrl->encode_queue = anyka_queue_init(10);
        pthread_mutex_init(&pvideo_ctrl->video_ctrl_mutex, NULL);
        pthread_mutex_init(&pvideo_ctrl->move_ctrl_mutex, NULL);
		pthread_mutex_init(&pvideo_ctrl->video_encode_mutex, NULL);
        sem_init(&pvideo_ctrl->video_sem, 0, 0);
        sem_init(&pvideo_ctrl->camera_sem, 0, 0);

        /***********************************************************************
            //����ʼ�����еı�������Ŀǰϵͳͬʱ���ĸ����
            ÿ�������һ�����У�����һ���߳����ڴ�������
            FRAMES_ENCODE_RECORD,   //30  FRAMES
            FRAMES_ENCODE_720P_NET, //10 FRAMES
            FRAMES_ENCODE_VGA_NET, // 15 FRAMES
            FRAMES_ENCODE_PICTURE, // PICTURE
            ***************************************************************************/

        //��ʼ������¼����߾�������Ƶ���������

		encode_info.minqp = 20;
		encode_info.maxqp = 51;
		encode_info.fps = 25;
		encode_info.goplen = encode_info.fps * 2;	   //current gop is stationary
		encode_info.bps = 2000;				   //kbps
		encode_info.profile = PROFILE_MAIN;	   //main profile
		encode_info.use_chn = ENCODE_SUB_CHN;   //use main yuv channel data
		encode_info.enc_grp = ENCODE_RECORD;     //assignment from enum encode_group_type
		encode_info.br_mode = BR_MODE_CBR;	   //default is cbr
		encode_info.enc_out_type = H264_ENC_TYPE;//h.264


        encode_info.width = VIDEO_WIDTH_720P;
        encode_info.height = VIDEO_HEIGHT_720P;
        //encode_info.lumWidthSrc = VIDEO_WIDTH_720P;
        //encode_info.lumHeightSrc = VIDEO_HEIGHT_720P;
        encode_info.minqp = pvideo_default_config->minQp;
        encode_info.maxqp = pvideo_default_config->maxQp;
        #ifdef CONFIG_ONVIF_SUPPORT
        encode_info.goplen = pvideo_default_config->gopLen * ponvif_default_config->fps1;
        encode_info.fps = ponvif_default_config->fps1;
        encode_info.bps = ponvif_default_config->kbps1;
        #else
        encode_info.gopLen = pvideo_default_config->gopLen * pvideo_default_config->savefilefps;
        encode_info.framePerSecond = pvideo_default_config->savefilefps;
        encode_info.bitPerSecond = pvideo_default_config->savefilekbps;
        #endif
        encode_info.enc_out_type = H264_ENC_TYPE;
		encode_info.br_mode = pvideo_default_config->video_mode;
        encode_handle = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD];
        encode_handle->index = 0;
        encode_handle->frames = encode_info.framePerSecond;
        encode_handle->encode_use_flag = 0;
        encode_handle->video_size_flag = 1;
        encode_handle->encode_handle = video_encode_open(&encode_info);
        encode_handle->pthread_info = NULL;
        encode_handle->stream_size = 0;

        //��ʼ�� 720P��������Ƶ���
        encode_info.width = VIDEO_WIDTH_720P;
        encode_info.height = VIDEO_HEIGHT_720P;
        encode_info.lumWidthSrc = VIDEO_WIDTH_720P;
        encode_info.lumHeightSrc = VIDEO_HEIGHT_720P;
        encode_info.minQp = pvideo_default_config->minQp;
        encode_info.maxQp = pvideo_default_config->maxQp;
        encode_info.gopLen = pvideo_default_config->gopLen * pvideo_default_config->V720Pfps;
        encode_info.framePerSecond = pvideo_default_config->V720Pfps;
        encode_info.bitPerSecond = pvideo_default_config->V720Pminkbps;
        encode_info.encode_type = H264_ENC_TYPE;
		encode_info.video_mode = pvideo_default_config->video_mode;
        encode_handle = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET];
        encode_handle->index = 0;
        encode_handle->frames = encode_info.framePerSecond;
        encode_handle->encode_use_flag = 0;
        encode_handle->video_size_flag = 1;
        encode_handle->encode_handle = video_encode_open(&encode_info);
        encode_handle->pthread_info = NULL;
        encode_handle->stream_size = 0;

        //��ʼ�� VGA������Ƶ���
        encode_info.width = VIDEO_WIDTH_VGA;
        encode_info.height = VIDEO_HEIGHT_VGA;
        encode_info.lumWidthSrc = VIDEO_WIDTH_VGA;
        encode_info.lumHeightSrc = VIDEO_HEIGHT_VGA;
        encode_info.minQp = pvideo_default_config->minQp;
        encode_info.maxQp = pvideo_default_config->maxQp;
        #ifdef CONFIG_ONVIF_SUPPORT
        encode_info.gopLen = pvideo_default_config->gopLen * ponvif_default_config->fps2;
        encode_info.framePerSecond = ponvif_default_config->fps2;
        encode_info.bitPerSecond = ponvif_default_config->kbps2;        
        #else
        encode_info.gopLen = pvideo_default_config->gopLen * pvideo_default_config->VGAPfps;
        encode_info.framePerSecond = pvideo_default_config->VGAPfps;
        encode_info.bitPerSecond = pvideo_default_config->VGAminkbps;
        #endif
        encode_info.encode_type = H264_ENC_TYPE;
		encode_info.video_mode = pvideo_default_config->video_mode;
        encode_handle = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_VGA_NET];
        encode_handle->index = 0;
        encode_handle->frames = encode_info.framePerSecond;
        encode_handle->encode_use_flag = 0;
        encode_handle->video_size_flag = 0;
        encode_handle->encode_handle = video_encode_open(&encode_info);
        encode_handle->pthread_info = NULL;
        encode_handle->stream_size = 0;

        //��ʼ�� picture ������
        encode_handle = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_PICTURE];
        if(pvideo_default_config->pic_ch)
        {
            encode_info.width = VIDEO_WIDTH_VGA;
            encode_info.height = VIDEO_HEIGHT_VGA;
            encode_info.lumWidthSrc = VIDEO_WIDTH_VGA;
            encode_info.lumHeightSrc = VIDEO_HEIGHT_VGA;
            encode_info.encode_type = MJPEG_ENC_TYPE;
            encode_handle->video_size_flag = 0;
        }
        else
        {
            encode_info.width = VIDEO_WIDTH_720P;
            encode_info.height = VIDEO_HEIGHT_720P;
            encode_info.lumWidthSrc = VIDEO_WIDTH_720P;
            encode_info.lumHeightSrc = VIDEO_HEIGHT_720P;
            encode_info.encode_type = MJPEG_ENC_TYPE;
            encode_handle->video_size_flag = 1;
        }
        encode_handle->frames = camera_get_max_frame();
        encode_handle->index = 0;
        encode_handle->encode_use_flag = 0;
        encode_handle->encode_handle = video_encode_open(&encode_info);
        encode_handle->pthread_info = NULL;

        
        if (0 != anyka_pthread_create(&(pvideo_ctrl->video_thread),  &video_encode_read_data, NULL, ANYKA_THREAD_NORMAL_STACK_SIZE, 99)) 
        {
        }
        if (0 != anyka_pthread_create(&(pvideo_ctrl->video_thread),  &video_encode_data, NULL, ANYKA_THREAD_NORMAL_STACK_SIZE, 99)) 
        {
        }

		pthread_t my_pthread;

		if(0 != anyka_pthread_create(&my_pthread,  &my_thread_sendto_msg, NULL, ANYKA_THREAD_NORMAL_STACK_SIZE, 99))
		{}

    }  
    
}



/**
 * NAME         video_malloc_stream_ram
 * @BRIEF	 ������Ƶ�ڴ�
 
 * @PARAM	

 * @RETURN	void
 * @RETVAL	
 */

T_STM_STRUCT * video_malloc_stream_ram(int size)
{
    T_STM_STRUCT *pStream;
    
    pStream = malloc(sizeof(T_STM_STRUCT));
    if(pStream)
    {
        pStream->buf = malloc(size);
        if(pStream->buf == NULL)
        {
            free(pStream);
            return NULL;
        }
    }

    return pStream;
}


/**
 * NAME         video_free_stream_ram
 * @BRIEF	�ͷ���Ƶ�ڴ�
 
 * @PARAM	

 * @RETURN	void
 * @RETVAL	
 */

void video_free_stream_ram(void *pStream)
{
    T_STM_STRUCT *tmp = (T_STM_STRUCT*)pStream;

    if(pStream)
    {
        free(tmp->buf);
        free(tmp);
    }
}


/**
 * NAME         video_send_thread
 * @BRIEF	��ע�����ģ�鷢����Ƶ�����������ͷ������Դ 
 
 * @PARAM	
  
 * @RETURN	void
 * @RETVAL	
 */

void* video_send_thread(void *arg)
{
	anyka_debug("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());
    
    Panyka_video_thread_info pthread_info = (Panyka_video_thread_info)arg;
    
    while(pthread_info->run_flag)
    {
    	sem_wait(&pthread_info->video_data_mutex);
        //��������ж���������,��һ�η�����������,Ȼ���ٽ���ȴ�״̬
        while(pthread_info->run_flag && anyka_queue_not_empty(pthread_info->queue_handle))
        {
	        T_STM_STRUCT *stream = (T_STM_STRUCT *)anyka_queue_pop(pthread_info->queue_handle);

            pthread_info->pcallback(pthread_info->mydata, stream);
            video_free_stream_ram((void *)stream);
        }
    }
	anyka_debug("[%s:%d] This thread is going to exit, Id: %ld\n", __func__, __LINE__, (long int)gettid());
    pthread_exit(NULL);
}


/**
 * NAME         video_select_encode_handle
 * @BRIEF	FRAMES_ENCODE_RECORD��FRAMES_ENCODE_720P_NET������ͬʱ����,��оƬ�ı�����������
                    ������ɲ�ͬ����������л�������¼���֡�ʿ��ܹ���10
 
 * @PARAM	user   
                    video_bps
                    
 * @RETURN	void
 * @RETVAL	
 */
void video_select_encode_handle(Panyka_video_thread_info user, int video_bps)
{
    Panyka_video_encode_handle find ;
    Panyka_video_thread_info head;
    
	pthread_mutex_lock(&pvideo_ctrl->video_ctrl_mutex);
#ifdef VIDEO_LOW_POWER_SWITCH
    if(user->video_type == FRAMES_ENCODE_PICTURE)
    {
        find = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_PICTURE];
    }
    else if(user->video_type == FRAMES_ENCODE_VGA_NET)
    {
        find = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_VGA_NET];
    }
    else if(user->video_type == FRAMES_ENCODE_RECORD)
    {
        if(pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].pthread_info)
        {
            find = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET];
        }
        else
        {
            find = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD];
        }
    }
    else  if(user->video_type == FRAMES_ENCODE_720P_NET)
    {
        if(pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].pthread_info)
        {
            pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].pthread_info = pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].pthread_info;
            pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].pthread_info = NULL;
            pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].encode_use_flag = 0;
        }
        find = &pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET];        
    }
#else
    find = &pvideo_ctrl->encode_handle[user->video_type];
#endif
    user->next = NULL;
    if(find->pthread_info == NULL)
    {
        find->pthread_info = user; 
    }
    else
    {
        head = find->pthread_info;
        while(head->next)
        {
            head = head->next;
        }
        head->next = user;
    }
    video_encode_set_Iframe(find->encode_handle);
    if(user->video_type != FRAMES_ENCODE_PICTURE)
    {
        video_encode_reSetRc(find->encode_handle, video_bps);
    }
    find->encode_use_flag = 1;
	pthread_mutex_unlock(&pvideo_ctrl->video_ctrl_mutex);
}


/**
 * NAME         video_check_encode_user_type
 * @BRIEF	 ���ϵͳ �е����о�� �����ӻ�ɾ����
                     �޸����Ͳ�һ�µ�����,
 * @PARAM	  
 * @RETURN	void
 * @RETVAL	
 */
void video_check_encode_user_type()
{        
    Panyka_video_thread_info head;
    
    if(NULL == pvideo_ctrl)
    {
        return ;
    }

    if(pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].pthread_info)
    {
        if(pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].pthread_info)
        {
            head = pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].pthread_info;
            while(head->next)
            {
                head = head->next;
            }
            head->next = pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].pthread_info;
            pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].pthread_info = NULL;
            pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].encode_use_flag = 0;
            video_encode_set_Iframe(pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].encode_handle);
        }
    }
    else 
    {
        if(pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].pthread_info)
        {
            head = pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].pthread_info;
            if((NULL == head->next) && (head->video_type == FRAMES_ENCODE_RECORD))
            {
                pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].pthread_info = head;
                pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].encode_use_flag = 1;
                pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].pthread_info = NULL;
                pvideo_ctrl->encode_handle[FRAMES_ENCODE_720P_NET].encode_use_flag = 0;
                video_encode_set_Iframe(pvideo_ctrl->encode_handle[FRAMES_ENCODE_RECORD].encode_handle);
            }
        }
    }    
}

/**
 * NAME         video_set_encode_fps
 * @BRIEF	 ���ñ����֡�ʣ�Ŀǰֻ��ONVIF������÷�
 
 
 * @PARAM    video_type
                    fps
 * @RETURN	void
 * @RETVAL	
 */


void video_set_encode_fps(int video_type, int fps)
{
    pvideo_ctrl->encode_handle[video_type].frames = fps;
}


/**
 * NAME         video_set_encode_bps
 * @BRIEF	 ���ñ�������ʣ�Ŀǰֻ��ONVIF������÷�
 * @PARAM	video_type
                    bitrate
 * @RETURN	void
 * @RETVAL	
 */

void video_set_encode_bps(int video_type, int bitrate)
{
    video_encode_reSetRc(pvideo_ctrl->encode_handle[video_type].encode_handle, bitrate);
}


/**
 * NAME         video_add
 * @BRIEF	 ������Ƶ�����̣߳�������������ͨ���ص�����Ӧ��,
 * @PARAM	pcallback           ��Ƶ�ص�����
                    mydata              �û�����
                    video_type
                    video_bps
 * @RETURN	void
 * @RETVAL	
 */


void video_add(P_VIDEO_DOWITH_CALLBACK pcallback, T_VOID *mydata, int video_type, int video_bps)
{
    Panyka_video_thread_info pthread_info;
    
    if(NULL == pvideo_ctrl)
    {
        anyka_print("[%s:%d] it fail to add because you must init video model!\n", __func__, __LINE__);
        return ;
    }

    pthread_info = (Panyka_video_thread_info)malloc(sizeof(anyka_video_thread_info));
    if(NULL == pthread_info)
    {
        return ;
    }
    pthread_info->next = NULL;
    pthread_info->mydata = mydata;
    pthread_info->pcallback = pcallback;
    if(NULL == (pthread_info->queue_handle = anyka_queue_init(MAX_FRAME_SEND_BUF)))
    {
        free(pthread_info);
        return;
    }
    pthread_info->video_type = video_type;
    pthread_info->run_flag = 1;
    //�����̺߳��ź���,Ĭ��Ϊû������.
    sem_init(&pthread_info->video_data_mutex, 0, 0); 
    video_select_encode_handle(pthread_info, video_bps);
	
    if (0 != anyka_pthread_create(&(pthread_info->id ), &video_send_thread, (void *)pthread_info, ANYKA_THREAD_MIN_STACK_SIZE, -1)) 
    {
        sem_destroy(&pthread_info->video_data_mutex);
        anyka_queue_destroy(pthread_info->queue_handle, video_free_stream_ram);
        free(pthread_info);
        pthread_detach(pthread_info->id);
    } 	

    sem_post(&pvideo_ctrl->video_sem);
}


/**
 * NAME     video_add
 * @BRIEF	�޸���Ƶ����ز���
 * @PARAM	mydata              �û�����
            video_type
            video_bps
 * @RETURN	void
 * @RETVAL	
 */
void video_change_attr(T_VOID *mydata, int video_type, int video_bps)
{
    int i;
    Panyka_video_thread_info cur, head;
    
    if(NULL == pvideo_ctrl)
    {
        return ;
    }
    
	pthread_mutex_lock(&pvideo_ctrl->video_ctrl_mutex);
    for(i = 0; i < FRAMES_ENCODE_HANDLE_NUM; i++)
    {
        head = pvideo_ctrl->encode_handle[i].pthread_info;
        cur = head;
        while(head && (head->mydata != mydata))
        {
            cur  = head;
            head = head->next;
        }
        if(head)
        {
            if(video_type == i)
            {
                video_encode_reSetRc(pvideo_ctrl->encode_handle[i].encode_handle, video_bps);
            }
            else
            {
                //����л������ʽ�����ȴ�һ��������ɾ�������ӵ���һ��
                if(head == pvideo_ctrl->encode_handle[i].pthread_info)
                {
                    pvideo_ctrl->encode_handle[i].pthread_info = head->next;
                }
                else
                {
                    cur->next = head->next;
                }
                if(pvideo_ctrl->encode_handle[i].pthread_info == NULL)
                {
                    pvideo_ctrl->encode_handle[i].encode_use_flag = 0;
                }
                head->next = NULL;
                video_encode_reSetRc(pvideo_ctrl->encode_handle[video_type].encode_handle, video_bps);
                cur = pvideo_ctrl->encode_handle[video_type].pthread_info;
                if(cur == NULL)
                {
                    pvideo_ctrl->encode_handle[video_type].pthread_info = head;
                }
                else
                {
                    while(cur->next)
                    {
                        cur = cur->next;
                    }
                    cur->next = head;
                }
                pvideo_ctrl->encode_handle[video_type].encode_use_flag = 1;
            }
            break;
        }
    }
#ifdef VIDEO_LOW_POWER_SWITCH
    video_check_encode_user_type();
#endif
	pthread_mutex_unlock(&pvideo_ctrl->video_ctrl_mutex);
}



/**
 * NAME         video_set_iframe
 * @BRIEF	ǿ�ƽ���Ƶ����ΪI֡
 * @PARAM	mydata              �û�����
 * @RETURN	void
 * @RETVAL	
 */

void video_set_iframe(T_VOID *mydata)
{
    int i;
    Panyka_video_thread_info cur, head;
    
    if(NULL == pvideo_ctrl)
    {
        return ;
    }

    
	pthread_mutex_lock(&pvideo_ctrl->video_ctrl_mutex);
    for(i = 0; i < FRAMES_ENCODE_HANDLE_NUM; i++)
    {
        head = pvideo_ctrl->encode_handle[i].pthread_info;
        cur = head;
        while(head && (head->mydata != mydata))
        {
            cur  = head;
            head = head->next;
        }
        if(head)
        {
            video_encode_set_Iframe(pvideo_ctrl->encode_handle[i].encode_handle);
            break;
        }
    }
	pthread_mutex_unlock(&pvideo_ctrl->video_ctrl_mutex);
}


/**
 * NAME         video_del
 * @BRIEF	�رն�Ӧ����Ƶ�����������ܻᴥ��CAMERA�Ĺرն���
 * @PARAM	mydata              �û�����
 * @RETURN	void
 * @RETVAL	
 */
void video_del(T_VOID *mydata)
{
    int i;
    Panyka_video_thread_info cur, head;
    
    if(NULL == pvideo_ctrl)
    {
        return ;
    }

	pthread_mutex_lock(&pvideo_ctrl->video_ctrl_mutex);
    for(i = 0; i < FRAMES_ENCODE_HANDLE_NUM; i++)
    {
        head = pvideo_ctrl->encode_handle[i].pthread_info;
        cur = head;
        while(head && (head->mydata != mydata))
        {
            cur  = head;
            head = head->next;
        }
        if(head)
        {
            if(cur == head)
            {
                pvideo_ctrl->encode_handle[i].pthread_info = head->next;
            }
            else
            {
                cur->next = head->next;
            }
            //�������û�����񣬽������ò�����
            if(pvideo_ctrl->encode_handle[i].pthread_info == NULL)
            {
                pvideo_ctrl->encode_handle[i].encode_use_flag = 0;
                pvideo_ctrl->encode_handle[i].index = 0;
            }
            #ifdef VIDEO_LOW_POWER_SWITCH
            video_check_encode_user_type();
            #endif
            pthread_mutex_unlock(&pvideo_ctrl->video_ctrl_mutex);
            head->run_flag = 0;
            sem_post(&head->video_data_mutex);
            //���رշ��������߳�,���ͷ������߳�ռ�õ���Դ.
            //anyka_print("[%s:%d] waiting until thread is over!\n", __func__, __LINE__);
            //sleep(1);
            pthread_join(head->id, NULL);
            sem_destroy(&head->video_data_mutex);
            anyka_queue_destroy(head->queue_handle, video_free_stream_ram);
            anyka_debug("[%s:%d] the video thread will be destroy!\n", __func__, __LINE__);
            free(head);
            return;
        }
    }
	pthread_mutex_unlock(&pvideo_ctrl->video_ctrl_mutex);
}


/**
 * NAME         video_move_check_malloc
 * @BRIEF	�ƶ��������ڴ�
 * @PARAM	T_U32 size, Ҫ������ڴ��С
 			T_pSTR filename, 
 			T_U32 line
 * @RETURN	T_pVOID
 * @RETVAL	ָ��������ڴ��ַ
 */

static T_pVOID video_move_check_malloc(T_U32 size, T_pSTR filename, T_U32 line)
{
	return malloc(size);
}


/**
 * NAME         video_move_check_free
 * @BRIEF	�ͷ��ƶ���������ڴ�
 * @PARAM	T_pVOID mem���ڴ�ռ��׵�ַ
 * @RETURN	T_pVOID
 * @RETVAL	NULL
 */

static T_pVOID video_move_check_free(T_pVOID mem)
{
	free(mem);
	return NULL;
}

/**
 * NAME         video_init_move_handle
 * @BRIEF	��ʼ���ƶ���⹦��
 * @PARAM	height
                    width
                    detction
                    ratios
 * @RETURN	0-->fail; 1-->ok
 * @RETVAL	
 */

int video_init_move_handle(int height, int width, T_MOTION_DETECTOR_DIMENSION *detction, uint16 *ratios)
{
    T_MOTION_DETECTOR_OPEN_PARA openParam;

    
    //bzero( CompareData, sizeof( CompareData ) );
    bzero( &openParam, sizeof( T_MOTION_DETECTOR_OPEN_PARA ) );
    
    openParam.m_uWidth      = (T_U16)width; //any
    openParam.m_uHeight     = (T_U16)height;    //any
        
    if (width > WIDTH_SVGA)
        openParam.m_uIntervalHoriNum = 1;
    else
        openParam.m_uIntervalHoriNum = 0;

    if (height > HEIGHT_SVGA)
        openParam.m_uIntervalVeriNum = 1;
    else
        openParam.m_uIntervalVeriNum = 0;

    openParam.m_uThreshold  = (T_U16)THRESHOLD; //10
    openParam.m_Dimension.m_uHoriNum = detction->m_uHoriNum;
    openParam.m_Dimension.m_uVeriNum = detction->m_uVeriNum;
    
    openParam.m_CBFunc.m_FunPrintf = (MEDIALIB_CALLBACK_FUN_PRINTF)anyka_print;
    openParam.m_CBFunc.m_FunMalloc = (MEDIALIB_CALLBACK_FUN_MALLOC)video_move_check_malloc;
    openParam.m_CBFunc.m_FunFree   = (MEDIALIB_CALLBACK_FUN_FREE)video_move_check_free;

    //open the detector
//    pvideo_ctrl->pmove_check_handle->encode_handle = Motion_Detector_Open( &openParam );
    if (!pvideo_ctrl->pmove_check_handle->encode_handle) 
    {
        anyka_print("[%s:%d] motion detector open failed!\n", __func__, __LINE__);
        
        return 0;
    }
    anyka_print("[%s:%d] Open MotionDect: ratio=%d, m_uThreshold=%d\n",
					__func__, __LINE__, *ratios, openParam.m_uThreshold);    
	
//    Motion_Detector_SetRatio(pvideo_ctrl->pmove_check_handle->encode_handle, ratios);   
    return 1;
}
/**
 * NAME         video_destroy_move_handle
 * @BRIEF	�ر��ƶ���⹦��
 * @PARAM	
 * @RETURN	
 * @RETVAL	
 */


void video_destroy_move_handle()
{
    
    if((pvideo_ctrl  && pvideo_ctrl->pmove_check_handle && pvideo_ctrl->pmove_check_handle->encode_handle))
    {
//        Motion_Detector_Close(pvideo_ctrl->pmove_check_handle->encode_handle);
        pvideo_ctrl->pmove_check_handle->encode_handle = NULL;
    }
}
/**
 * NAME         video_set_move_check_attr
 * @BRIEF	�����ƶ�������
 * @PARAM	ratios
 * @RETURN	
 * @RETVAL	
 */

void video_set_move_check_attr(uint16 *ratios)
{
    if(pvideo_ctrl && pvideo_ctrl->pmove_check_handle && pvideo_ctrl->pmove_check_handle->encode_handle)
    {
//        Motion_Detector_SetRatio(pvideo_ctrl->pmove_check_handle->encode_handle, ratios);
    }
    
}

/**
 * NAME         video_move_check_main
 * @BRIEF	�ƶ����������
 * @PARAM	arg
 * @RETURN	
 * @RETVAL	
 */

void* video_move_check_main(void *arg)
{
	anyka_print("[%s:%d] This thread id : %ld\n", __func__, __LINE__, (long int)gettid());
	
	int pre_2_cmp = 0;
	int pre_2_stream_moving = 0;
    int cur_flag = 0;
    T_STM_STRUCT *_1st_stream = NULL, *_2nd_stream = NULL, *cur_stream;
    Panyka_video_check_move_info pmove_handle;
    
    pmove_handle = pvideo_ctrl->pmove_check_handle;
    while(pmove_handle->run_flag)
    {
        sem_wait(&pmove_handle->video_data_mutex);
        if(_1st_stream == NULL)
        {
            _1st_stream = anyka_queue_pop(pmove_handle->queue_handle);
        }

		if (_1st_stream && !_2nd_stream)
		{
            _2nd_stream = anyka_queue_pop(pmove_handle->queue_handle);
		}

		if (_1st_stream && _2nd_stream && !pre_2_cmp)
		{
//			cur_flag = Motion_Detector_Handle(pmove_handle->encode_handle, _1st_stream->buf, _2nd_stream->buf);
			if (cur_flag)
				pre_2_stream_moving = 1;
			else 
				pre_2_stream_moving = 0;

			pre_2_cmp = 1;
		}
        
        if(_2nd_stream && (cur_stream = anyka_queue_pop(pmove_handle->queue_handle)))
        {
			pre_2_cmp = 0;

			if (pre_2_stream_moving)
			{
				// check the yuv data;
//				cur_flag = Motion_Detector_Handle(pmove_handle->encode_handle, _2nd_stream->buf, cur_stream->buf);
				if(cur_flag && (pmove_handle->pcallback))
				{
					if(pvideo_ctrl->check_frame)
					{
						pmove_handle->pcallback(pmove_handle->mydata, cur_flag);
					}
					//anyka_print("detect video moving: %u-%u-%u\n", _1st_stream->timestamp, _2nd_stream->timestamp, cur_stream->timestamp);
				}
			}
            pvideo_ctrl->check_frame ++;
            video_free_stream_ram(_1st_stream);
            video_free_stream_ram(_2nd_stream);
            _1st_stream = cur_stream;
            _2nd_stream = NULL;
        }
    }
	video_free_stream_ram(_1st_stream);
	video_free_stream_ram(_2nd_stream);

	anyka_print("[%s:%d] This thread is going to exit, Id: %ld\n", __func__, __LINE__, (long int)gettid());
	return NULL;
}



/**
 * NAME         video_start_move_check
 * @BRIEF	�����ƶ����������
 * @PARAM	height
                    width
                    detction
                    ratios
                    Palarm_func  ����������ʱ�Ļص�����
                    user        �û�����
                    filter_check   ��ǰ�Ƿ�������ģ�Ŀǰ�����Ѷ������Ƶ�ۿ���ʱ�������
 * @RETURN	0-->fail; 1-->ok
 * @RETVAL	
 */

int video_start_move_check(int height, int width, T_MOTION_DETECTOR_DIMENSION *detction, uint16 *ratios, int time, video_move_check_notice_callback pcallback, void *user, PANYKA_FILTER_VIDEO_CHECK_FUNC filter_check)
{
    if(NULL == pvideo_ctrl)
    {
        anyka_print("[%s:%d] it fails to add beacuse you must init video model!\n", __func__, __LINE__);
        return -1;
    }

    if(pvideo_ctrl->pmove_check_handle)
    {
        anyka_print("[%s:%d] it fails to add beacuse it has runned!\n", __func__, __LINE__);
        return -2;
    }
    
    pthread_mutex_lock(&pvideo_ctrl->move_ctrl_mutex);
    pvideo_ctrl->pmove_check_handle = (Panyka_video_check_move_info)malloc(sizeof(anyka_video_check_move_info));
    if(pvideo_ctrl->pmove_check_handle == NULL)
    {
        anyka_print("[%s:%d] it fails to malloc!\n", __func__, __LINE__);
        pthread_mutex_unlock(&pvideo_ctrl->move_ctrl_mutex);
        return -3;
    }
    
    memset(pvideo_ctrl->pmove_check_handle, 0, sizeof(anyka_video_check_move_info));
    if(sem_init(&pvideo_ctrl->pmove_check_handle->video_data_mutex, 0, 0))
    {
        // do with err
        anyka_print("[%s:%d] it fails to create sem!\n", __func__, __LINE__);
        goto ERR_video_add_move_check;
    }
    
    pvideo_ctrl->pmove_check_handle->queue_handle = anyka_queue_init(50);
    if(NULL == pvideo_ctrl->pmove_check_handle->queue_handle)
    {
        anyka_print("[%s:%d] it fails to create queue!\n", __func__, __LINE__);
        goto ERR_video_add_move_check;
    }
    
    pvideo_ctrl->pmove_check_handle->time_interval = time;
    pvideo_ctrl->pmove_check_handle->time_start = 0;
    pvideo_ctrl->pmove_check_handle->run_flag = 1;
    pvideo_ctrl->pmove_check_handle->pcallback = pcallback;
    pvideo_ctrl->pmove_check_handle->filter_check = filter_check;
    pvideo_ctrl->pmove_check_handle->mydata = user;
    pvideo_ctrl->check_frame = 0;
    if(video_init_move_handle(height, width, detction, ratios) == 0)
    {
        // do with err
        anyka_print("[%s:%d] it fails to create move video_handle!\n", __func__, __LINE__);
        goto ERR_video_add_move_check;
    }
    if (0 != anyka_pthread_create(&(pvideo_ctrl->pmove_check_handle->id), video_move_check_main, (void *)pvideo_ctrl->pmove_check_handle, ANYKA_THREAD_NORMAL_STACK_SIZE, 10)) 
    {
        goto ERR_video_add_move_check;
    } 
    pthread_mutex_unlock(&pvideo_ctrl->move_ctrl_mutex);
    sem_post(&pvideo_ctrl->video_sem);
    return 1;

 ERR_video_add_move_check:
    sem_destroy(&pvideo_ctrl->pmove_check_handle->video_data_mutex);
    anyka_queue_destroy(pvideo_ctrl->pmove_check_handle->queue_handle, video_free_stream_ram);
    video_destroy_move_handle();
    free(pvideo_ctrl->pmove_check_handle);
    pvideo_ctrl->pmove_check_handle = NULL;
    pthread_mutex_unlock(&pvideo_ctrl->move_ctrl_mutex);
    return 0;
}


/**
 * NAME         video_start_move_check
 * @BRIEF	ֹͣ�ƶ����
 * @PARAM	
 * @RETURN	
 * @RETVAL	
 */

void video_stop_move_check(void)
{
    if(NULL == pvideo_ctrl || (NULL == pvideo_ctrl->pmove_check_handle))
    {
        anyka_print("[%s:%d] it fail to del beacuse you must init video model!\n", __func__, __LINE__);
        return ;
    }
    pthread_mutex_lock(&pvideo_ctrl->move_ctrl_mutex);
    pvideo_ctrl->pmove_check_handle->run_flag = 0;
    sem_post(&pvideo_ctrl->pmove_check_handle->video_data_mutex);
    pthread_join(pvideo_ctrl->pmove_check_handle->id, NULL);
    sem_destroy(&pvideo_ctrl->pmove_check_handle->video_data_mutex);
    anyka_queue_destroy(pvideo_ctrl->pmove_check_handle->queue_handle, video_free_stream_ram);
    video_destroy_move_handle();
    free(pvideo_ctrl->pmove_check_handle);
    pvideo_ctrl->pmove_check_handle = NULL;
    anyka_print("[%s:%d] moving check has stoppedl!\n", __func__, __LINE__);
    pthread_mutex_unlock(&pvideo_ctrl->move_ctrl_mutex);
}

