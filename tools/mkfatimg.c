/* Host tool: create a raw FAT16 disk image (no host mkfs required). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

int main(int argc, char **argv)
{
    const char *path;
    uint32_t size_mb;
    uint32_t total_secs;
    uint32_t reserved = 1;
    uint32_t root_ents = 512;
    uint32_t root_secs;
    uint32_t spc = 4;
    uint32_t fat_secs = 1;
    uint32_t clusters;
    uint32_t need;
    uint8_t *img;
    uint32_t img_bytes;
    uint32_t i;
    FILE *fp;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s <out.img> [size_mb]\n", argv[0]);
        return 1;
    }
    path = argv[1];
    size_mb = (argc == 3) ? (uint32_t)atoi(argv[2]) : 64u;
    if (size_mb < 4u || size_mb > 2048u) {
        fprintf(stderr, "size_mb must be 4..2048\n");
        return 1;
    }

    total_secs = size_mb * 1024u * 1024u / 512u;
    root_secs = (root_ents * 32u + 511u) / 512u;

    for (;;) {
        uint32_t data_secs;
        if (reserved + 2u * fat_secs + root_secs >= total_secs) {
            fprintf(stderr, "image too small for FAT16 layout\n");
            return 1;
        }
        data_secs = total_secs - reserved - 2u * fat_secs - root_secs;
        clusters = data_secs / spc;
        if (clusters < 4085u || clusters >= 65525u) {
            /* Keep FAT16 range: bump cluster size if needed. */
            if (clusters >= 65525u && spc < 64u) {
                spc *= 2u;
                fat_secs = 1u;
                continue;
            }
            if (clusters < 4085u && spc > 1u) {
                spc /= 2u;
                fat_secs = 1u;
                continue;
            }
        }
        need = ((clusters + 2u) * 2u + 511u) / 512u;
        if (need > fat_secs) {
            fat_secs = need;
            continue;
        }
        break;
    }

    if (clusters < 4085u || clusters >= 65525u) {
        fprintf(stderr, "cannot fit FAT16 for %u MiB\n", size_mb);
        return 1;
    }

    img_bytes = total_secs * 512u;
    img = (uint8_t *)calloc(1, img_bytes);
    if (!img) {
        perror("calloc");
        return 1;
    }

    /* Boot sector / BPB */
    img[0] = 0xEB;
    img[1] = 0x3C;
    img[2] = 0x90;
    memcpy(img + 3, "MSWIN4.1", 8);
    le16(img + 11, 512);                 /* bytes/sector */
    img[13] = (uint8_t)spc;              /* sectors/cluster */
    le16(img + 14, (uint16_t)reserved);  /* reserved */
    img[16] = 2;                         /* FATs */
    le16(img + 17, (uint16_t)root_ents);
    if (total_secs < 65536u)
        le16(img + 19, (uint16_t)total_secs);
    else
        le32(img + 32, total_secs);
    img[21] = 0xF8;                      /* media */
    le16(img + 22, (uint16_t)fat_secs);
    le16(img + 24, 32);                  /* sectors/track (dummy) */
    le16(img + 26, 64);                  /* heads (dummy) */
    img[36] = 0x80;                      /* drive number */
    img[38] = 0x29;                      /* ext boot sig */
    le32(img + 39, 0x48535243u);         /* volume serial 'HSRC' */
    memcpy(img + 43, "HSRC_DISK  ", 11);
    memcpy(img + 54, "FAT16   ", 8);
    img[510] = 0x55;
    img[511] = 0xAA;

    /* FAT0 + FAT1: media + EOC for cluster 1 */
    for (i = 0; i < 2u; i++) {
        uint8_t *fat = img + (reserved + i * fat_secs) * 512u;
        fat[0] = 0xF8;
        fat[1] = 0xFF;
        fat[2] = 0xFF;
        fat[3] = 0xFF;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        perror(path);
        free(img);
        return 1;
    }
    if (fwrite(img, 1, img_bytes, fp) != img_bytes) {
        perror("fwrite");
        fclose(fp);
        free(img);
        return 1;
    }
    fclose(fp);
    free(img);

    printf("wrote %s (%u MiB, FAT16, spc=%u, fat_secs=%u, clusters=%u)\n",
           path, size_mb, spc, fat_secs, clusters);
    return 0;
}
