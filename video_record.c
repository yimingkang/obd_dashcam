/* 
 * 
 * Author: Tasanakorn, rk
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>				// multithreading
#include <stdint.h>
#include <unistd.h>
#include <cairo/cairo.h>

#include "bcm_host.h"
#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"
#include "sys_stat.h"
#include "rpi_uart_dev/include/rpi_uart.h"
#include "rpi_uart_dev/include/rpi_logger.h"
#include "include/video_record.h"


#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

#define VIDEO_FPS 30 
#define VIDEO_WIDTH 1280
#define VIDEO_HEIGHT 720
 
#define OVERLAY_WIDTH 1150
#define OVERLAY_HEIGHT 40
#define OVERLAY_REFRESH_RT 50000

// 10 min per vid
#define VID_LENGTH_LIMIT 600
// 200 mins 
#define VID_FILE_LIMIT 20
#define VID_FILE_PREFIX "vid_out"
#define VID_FILE_SUFFIX ".h264"


typedef struct {
    int width;
    int height;
    MMAL_COMPONENT_T *camera;
    MMAL_COMPONENT_T *encoder;
    MMAL_COMPONENT_T *preview;
    MMAL_PORT_T *camera_preview_port;
    MMAL_PORT_T *camera_video_port;
    MMAL_PORT_T *camera_still_port;
    MMAL_POOL_T *camera_video_port_pool;
    MMAL_PORT_T *encoder_input_port;
    MMAL_POOL_T *encoder_input_pool;
    MMAL_PORT_T *encoder_output_port;
    MMAL_POOL_T *encoder_output_pool;
    uint8_t *overlay_buffer;
    uint8_t *overlay_buffer2;
    int overlay;
    float fps;
	FILE *fd;

    uint8_t next_file_number;
    uint8_t time_up;
    pthread_t timer_thread;
} PORT_USERDATA;


static void camera_video_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {
    static int frame_count = 0;
    static struct timespec t1;
    struct timespec t2;
    uint8_t *local_overlay_buffer;

    //fprintf(stderr, "INFO:%s\n", __func__);
    if (frame_count == 0) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
    }
    clock_gettime(CLOCK_MONOTONIC, &t2);

    int d = t2.tv_sec - t1.tv_sec;

    MMAL_BUFFER_HEADER_T *new_buffer;
    MMAL_BUFFER_HEADER_T *output_buffer = 0;
    PORT_USERDATA *userdata = (PORT_USERDATA *) port->userdata;

    MMAL_POOL_T *pool = userdata->camera_video_port_pool;


    frame_count++;

    output_buffer = mmal_queue_get(userdata->encoder_input_pool->queue);

    //Set pointer to  latest updated/drawn double buffer to local pointer  
    if (userdata->overlay == 0) {
        local_overlay_buffer = userdata->overlay_buffer;
    }
    else {
        local_overlay_buffer = userdata->overlay_buffer2;
    }

    //Try to some colors http://en.wikipedia.org/wiki/YUV
    int chrominance_offset = userdata->width * userdata->height;
    int v_offset = chrominance_offset / 4;
    int chroma = 0;

    if (output_buffer) {
        mmal_buffer_header_mem_lock(buffer);
        memcpy(output_buffer->data, buffer->data, buffer->length);
        // dim
        int x, y;
        for (x = 0; x < OVERLAY_WIDTH; x++) {
            for (y = 0; y < OVERLAY_HEIGHT; y++) {
                //copy luma Y
                //output_buffer->data[y * userdata->width + x ] = 0xdf;
                //pointer to chrominance U/V
                chroma= y / 2 * userdata->width / 2 + x / 2 + chrominance_offset;
                if (local_overlay_buffer[(y * OVERLAY_WIDTH+ x) * 4] > 0) {
                	output_buffer->data[y * userdata->width + x ] = 0xff;
                    //just guessing colors 
                    output_buffer->data[chroma] = 0x80 ;
                    output_buffer->data[chroma+v_offset] = 0x80 ;
                }
				else{
					// else fill with black
                    //just guessing colors 
                	output_buffer->data[y * userdata->width + x ] = 0x80;
				}
            }
        }

        output_buffer->length = buffer->length;
        mmal_buffer_header_mem_unlock(buffer);
        if (mmal_port_send_buffer(userdata->encoder_input_port, output_buffer) != MMAL_SUCCESS) {
            fprintf(stderr, "ERROR: Unable to send buffer \n");
        }
    } else {
        fprintf(stderr, "ERROR: mmal_queue_get (%d)\n", output_buffer);
    }


    if (frame_count % 10 == 0) {
        // print framerate every n frame
        clock_gettime(CLOCK_MONOTONIC, &t2);
        float d = (t2.tv_sec + t2.tv_nsec / 1000000000.0) - (t1.tv_sec + t1.tv_nsec / 1000000000.0);
        float fps = 0.0;

        if (d > 0) {
            fps = frame_count / d;
        } else {
            fps = frame_count;
        }
        userdata->fps = fps;
        fprintf(stderr, "  Frame = %d,  Framerate = %.1f fps \n", frame_count, fps);
    }


    mmal_buffer_header_release(buffer);

    // and send one back to the port (if still open)
    if (port->is_enabled) {
        MMAL_STATUS_T status;

        new_buffer = mmal_queue_get(pool->queue);

        if (new_buffer) {
            status = mmal_port_send_buffer(port, new_buffer);
        }

        if (!new_buffer || status != MMAL_SUCCESS) {
            fprintf(stderr, "Error: Unable to return a buffer to the video port\n");
        }
    }
}

static void encoder_input_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {
    //fprintf(stderr, "INFO:%s\n", __func__);    
    mmal_buffer_header_release(buffer);
}

void *time_keeper(void *udata){
	fprintf(stderr, "Starting timer...\n");
    PORT_USERDATA *userdata = (PORT_USERDATA *) udata;
    if(userdata->time_up){
        fprintf(stderr, "ERROR: time_up is not cleared when timer is started\n");
        exit(-1);
    }
    sleep(VID_LENGTH_LIMIT);
    userdata->time_up = 1;
}

void *dummy_thread(void *dummy_arg){
	return NULL;
}

void start_timer(PORT_USERDATA *userdata){

    // if timer is still running, wait it out (should NOT happen);
    pthread_join(userdata->timer_thread, NULL);
    pthread_create(&(userdata->timer_thread), NULL, time_keeper, userdata);
    return;
}
void renew_fd(PORT_USERDATA *userdata){
    if(userdata->time_up){
        fprintf(stderr, "Renewing file descriptor...");
        fflush(userdata->fd);
        fclose(userdata->fd);
        userdata->fd = NULL;

        char *filename = malloc(100);
        sprintf(filename, "%s_%d%s", VID_FILE_PREFIX, userdata->next_file_number, VID_FILE_SUFFIX);
		fprintf(stderr, "Opening %s for writing\n", filename);
        userdata->fd = fopen(filename, "w");
        if(!userdata->fd){
            fprintf(stderr, "Error in opening file %s\n", filename);
            free(filename); 
            exit(-1);
        }
        free(filename);
	
		// clear timer bit, then start again
		// if seg fault, then FUCK IT....or lock it
        userdata->time_up = 0;
        start_timer(userdata);

        userdata->next_file_number++;
        if(userdata->next_file_number > VID_FILE_LIMIT){
			//loop
            userdata->next_file_number = 1;
        }
    }
}

static void encoder_output_buffer_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {
    MMAL_BUFFER_HEADER_T *new_buffer;
    PORT_USERDATA *userdata = (PORT_USERDATA *) port->userdata;
    MMAL_POOL_T *pool = userdata->encoder_output_pool;
    //fprintf(stderr, "INFO:%s\n", __func__);

    
    mmal_buffer_header_mem_lock(buffer);
	renew_fd(userdata);
	fwrite(buffer->data, 1, buffer->length, userdata->fd);
	fflush(userdata->fd);
    //fwrite(buffer->data, 1, buffer->length, stdout);
    mmal_buffer_header_mem_unlock(buffer);

    mmal_buffer_header_release(buffer);
    if (port->is_enabled) {
        MMAL_STATUS_T status;

        new_buffer = mmal_queue_get(pool->queue);

        if (new_buffer) {
            status = mmal_port_send_buffer(port, new_buffer);
        }

        if (!new_buffer || status != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to return a buffer to the video port\n");
        }
    }
}

int fill_port_buffer(MMAL_PORT_T *port, MMAL_POOL_T *pool) {
    int q;
    int num = mmal_queue_length(pool->queue);

    for (q = 0; q < num; q++) {
        MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(pool->queue);
        if (!buffer) {
            fprintf(stderr, "Unable to get a required buffer %d from pool queue\n", q);
        }

        if (mmal_port_send_buffer(port, buffer) != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to send a buffer to port (%d)\n", q);
        }
    }
}

int setup_camera(PORT_USERDATA *userdata) {
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *camera = 0;
    MMAL_ES_FORMAT_T *format;
    MMAL_PORT_T * camera_preview_port;
    MMAL_PORT_T * camera_video_port;
    MMAL_PORT_T * camera_still_port;
    MMAL_POOL_T * camera_video_port_pool;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: create camera %x\n", status);
        return -1;
    }
    userdata->camera = camera;
    userdata->camera_preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
    userdata->camera_video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
    userdata->camera_still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];

    camera_preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
    camera_video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
    camera_still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];


    {
        MMAL_PARAMETER_CAMERA_CONFIG_T cam_config = {
            { MMAL_PARAMETER_CAMERA_CONFIG, sizeof (cam_config)},
            .max_stills_w = 1280,
            .max_stills_h = 720,
            .stills_yuv422 = 0,
            .one_shot_stills = 1,
            .max_preview_video_w = VIDEO_WIDTH,
            .max_preview_video_h = VIDEO_HEIGHT,
            .num_preview_video_frames = 3,
            .stills_capture_circular_buffer_height = 0,
            .fast_preview_resume = 0,
            .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
        };
        mmal_port_parameter_set(camera->control, &cam_config.hdr);
    }

    // Setup camera preview port format 
    format = camera_preview_port->format;
    format->encoding = MMAL_ENCODING_OPAQUE;
    format->encoding_variant = MMAL_ENCODING_I420;
    format->es->video.width = VIDEO_WIDTH;
    format->es->video.height = VIDEO_HEIGHT;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = VIDEO_WIDTH;
    format->es->video.crop.height = VIDEO_HEIGHT;

    status = mmal_port_format_commit(camera_preview_port);

    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: camera viewfinder format couldn't be set\n");
        return -1;
    }

    // Setup camera video port format
    mmal_format_copy(camera_video_port->format, camera_preview_port->format);

    format = camera_video_port->format;
    format->encoding = MMAL_ENCODING_I420;
    format->encoding_variant = MMAL_ENCODING_I420;
    format->es->video.width = VIDEO_WIDTH;
    format->es->video.height = VIDEO_HEIGHT;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = VIDEO_WIDTH;
    format->es->video.crop.height = VIDEO_HEIGHT;
    format->es->video.frame_rate.num = VIDEO_FPS;
    format->es->video.frame_rate.den = 1;

    camera_video_port->buffer_size = format->es->video.width * format->es->video.height * 12 / 8;
    camera_video_port->buffer_num = 2;

    fprintf(stderr, "INFO:camera video buffer_size = %d\n", camera_video_port->buffer_size);
    fprintf(stderr, "INFO:camera video buffer_num = %d\n", camera_video_port->buffer_num);

    status = mmal_port_format_commit(camera_video_port);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to commit camera video port format (%u)\n", status);
        return -1;
    }

    camera_video_port_pool = (MMAL_POOL_T *) mmal_port_pool_create(camera_video_port, camera_video_port->buffer_num, camera_video_port->buffer_size);
    userdata->camera_video_port_pool = camera_video_port_pool;
    camera_video_port->userdata = (struct MMAL_PORT_USERDATA_T *) userdata;


    status = mmal_port_enable(camera_video_port, camera_video_buffer_callback);

    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable camera video port (%u)\n", status);
        return -1;
    }

    status = mmal_component_enable(camera);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable camera (%u)\n", status);
        return -1;
    }


    fill_port_buffer(userdata->camera_video_port, userdata->camera_video_port_pool);

    if (mmal_port_parameter_set_boolean(camera_video_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS) {
        printf("%s: Failed to start capture\n", __func__);
    }

    fprintf(stderr, "INFO: camera created\n");
    return 0;
}

int setup_encoder(PORT_USERDATA *userdata) {
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *encoder = 0;
    MMAL_PORT_T *preview_input_port = NULL;

    MMAL_PORT_T *encoder_input_port = NULL, *encoder_output_port = NULL;
    MMAL_POOL_T *encoder_input_port_pool;
    MMAL_POOL_T *encoder_output_port_pool;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to create preview (%u)\n", status);
        return -1;
    }

    encoder_input_port = encoder->input[0];
    encoder_output_port = encoder->output[0];
    userdata->encoder_input_port = encoder_input_port;
    userdata->encoder_output_port = encoder_output_port;

    mmal_format_copy(encoder_input_port->format, userdata->camera_video_port->format);
    encoder_input_port->buffer_size = encoder_input_port->buffer_size_recommended;
    encoder_input_port->buffer_num = 2;


    mmal_format_copy(encoder_output_port->format, encoder_input_port->format);

    encoder_output_port->buffer_size = encoder_output_port->buffer_size_recommended;
    encoder_output_port->buffer_num = 2;
    // Commit the port changes to the input port 
    status = mmal_port_format_commit(encoder_input_port);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to commit encoder input port format (%u)\n", status);
        return -1;
    }

    // Only supporting H264 at the moment
    encoder_output_port->format->encoding = MMAL_ENCODING_H264;
    encoder_output_port->format->bitrate = 2000000;

    encoder_output_port->buffer_size = encoder_output_port->buffer_size_recommended;

    if (encoder_output_port->buffer_size < encoder_output_port->buffer_size_min) {
        encoder_output_port->buffer_size = encoder_output_port->buffer_size_min;
    }

    encoder_output_port->buffer_num = encoder_output_port->buffer_num_recommended;

    if (encoder_output_port->buffer_num < encoder_output_port->buffer_num_min) {
        encoder_output_port->buffer_num = encoder_output_port->buffer_num_min;
    }


    // Commit the port changes to the output port    
    status = mmal_port_format_commit(encoder_output_port);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to commit encoder output port format (%u)\n", status);
        return -1;
    }

    fprintf(stderr, " encoder input buffer_size = %d\n", encoder_input_port->buffer_size);
    fprintf(stderr, " encoder input buffer_num = %d\n", encoder_input_port->buffer_num);

    fprintf(stderr, " encoder output buffer_size = %d\n", encoder_output_port->buffer_size);
    fprintf(stderr, " encoder output buffer_num = %d\n", encoder_output_port->buffer_num);

    encoder_input_port_pool = (MMAL_POOL_T *) mmal_port_pool_create(encoder_input_port, encoder_input_port->buffer_num, encoder_input_port->buffer_size);
    userdata->encoder_input_pool = encoder_input_port_pool;
    encoder_input_port->userdata = (struct MMAL_PORT_USERDATA_T *) userdata;
    status = mmal_port_enable(encoder_input_port, encoder_input_buffer_callback);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable encoder input port (%u)\n", status);
        return -1;
    }
    fprintf(stderr, "INFO:Encoder input pool has been created\n");


    encoder_output_port_pool = (MMAL_POOL_T *) mmal_port_pool_create(encoder_output_port, encoder_output_port->buffer_num, encoder_output_port->buffer_size);
    userdata->encoder_output_pool = encoder_output_port_pool;
    encoder_output_port->userdata = (struct MMAL_PORT_USERDATA_T *) userdata;

    status = mmal_port_enable(encoder_output_port, encoder_output_buffer_callback);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable encoder output port (%u)\n", status);
        return -1;
    }
    fprintf(stderr, "INFO:Encoder output pool has been created\n");    

    fill_port_buffer(encoder_output_port, encoder_output_port_pool);

    fprintf(stderr, "INFO:Encoder has been created\n");
    return 0;
}

int setup_preview(PORT_USERDATA *userdata) {
    MMAL_STATUS_T status;
    MMAL_COMPONENT_T *preview = 0;
    MMAL_CONNECTION_T *camera_preview_connection = 0;
    MMAL_PORT_T *preview_input_port;

    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &preview);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to create preview (%u)\n", status);
        return -1;
    }
    userdata->preview = preview;
    preview_input_port = preview->input[0];

    {
        MMAL_DISPLAYREGION_T param;
        param.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
        param.hdr.size = sizeof (MMAL_DISPLAYREGION_T);
        param.set = MMAL_DISPLAY_SET_LAYER;
        param.layer = 0;
        param.set |= MMAL_DISPLAY_SET_FULLSCREEN;
        param.fullscreen = 1;
        status = mmal_port_parameter_set(preview_input_port, &param.hdr);
        if (status != MMAL_SUCCESS && status != MMAL_ENOSYS) {
            fprintf(stderr, "Error: unable to set preview port parameters (%u)\n", status);
            return -1;
        }
    }


    status = mmal_connection_create(&camera_preview_connection, userdata->camera_preview_port, preview_input_port, MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to create connection (%u)\n", status);
        return -1;
    }

    status = mmal_connection_enable(camera_preview_connection);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error: unable to enable connection (%u)\n", status);
        return -1;
    }
    fprintf(stderr, "INFO: preview created\n");
    return 0;
}

int setup_video_overlay(PORT_USERDATA *userdata) {
    return 0;
}

int get_next_file_number(){
    char *filename = calloc(100, sizeof(char));
    int i;
    for(i = 1; i < VID_FILE_LIMIT + 1; i++){
        sprintf(filename, "%s_%d%s", VID_FILE_PREFIX, i,VID_FILE_SUFFIX);
        if(access(filename, F_OK)){
            //if file dne 
            free(filename);
            return i;
        }
    }
    free(filename);
    return 1;
}




int main(int argc, char** argv) {

    PORT_USERDATA userdata;
    MMAL_STATUS_T status;


    cairo_surface_t *surface,*surface2;
    cairo_t *context,*context2;

    memset(&userdata, 0, sizeof (PORT_USERDATA));

    userdata.width = VIDEO_WIDTH;
    userdata.height = VIDEO_HEIGHT;
    userdata.fps = 0.0;
    userdata.fd = NULL;
    userdata.next_file_number = get_next_file_number();
    userdata.timer_thread = 0;

    fprintf(stderr, "VIDEO_WIDTH : %i\n", userdata.width );
    fprintf(stderr, "VIDEO_HEIGHT: %i\n", userdata.height );
    fprintf(stderr, "VIDEO_FPS   : %i\n",  VIDEO_FPS);
    fprintf(stderr, "Running...\n");

    bcm_host_init();

	fprintf(stderr, "System free space: %d\n", get_perc_free_int("/home"));

    char *filename = calloc(100, sizeof(char));
    sprintf(filename, "%s_%d%s", VID_FILE_PREFIX, userdata.next_file_number, VID_FILE_SUFFIX);
	userdata.next_file_number++;
	fprintf(stderr, "Opening file %s for writeing\n", filename);
	userdata.fd = fopen(filename, "w");

	if(!userdata.fd){
		fprintf(stderr, "ERROR: Unable to open %s for write\n", filename);
        free(filename);
		exit(-1);
	}
    free(filename);

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    context = cairo_create(surface);
    cairo_rectangle(context, 0.0, 0.0, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 1.0);
    cairo_fill(context);

    userdata.overlay_buffer = cairo_image_surface_get_data(surface);
    userdata.overlay = 1;

    surface2 = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    context2 = cairo_create(surface2);
    cairo_rectangle(context2, 0.0, 0.0, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    cairo_set_source_rgba(context2, 0.0, 0.0, 0.0, 1.0);
    cairo_fill(context2);

    userdata.overlay_buffer2 = cairo_image_surface_get_data(surface2);



    if (1 && setup_camera(&userdata) != 0) {
        fprintf(stderr, "Error: setup camera %x\n", status);
        return -1;
    }


    if (1 && setup_encoder(&userdata) != 0) {
        fprintf(stderr, "Error: setup encoder %x\n", status);
        return -1;
    }




    if (1 && setup_preview(&userdata) != 0) {
        fprintf(stderr, "Error: setup preview %x\n", status);
        return -1;
    }


    char text[256];
	
	overlay_info_t info;
	info.err = malloc(sizeof(char) * 10);
	pthread_mutex_init(&(info.overlay_info_lock), NULL); 
	if(!(info.err)) {
		exit (EXIT_FAILURE);
	}
	info.speed 		= 40;
	info.rpm   		= 2000;
	strcpy(info.err, "NOERROR");
	info.free_space = -1;

	pthread_t thread_collect; 
	pthread_create(&thread_collect, NULL, collect_vehicle_data, (void *) &info);

	// a dummy thread to get timer_thread a valid thread_t 
	pthread_create(&(userdata.timer_thread), NULL, dummy_thread, NULL);

    //fake Speed and GPS data
    float speed = -1.0;
    int rpm = -1;
	int free_space = -1;
	char *err = malloc(sizeof(char) * 10);
	double sys_tmp = -1.0;
	

    start_timer(&userdata);

    while (1) {
        //Update Draw to unused buffer that way there is no flickering of the overlay text if the overlay update rate
        //and video FPS are not the same
		pthread_mutex_lock(&(info.overlay_info_lock));
		speed 	   = info.speed;
		rpm   	   = info.rpm;
		free_space = info.free_space;
		sys_tmp    = info.tmp;
		// use strcpy to get err from info
		strcpy(err, info.err);
		
		pthread_mutex_unlock(&(info.overlay_info_lock));

		time_t rawtime;
    	struct tm * timeinfo;

    	time ( &rawtime );
    	timeinfo = localtime ( &rawtime );
    	char *cur_time = asctime(timeinfo);
	    cur_time[strlen(cur_time) - 1] = '\0';

	
        if (userdata.overlay == 1) { 
            cairo_rectangle(context, 0.0, 0.0, OVERLAY_WIDTH, OVERLAY_HEIGHT);
            cairo_set_source_rgba(context, 0.0, 0.0, 0.0, 1.0);
            cairo_fill(context);
            cairo_move_to(context, 0.0, 0.0);
            cairo_set_source_rgba(context, 1.0, 1.0, 1.0, 1.0);        
            cairo_move_to(context, 0.0, 30.0);
            cairo_set_font_size(context, 30);
            sprintf(text, "[%s] RPM: %d Speed: %.1fkm/h Tmp: %d'C %s",cur_time, rpm, speed, (int) sys_tmp, err);
            cairo_show_text(context, text);
            userdata.overlay = 0;
        }
        else {
            cairo_rectangle(context2, 0.0, 0.0, OVERLAY_WIDTH, OVERLAY_HEIGHT);
            cairo_set_source_rgba(context2, 0.0, 0.0, 0.0, 1.0);
            cairo_fill(context2);
            cairo_move_to(context2, 0.0, 0.0);
            cairo_set_source_rgba(context2, 1.0, 1.0, 1.0, 1.0);        
            cairo_move_to(context2, 0.0, 30.0);
            cairo_set_font_size(context2, 30);
            sprintf(text, "[%s] RPM: %d Speed: %.1fkm/h Tmp: %d'C %s",cur_time, rpm, speed, (int) sys_tmp, err);
            //sprintf(text, "%.2fFPS GPS: 0.00000, 0.00000 Speed 0km/h b1", userdata.fps);
            cairo_show_text(context2, text);
            userdata.overlay = 1;
        }


        usleep(OVERLAY_REFRESH_RT);
    }

    return 0;
}

