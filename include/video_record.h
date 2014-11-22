#ifndef __VIDEO_RECORD_H
#define __VIDEO_RECORD_H

typedef struct overlay_info{
    pthread_mutex_t overlay_info_lock;
    float speed;
    int rpm;
    char *err;
    int free_space;
    double tmp;
} overlay_info_t;

#endif
