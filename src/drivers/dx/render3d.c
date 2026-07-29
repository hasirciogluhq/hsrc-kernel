#include "render3d.h"
#include <drivers/display.h>
#include <kernel/heap.h>
#include <kernel/string.h>

int dx_gpu_init(dx_gpu *gpu)
{
    if (!gpu)
        return -1;
    memset(gpu, 0, sizeof(*gpu));
    gpu->next_buf_id = 1;
    gpu->next_pipe_id = 1;
    gpu->ready = 1;
    return 0;
}

void dx_gpu_shutdown(dx_gpu *gpu)
{
    int i;
    if (!gpu)
        return;
    for (i = 0; i < DX_MAX_BUFFERS; i++) {
        if (gpu->buffers[i].used && gpu->buffers[i].data)
            kfree(gpu->buffers[i].data);
    }
    memset(gpu, 0, sizeof(*gpu));
}

int dx_buffer_create(dx_gpu *gpu, dx_buffer_kind kind, uint32_t size, const void *data)
{
    int i;
    if (!gpu || !gpu->ready || size == 0)
        return -1;
    for (i = 0; i < DX_MAX_BUFFERS; i++) {
        if (!gpu->buffers[i].used) {
            void *mem = kmalloc(size);
            if (!mem)
                return -1;
            if (data)
                memcpy(mem, data, size);
            else
                memset(mem, 0, size);
            gpu->buffers[i].used = 1;
            gpu->buffers[i].id = gpu->next_buf_id++;
            gpu->buffers[i].kind = kind;
            gpu->buffers[i].data = mem;
            gpu->buffers[i].size = size;
            return gpu->buffers[i].id;
        }
    }
    return -1;
}

int dx_buffer_destroy(dx_gpu *gpu, int id)
{
    int i;
    if (!gpu)
        return -1;
    for (i = 0; i < DX_MAX_BUFFERS; i++) {
        if (gpu->buffers[i].used && gpu->buffers[i].id == id) {
            if (gpu->buffers[i].data)
                kfree(gpu->buffers[i].data);
            memset(&gpu->buffers[i], 0, sizeof(gpu->buffers[i]));
            return 0;
        }
    }
    return -1;
}

int dx_pipeline_create(dx_gpu *gpu, uint32_t flags)
{
    int i;
    if (!gpu || !gpu->ready)
        return -1;
    for (i = 0; i < DX_MAX_PIPELINES; i++) {
        if (!gpu->pipelines[i].used) {
            gpu->pipelines[i].used = 1;
            gpu->pipelines[i].id = gpu->next_pipe_id++;
            gpu->pipelines[i].flags = flags;
            return gpu->pipelines[i].id;
        }
    }
    return -1;
}

int dx_draw_indexed(dx_gpu *gpu, int pipeline, int vbo, int ibo,
                      uint32_t index_count, uint32_t index_offset)
{
    (void)gpu;
    (void)pipeline;
    (void)vbo;
    (void)ibo;
    (void)index_count;
    (void)index_offset;
    /* SW raster path reserved - record-only for now */
    return 0;
}

int dx_submit(dx_gpu *gpu)
{
    display_ops_t *ops;
    if (!gpu || !gpu->ready)
        return -1;

    ops = display_active();
    if (!ops)
        return -1;

    /* virtio + gpu_ops: submit; otherwise SW path (no-op success for BGA) */
    if (ops->gpu_submit)
        return ops->gpu_submit(NULL, 0);

    return 0;
}
