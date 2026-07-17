#ifndef MYKERNEL_MKDX_RENDER3D_H
#define MYKERNEL_MKDX_RENDER3D_H

#include <kernel/types.h>

#define MKDX_MAX_BUFFERS   16
#define MKDX_MAX_PIPELINES 8

typedef enum {
    MKDX_BUF_VERTEX = 1,
    MKDX_BUF_INDEX  = 2
} mkdx_buffer_kind;

typedef struct mkdx_buffer {
    int      used;
    int      id;
    mkdx_buffer_kind kind;
    void    *data;
    uint32_t size;
} mkdx_buffer;

typedef struct mkdx_pipeline {
    int      used;
    int      id;
    uint32_t flags;
} mkdx_pipeline;

typedef struct mkdx_gpu {
    mkdx_buffer   buffers[MKDX_MAX_BUFFERS];
    mkdx_pipeline pipelines[MKDX_MAX_PIPELINES];
    int           next_buf_id;
    int           next_pipe_id;
    int           ready;
} mkdx_gpu;

int  mkdx_gpu_init(mkdx_gpu *gpu);
void mkdx_gpu_shutdown(mkdx_gpu *gpu);

int  mkdx_buffer_create(mkdx_gpu *gpu, mkdx_buffer_kind kind, uint32_t size, const void *data);
int  mkdx_buffer_destroy(mkdx_gpu *gpu, int id);
int  mkdx_pipeline_create(mkdx_gpu *gpu, uint32_t flags);
int  mkdx_draw_indexed(mkdx_gpu *gpu, int pipeline, int vbo, int ibo,
                       uint32_t index_count, uint32_t index_offset);
int  mkdx_submit(mkdx_gpu *gpu);

#endif
