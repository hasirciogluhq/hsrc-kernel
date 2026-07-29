/* Host tool: add files (with VFAT LFN) into an existing FAT16 image from mkfatimg. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint8_t lfn_checksum(const uint8_t name83[11])
{
    uint8_t sum = 0;
    int i;
    for (i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name83[i]);
    return sum;
}

static int make_short_name(const char *long_name, uint8_t out[11], int uniq)
{
    const char *base = long_name;
    const char *slash = strrchr(long_name, '/');
    const char *dot;
    char stem[16];
    char ext[4];
    size_t i, si = 0, ei = 0;
    int tilde = uniq;

    if (slash)
        base = slash + 1;
    memset(out, ' ', 11);
    memset(stem, 0, sizeof(stem));
    memset(ext, 0, sizeof(ext));
    dot = strrchr(base, '.');
    if (dot && dot != base) {
        for (i = 0; base + i < dot && si < 8; i++) {
            char c = base[i];
            if (c == ' ' || c == '.' || c == '+' || c == ',' || c == ';' ||
                c == '=' || c == '[' || c == ']')
                continue;
            if (c == '-')
                c = '_';
            stem[si++] = (char)toupper((unsigned char)c);
        }
        for (i = 1; dot[i] && ei < 3; i++) {
            char c = dot[i];
            if (!isalnum((unsigned char)c))
                continue;
            ext[ei++] = (char)toupper((unsigned char)c);
        }
    } else {
        for (i = 0; base[i] && si < 8; i++) {
            char c = base[i];
            if (c == ' ' || c == '.')
                continue;
            if (c == '-')
                c = '_';
            if (!isalnum((unsigned char)c) && c != '_')
                continue;
            stem[si++] = (char)toupper((unsigned char)c);
        }
    }
    if (si == 0)
        stem[si++] = 'X';
    if (strlen(base) > 12 || strchr(base, '-') || (dot && (size_t)(dot - base) > 8)) {
        /* Need ~N form */
        char num[4];
        size_t keep;
        snprintf(num, sizeof(num), "%d", tilde < 1 ? 1 : tilde);
        keep = 8 - 1 - strlen(num);
        if (keep > si)
            keep = si;
        memset(out, ' ', 11);
        memcpy(out, stem, keep);
        out[keep] = '~';
        memcpy(out + keep + 1, num, strlen(num));
    } else {
        memcpy(out, stem, si);
    }
    memcpy(out + 8, ext, ei);
    return 0;
}

static void put_lfn_chars(uint8_t *ent, const char *name, int start_idx)
{
    /* Fill one LFN entry slots: 5 + 6 + 2 UTF-16LE chars from name[start_idx..] */
    static const int pos[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    int i;
    for (i = 0; i < 13; i++) {
        size_t ni = (size_t)(start_idx + i);
        uint16_t ch;
        if (name[ni] == 0 && i == 0) {
            ch = 0;
        } else if (ni > strlen(name)) {
            ch = 0xFFFF;
        } else if (name[ni] == 0) {
            ch = 0;
        } else {
            ch = (uint8_t)name[ni];
        }
        ent[pos[i]] = (uint8_t)(ch & 0xff);
        ent[pos[i] + 1] = (uint8_t)(ch >> 8);
        if (ch == 0) {
            /* pad remaining with 0xFFFF */
            int j;
            for (j = i + 1; j < 13; j++) {
                ent[pos[j]] = 0xFF;
                ent[pos[j] + 1] = 0xFF;
            }
            break;
        }
    }
}

typedef struct {
    uint8_t *img;
    size_t   img_bytes;
    uint16_t bps;
    uint8_t  spc;
    uint16_t reserved;
    uint8_t  fats;
    uint16_t root_ents;
    uint16_t fat_secs;
    uint32_t total_secs;
    uint32_t root_lba;
    uint32_t data_lba;
    uint32_t fat_lba;
    uint32_t clusters;
} fat_img_t;

static int fat_open(fat_img_t *f, uint8_t *img, size_t img_bytes)
{
    uint32_t root_secs;
    memset(f, 0, sizeof(*f));
    f->img = img;
    f->img_bytes = img_bytes;
    if (img_bytes < 512 || img[510] != 0x55 || img[511] != 0xAA)
        return -1;
    f->bps = rd16(img + 11);
    f->spc = img[13];
    f->reserved = rd16(img + 14);
    f->fats = img[16];
    f->root_ents = rd16(img + 17);
    f->fat_secs = rd16(img + 22);
    f->total_secs = rd16(img + 19);
    if (!f->total_secs)
        f->total_secs = rd32(img + 32);
    if (!f->bps || !f->spc || !f->fats || !f->fat_secs || !f->root_ents)
        return -1;
    root_secs = ((uint32_t)f->root_ents * 32u + f->bps - 1u) / f->bps;
    f->fat_lba = f->reserved;
    f->root_lba = f->reserved + (uint32_t)f->fats * f->fat_secs;
    f->data_lba = f->root_lba + root_secs;
    f->clusters = (f->total_secs - f->data_lba) / f->spc;
    return 0;
}

static uint16_t fat_get(fat_img_t *f, uint32_t cl)
{
    uint8_t *fat = f->img + f->fat_lba * f->bps;
    uint32_t off = cl * 2u;
    return rd16(fat + off);
}

static void fat_set(fat_img_t *f, uint32_t cl, uint16_t v)
{
    int i;
    for (i = 0; i < f->fats; i++) {
        uint8_t *fat = f->img + (f->fat_lba + (uint32_t)i * f->fat_secs) * f->bps;
        wr16(fat + cl * 2u, v);
    }
}

static uint32_t fat_alloc_cluster(fat_img_t *f)
{
    uint32_t cl;
    for (cl = 2; cl < f->clusters + 2; cl++) {
        if (fat_get(f, cl) == 0) {
            fat_set(f, cl, 0xFFF8);
            return cl;
        }
    }
    return 0;
}

static int root_find_free_slots(fat_img_t *f, int need, uint32_t *out_index)
{
    uint32_t root_bytes = (uint32_t)f->root_ents * 32u;
    uint8_t *root = f->img + f->root_lba * f->bps;
    uint32_t i;
    int run = 0;
    uint32_t run_start = 0;

    for (i = 0; i < f->root_ents; i++) {
        uint8_t *e = root + i * 32u;
        if (e[0] == 0x00 || e[0] == 0xE5) {
            if (run == 0)
                run_start = i;
            run++;
            if (run >= need) {
                *out_index = run_start;
                return 0;
            }
        } else {
            run = 0;
        }
    }
    (void)root_bytes;
    return -1;
}

static int add_file(fat_img_t *f, const char *host_path, const char *disk_name)
{
    FILE *fp;
    long sz;
    uint8_t *data;
    uint8_t name83[11];
    uint8_t chk;
    int nlen;
    int n_lfn;
    int need;
    uint32_t slot;
    uint8_t *root;
    uint32_t first = 0, prev = 0;
    long left;
    long off;
    int seq;
    int i;

    fp = fopen(host_path, "rb");
    if (!fp) {
        perror(host_path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    data = (uint8_t *)malloc((size_t)sz + 1);
    if (!data || (sz > 0 && fread(data, 1, (size_t)sz, fp) != (size_t)sz)) {
        fprintf(stderr, "read failed: %s\n", host_path);
        fclose(fp);
        free(data);
        return -1;
    }
    fclose(fp);

    nlen = (int)strlen(disk_name);
    n_lfn = (nlen + 12) / 13;
    if (n_lfn < 1)
        n_lfn = 1;
    need = n_lfn + 1;
    if (root_find_free_slots(f, need, &slot) < 0) {
        fprintf(stderr, "root dir full for %s\n", disk_name);
        free(data);
        return -1;
    }

    make_short_name(disk_name, name83, 1);
    /* Ensure unique short name among existing */
    for (i = 2; i < 10; i++) {
        uint32_t e;
        int clash = 0;
        make_short_name(disk_name, name83, i);
        root = f->img + f->root_lba * f->bps;
        for (e = 0; e < f->root_ents; e++) {
            uint8_t *ent = root + e * 32u;
            if (ent[0] == 0 || ent[0] == 0xE5)
                continue;
            if ((ent[11] & 0x0F) == 0x0F)
                continue;
            if (memcmp(ent, name83, 11) == 0) {
                clash = 1;
                break;
            }
        }
        if (!clash)
            break;
    }
    chk = lfn_checksum(name83);

    /* Write file clusters */
    left = sz;
    off = 0;
    while (left > 0 || (sz == 0 && first == 0)) {
        uint32_t cl = fat_alloc_cluster(f);
        uint32_t lba;
        uint32_t cbytes = (uint32_t)f->spc * f->bps;
        uint32_t chunk;
        if (!cl) {
            fprintf(stderr, "no free clusters for %s\n", disk_name);
            free(data);
            return -1;
        }
        if (!first)
            first = cl;
        else
            fat_set(f, prev, (uint16_t)cl);
        fat_set(f, cl, 0xFFF8);
        lba = f->data_lba + (cl - 2) * f->spc;
        chunk = (uint32_t)((left > (long)cbytes) ? cbytes : (left > 0 ? (uint32_t)left : 0));
        memset(f->img + lba * f->bps, 0, cbytes);
        if (chunk)
            memcpy(f->img + lba * f->bps, data + off, chunk);
        off += (long)chunk;
        left -= (long)chunk;
        prev = cl;
        if (sz == 0)
            break;
    }

    root = f->img + f->root_lba * f->bps;
    /* LFN entries: highest seq first */
    for (seq = n_lfn; seq >= 1; seq--) {
        uint8_t *ent = root + (slot + (uint32_t)(n_lfn - seq)) * 32u;
        memset(ent, 0, 32);
        ent[0] = (uint8_t)seq;
        if (seq == n_lfn)
            ent[0] |= 0x40;
        ent[11] = 0x0F;
        ent[12] = 0;
        ent[13] = chk;
        put_lfn_chars(ent, disk_name, (seq - 1) * 13);
    }
    {
        uint8_t *ent = root + (slot + (uint32_t)n_lfn) * 32u;
        memset(ent, 0, 32);
        memcpy(ent, name83, 11);
        ent[11] = 0x20;
        wr16(ent + 26, (uint16_t)(first & 0xffff));
        wr16(ent + 20, (uint16_t)(first >> 16));
        wr32(ent + 28, (uint32_t)sz);
    }

    free(data);
    printf("  + %s (%ld bytes)\n", disk_name, sz);
    return 0;
}

int main(int argc, char **argv)
{
    const char *img_path;
    FILE *fp;
    uint8_t *img;
    long img_sz;
    fat_img_t fat;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <disk.img> <hostfile:diskname>...\n", argv[0]);
        return 1;
    }
    img_path = argv[1];
    fp = fopen(img_path, "rb+");
    if (!fp) {
        perror(img_path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    img_sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    img = (uint8_t *)malloc((size_t)img_sz);
    if (!img || fread(img, 1, (size_t)img_sz, fp) != (size_t)img_sz) {
        fprintf(stderr, "failed to read %s\n", img_path);
        return 1;
    }
    if (fat_open(&fat, img, (size_t)img_sz) < 0) {
        fprintf(stderr, "not a FAT16 image: %s\n", img_path);
        return 1;
    }
    for (i = 2; i < argc; i++) {
        char *arg = strdup(argv[i]);
        char *colon = strrchr(arg, ':');
        const char *host, *disk;
        if (!colon || colon == arg) {
            fprintf(stderr, "bad spec (want host:diskname): %s\n", argv[i]);
            return 1;
        }
        *colon = 0;
        host = arg;
        disk = colon + 1;
        if (add_file(&fat, host, disk) < 0)
            return 1;
        free(arg);
    }
    fseek(fp, 0, SEEK_SET);
    if (fwrite(img, 1, (size_t)img_sz, fp) != (size_t)img_sz) {
        perror("fwrite");
        return 1;
    }
    fclose(fp);
    free(img);
    printf("updated %s\n", img_path);
    return 0;
}
