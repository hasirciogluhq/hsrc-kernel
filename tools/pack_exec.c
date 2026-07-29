/* Host tool: wrap a flat i386 image into a .exec (userspace executable) container. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define EXEC_MAGIC      0x43455845u /* 'EXEC' */
#define EXEC_VERSION    2
#define EXEC_NAME_MAX   32
#define EXEC_NEEDED_MAX 4
#define DYNLIB_NAME_MAX   32

typedef struct exec_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t load_addr;
    uint32_t entry_off;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t stack_size;
    char     name[EXEC_NAME_MAX];
    uint32_t imports_off;
    char     needed[EXEC_NEEDED_MAX][DYNLIB_NAME_MAX];
} __attribute__((packed)) exec_header_t;

static int parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;

    if (!s || !out)
        return -1;
    v = strtoul(s, &end, 0);
    if (!end || *end != '\0')
        return -1;
    *out = (uint32_t)v;
    return 0;
}

int main(int argc, char **argv)
{
    exec_header_t hdr;
    FILE *in, *out;
    uint8_t *img;
    long file_sz;
    uint32_t load_addr, entry_off, image_size, bss_size, stack_size, imports_off;
    int i, needed_i;

    if (argc < 8) {
        fprintf(stderr,
                "usage: %s <out.exec> <image.bin> <load_addr> <entry_off> "
                "<image_size> <bss_size> <name> [stack_size] [imports_off] "
                "[needed.dynlib...]\n",
                argv[0]);
        return 1;
    }

    if (parse_u32(argv[3], &load_addr) < 0 ||
        parse_u32(argv[4], &entry_off) < 0 ||
        parse_u32(argv[5], &image_size) < 0 ||
        parse_u32(argv[6], &bss_size) < 0) {
        fprintf(stderr, "bad numeric argument\n");
        return 1;
    }

    stack_size = 8192;
    imports_off = 0;
    needed_i = 0;
    if (argc >= 9 && parse_u32(argv[8], &stack_size) < 0) {
        fprintf(stderr, "bad stack_size\n");
        return 1;
    }
    if (argc >= 10 && parse_u32(argv[9], &imports_off) < 0) {
        fprintf(stderr, "bad imports_off\n");
        return 1;
    }

    if (image_size == 0) {
        fprintf(stderr, "image_size must be non-zero\n");
        return 1;
    }

    in = fopen(argv[2], "rb");
    if (!in) {
        perror(argv[2]);
        return 1;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        perror("seek");
        fclose(in);
        return 1;
    }
    file_sz = ftell(in);
    if (file_sz < 0 || (uint32_t)file_sz < image_size) {
        fprintf(stderr, "image.bin too small for image_size\n");
        fclose(in);
        return 1;
    }
    if (fseek(in, 0, SEEK_SET) != 0) {
        perror("seek");
        fclose(in);
        return 1;
    }

    img = (uint8_t *)malloc(image_size);
    if (!img || fread(img, 1, image_size, in) != image_size) {
        fprintf(stderr, "read failed: %s\n", argv[2]);
        free(img);
        fclose(in);
        return 1;
    }
    fclose(in);

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = EXEC_MAGIC;
    hdr.version = EXEC_VERSION;
    hdr.header_size = (uint32_t)sizeof(hdr);
    hdr.load_addr = load_addr;
    hdr.entry_off = entry_off;
    hdr.image_size = image_size;
    hdr.bss_size = bss_size;
    hdr.stack_size = stack_size;
    hdr.imports_off = imports_off;
    strncpy(hdr.name, argv[7], EXEC_NAME_MAX - 1);

    for (i = 10; i < argc && needed_i < EXEC_NEEDED_MAX; i++) {
        strncpy(hdr.needed[needed_i], argv[i], DYNLIB_NAME_MAX - 1);
        needed_i++;
    }

    if (hdr.entry_off >= hdr.image_size + hdr.bss_size) {
        fprintf(stderr, "entry_off out of range\n");
        free(img);
        return 1;
    }
    if (hdr.imports_off != 0 &&
        hdr.imports_off >= hdr.image_size + hdr.bss_size) {
        fprintf(stderr, "imports_off out of range\n");
        free(img);
        return 1;
    }

    out = fopen(argv[1], "wb");
    if (!out) {
        perror(argv[1]);
        free(img);
        return 1;
    }
    if (fwrite(&hdr, 1, sizeof(hdr), out) != sizeof(hdr) ||
        fwrite(img, 1, image_size, out) != image_size) {
        fprintf(stderr, "write failed\n");
        fclose(out);
        free(img);
        return 1;
    }
    fclose(out);
    free(img);
    return 0;
}
