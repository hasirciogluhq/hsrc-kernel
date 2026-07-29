#ifndef DX_RENDER3D_H
#define DX_RENDER3D_H

#include <kernel/types.h>

#define DX_MAX_BUFFERS   16
#define DX_MAX_PIPELINES 8

typedef enum {
    DX_BUF_VERTEX = 1,
    DX_BUF_INDEX  = 2
} dx_buffer_kind;

typedef struct dx_buffer {
    int      used;
    int      id;
    dx_buffer_kind kind;
    void    *data;
    uint32_t size;
} dx_buffer;

typedef struct dx_pipeline {
    int      used;
    int      id;
    uint32_t flags;
} dx_pipeline;

typedef struct dx_gpu {
    dx_buffer   buffers[DX_MAX_BUFFERS];
    dx_pipeline pipelines[DX_MAX_PIPELINES];
    int           next_buf_id;
    int           next_pipe_id;
    int           ready;
} dx_gpu;

int  dx_gpu_init(dx_gpu *gpu);
void dx_gpu_shutdown(dx_gpu *gpu);

int  dx_buffer_create(dx_gpu *gpu, dx_buffer_kind kind, uint32_t size, const void *data);
int  dx_buffer_destroy(dx_gpu *gpu, int id);
int  dx_pipeline_create(dx_gpu *gpu, uint32_t flags);
int  dx_draw_indexed(dx_gpu *gpu, int pipeline, int vbo, int ibo,
                       uint32_t index_count, uint32_t index_offset);
int  dx_submit(dx_gpu *gpu);

#endif
