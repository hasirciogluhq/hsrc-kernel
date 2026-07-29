/* Host tool: add files (with VFAT LFN) into an existing FAT16 image.
 * diskname may contain directories: system/bin/foo.mke creates parents. */
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
        char num[4];
        size_t keep;
        snprintf(num, sizeof(num), "%d", uniq < 1 ? 1 : uniq);
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
    return rd16(fat + cl * 2u);
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

static uint32_t cluster_bytes(fat_img_t *f)
{
    return (uint32_t)f->spc * f->bps;
}

static uint8_t *cluster_ptr(fat_img_t *f, uint32_t cl)
{
    return f->img + (f->data_lba + (cl - 2) * f->spc) * f->bps;
}

/* dir_clust==0 → FAT16 root. */
static int dir_entry_count(fat_img_t *f, uint32_t dir_clust)
{
    if (dir_clust == 0)
        return (int)f->root_ents;
    return (int)(cluster_bytes(f) / 32u); /* single-cluster dirs for now */
}

static uint8_t *dir_entry_ptr(fat_img_t *f, uint32_t dir_clust, uint32_t index)
{
    if (dir_clust == 0)
        return f->img + f->root_lba * f->bps + index * 32u;
    return cluster_ptr(f, dir_clust) + index * 32u;
}

static int dir_find_free_slots(fat_img_t *f, uint32_t dir_clust, int need, uint32_t *out_index)
{
    int max = dir_entry_count(f, dir_clust);
    int i, run = 0;
    uint32_t run_start = 0;

    for (i = 0; i < max; i++) {
        uint8_t *e = dir_entry_ptr(f, dir_clust, (uint32_t)i);
        if (e[0] == 0x00 || e[0] == 0xE5) {
            if (run == 0)
                run_start = (uint32_t)i;
            run++;
            if (run >= need) {
                *out_index = run_start;
                return 0;
            }
        } else {
            run = 0;
        }
    }
    return -1;
}

static int dir_name_exists(fat_img_t *f, uint32_t dir_clust, const char *name,
                           uint32_t *out_clust, int *out_is_dir)
{
    int max = dir_entry_count(f, dir_clust);
    int i;
    char lfn[260];
    int lfn_len = 0;

    lfn[0] = 0;
    for (i = 0; i < max; i++) {
        uint8_t *e = dir_entry_ptr(f, dir_clust, (uint32_t)i);
        if (e[0] == 0x00)
            break;
        if (e[0] == 0xE5)
            continue;
        if ((e[11] & 0x0F) == 0x0F) {
            /* Rebuild LFN roughly from entries (seq ascending as we scan) */
            int seq = e[0] & 0x1F;
            int start = (seq - 1) * 13;
            static const int pos[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            int j;
            if (e[0] & 0x40)
                lfn_len = 0;
            for (j = 0; j < 13; j++) {
                uint16_t ch = (uint16_t)(e[pos[j]] | (e[pos[j] + 1] << 8));
                if (ch == 0 || ch == 0xFFFF)
                    break;
                if (start + j < (int)sizeof(lfn) - 1) {
                    lfn[start + j] = (char)(ch & 0xff);
                    if (start + j + 1 > lfn_len)
                        lfn_len = start + j + 1;
                }
            }
            lfn[lfn_len] = 0;
            continue;
        }
        if (lfn[0] && strcmp(lfn, name) == 0) {
            uint32_t cl = rd16(e + 26) | ((uint32_t)rd16(e + 20) << 16);
            if (out_clust)
                *out_clust = cl;
            if (out_is_dir)
                *out_is_dir = (e[11] & 0x10) != 0;
            return 1;
        }
        /* short-name fallback */
        {
            char shortn[13];
            int p = 0, k;
            for (k = 0; k < 8 && e[k] != ' '; k++)
                shortn[p++] = (char)tolower(e[k]);
            if (e[8] != ' ') {
                shortn[p++] = '.';
                for (k = 8; k < 11 && e[k] != ' '; k++)
                    shortn[p++] = (char)tolower(e[k]);
            }
            shortn[p] = 0;
            if (strcmp(shortn, name) == 0) {
                uint32_t cl = rd16(e + 26) | ((uint32_t)rd16(e + 20) << 16);
                if (out_clust)
                    *out_clust = cl;
                if (out_is_dir)
                    *out_is_dir = (e[11] & 0x10) != 0;
                return 1;
            }
        }
        lfn[0] = 0;
        lfn_len = 0;
    }
    return 0;
}

static int write_dir_entry(fat_img_t *f, uint32_t dir_clust, const char *name,
                           uint32_t first_clust, uint32_t size, int is_dir)
{
    uint8_t name83[11];
    uint8_t chk;
    int nlen = (int)strlen(name);
    int n_lfn = (nlen + 12) / 13;
    int need;
    uint32_t slot;
    int seq, i;

    if (n_lfn < 1)
        n_lfn = 1;
    need = n_lfn + 1;
    if (dir_find_free_slots(f, dir_clust, need, &slot) < 0) {
        fprintf(stderr, "directory full for %s\n", name);
        return -1;
    }

    make_short_name(name, name83, 1);
    for (i = 2; i < 10; i++) {
        uint32_t e;
        int clash = 0;
        int max = dir_entry_count(f, dir_clust);
        make_short_name(name, name83, i);
        for (e = 0; e < (uint32_t)max; e++) {
            uint8_t *ent = dir_entry_ptr(f, dir_clust, e);
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

    for (seq = n_lfn; seq >= 1; seq--) {
        uint8_t *ent = dir_entry_ptr(f, dir_clust, slot + (uint32_t)(n_lfn - seq));
        memset(ent, 0, 32);
        ent[0] = (uint8_t)seq;
        if (seq == n_lfn)
            ent[0] |= 0x40;
        ent[11] = 0x0F;
        ent[13] = chk;
        put_lfn_chars(ent, name, (seq - 1) * 13);
    }
    {
        uint8_t *ent = dir_entry_ptr(f, dir_clust, slot + (uint32_t)n_lfn);
        memset(ent, 0, 32);
        memcpy(ent, name83, 11);
        ent[11] = is_dir ? 0x10 : 0x20;
        wr16(ent + 26, (uint16_t)(first_clust & 0xffff));
        wr16(ent + 20, (uint16_t)(first_clust >> 16));
        wr32(ent + 28, size);
    }
    return 0;
}

static int mkdir_fat(fat_img_t *f, uint32_t parent, const char *name, uint32_t *out_clust)
{
    uint32_t cl;
    uint8_t *p;
    uint32_t existing = 0;
    int is_dir = 0;

    if (dir_name_exists(f, parent, name, &existing, &is_dir)) {
        if (!is_dir) {
            fprintf(stderr, "not a directory: %s\n", name);
            return -1;
        }
        *out_clust = existing;
        return 0;
    }

    cl = fat_alloc_cluster(f);
    if (!cl)
        return -1;
    p = cluster_ptr(f, cl);
    memset(p, 0, cluster_bytes(f));

    /* . */
    memset(p, ' ', 11);
    p[0] = '.';
    p[11] = 0x10;
    wr16(p + 26, (uint16_t)(cl & 0xffff));
    wr16(p + 20, (uint16_t)(cl >> 16));

    /* .. */
    memset(p + 32, ' ', 11);
    p[32] = '.';
    p[33] = '.';
    p[32 + 11] = 0x10;
    wr16(p + 32 + 26, (uint16_t)(parent & 0xffff));
    wr16(p + 32 + 20, (uint16_t)(parent >> 16));

    if (write_dir_entry(f, parent, name, cl, 0, 1) < 0)
        return -1;
    *out_clust = cl;
    return 0;
}

static int ensure_dir_path(fat_img_t *f, const char *dirpath, uint32_t *out_clust)
{
    char tmp[512];
    char *tok;
    char *save;
    uint32_t cur = 0;

    if (!dirpath || !dirpath[0] || strcmp(dirpath, ".") == 0) {
        *out_clust = 0;
        return 0;
    }
    if (strlen(dirpath) >= sizeof(tmp))
        return -1;
    strcpy(tmp, dirpath);
    for (tok = strtok_r(tmp, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (!tok[0] || strcmp(tok, ".") == 0)
            continue;
        if (mkdir_fat(f, cur, tok, &cur) < 0)
            return -1;
    }
    *out_clust = cur;
    return 0;
}

static int add_file_in_dir(fat_img_t *f, uint32_t dir_clust, const char *basename,
                           const uint8_t *data, long sz)
{
    uint32_t first = 0, prev = 0;
    long left = sz;
    long off = 0;

    while (left > 0 || (sz == 0 && first == 0)) {
        uint32_t cl = fat_alloc_cluster(f);
        uint32_t cbytes = cluster_bytes(f);
        uint32_t chunk;
        if (!cl) {
            fprintf(stderr, "no free clusters for %s\n", basename);
            return -1;
        }
        if (!first)
            first = cl;
        else
            fat_set(f, prev, (uint16_t)cl);
        fat_set(f, cl, 0xFFF8);
        chunk = (uint32_t)((left > (long)cbytes) ? cbytes : (left > 0 ? (uint32_t)left : 0));
        memset(cluster_ptr(f, cl), 0, cbytes);
        if (chunk)
            memcpy(cluster_ptr(f, cl), data + off, chunk);
        off += (long)chunk;
        left -= (long)chunk;
        prev = cl;
        if (sz == 0)
            break;
    }

    if (write_dir_entry(f, dir_clust, basename, first, (uint32_t)sz, 0) < 0)
        return -1;
    return 0;
}

static int add_file(fat_img_t *f, const char *host_path, const char *disk_path)
{
    FILE *fp;
    long sz;
    uint8_t *data;
    char pathbuf[512];
    char *slash;
    const char *basename;
    char dirpath[512];
    uint32_t dir_clust = 0;

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

    if (strlen(disk_path) >= sizeof(pathbuf)) {
        free(data);
        return -1;
    }
    strcpy(pathbuf, disk_path);
    slash = strrchr(pathbuf, '/');
    if (slash) {
        *slash = 0;
        basename = slash + 1;
        strcpy(dirpath, pathbuf);
        if (ensure_dir_path(f, dirpath, &dir_clust) < 0) {
            fprintf(stderr, "mkdir failed for %s\n", dirpath);
            free(data);
            return -1;
        }
    } else {
        basename = pathbuf;
    }

    if (!basename[0]) {
        free(data);
        return -1;
    }
    if (add_file_in_dir(f, dir_clust, basename, data, sz) < 0) {
        free(data);
        return -1;
    }
    free(data);
    printf("  + %s (%ld bytes)\n", disk_path, sz);
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
        fprintf(stderr, "usage: %s <disk.img> <hostfile:disk/path>...\n", argv[0]);
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
            fprintf(stderr, "bad spec (want host:disk/path): %s\n", argv[i]);
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
