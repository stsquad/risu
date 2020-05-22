/******************************************************************************
 * Copyright (c) 2010 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 *****************************************************************************/


/* Random Instruction Sequences for Userspace */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <ucontext.h>
#include <setjmp.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#include "config.h"
#include "risu.h"

void *memblock;

static int comm_fd;
static bool trace;
static size_t signal_count;

#ifdef HAVE_ZLIB
#include <zlib.h>
static gzFile gz_trace_file;
#define TRACE_TYPE "compressed"
#else
#define TRACE_TYPE "uncompressed"
#endif

static sigjmp_buf jmpbuf;

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

/* I/O functions */

RisuResult read_buffer(void *ptr, size_t bytes)
{
    size_t res;

    if (!trace) {
        return recv_data_pkt(comm_fd, ptr, bytes);
    }

#ifdef HAVE_ZLIB
    if (comm_fd == STDIN_FILENO) {
#endif
        res = read(comm_fd, ptr, bytes);
#ifdef HAVE_ZLIB
    } else {
        res = gzread(gz_trace_file, ptr, bytes);
    }
#endif

    return res == bytes ? RES_OK : RES_BAD_IO;
}

RisuResult write_buffer(void *ptr, size_t bytes)
{
    size_t res;

    if (!trace) {
        return send_data_pkt(comm_fd, ptr, bytes);
    }

#ifdef HAVE_ZLIB
    if (comm_fd == STDOUT_FILENO) {
#endif
        res = write(comm_fd, ptr, bytes);
#ifdef HAVE_ZLIB
    } else {
        res = gzwrite(gz_trace_file, ptr, bytes);
    }
#endif

    return res == bytes ? RES_OK : RES_BAD_IO;
}

void respond(RisuResult r)
{
    if (!trace) {
        send_response_byte(comm_fd, r);
    }
}

static void master_sigill(int sig, siginfo_t *si, void *uc)
{
    RisuResult r;
    signal_count++;

    r = send_register_info(uc);
    if (r == RES_OK) {
        advance_pc(uc);
    } else {
        siglongjmp(jmpbuf, r);
    }
}

static void apprentice_sigill(int sig, siginfo_t *si, void *uc)
{
    RisuResult r;
    signal_count++;

    r = recv_and_compare_register_info(uc);
    if (r == RES_OK) {
        advance_pc(uc);
    } else {
        siglongjmp(jmpbuf, r);
    }
}

static void set_sigill_handler(void (*fn) (int, siginfo_t *, void *))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));

    sa.sa_sigaction = fn;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGILL, &sa, 0) != 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

typedef void entrypoint_fn(void);

uintptr_t image_start_address;
static entrypoint_fn *image_start;

static void load_image(const char *imgfile)
{
    /* Load image file into memory as executable */
    struct stat st;
    fprintf(stderr, "loading test image %s...\n", imgfile);
    int fd = open(imgfile, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "failed to open image file %s\n", imgfile);
        exit(EXIT_FAILURE);
    }
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }
    size_t len = st.st_size;
    void *addr;

    /* Map writable because we include the memory area for store
     * testing in the image.
     */
    addr =
        mmap(0, len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd,
             0);
    if (!addr) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    close(fd);
    image_start = addr;
    image_start_address = (uintptr_t) addr;
}

static int master(void)
{
    RisuResult res = sigsetjmp(jmpbuf, 1);

    switch (res) {
    case RES_OK:
        set_sigill_handler(&master_sigill);
        fprintf(stderr, "starting master image at 0x%"PRIxPTR"\n",
                image_start_address);
        fprintf(stderr, "starting image\n");
        image_start();
        fprintf(stderr, "image returned unexpectedly\n");
        return EXIT_FAILURE;

    case RES_END:
#ifdef HAVE_ZLIB
        if (trace && comm_fd != STDOUT_FILENO) {
            gzclose(gz_trace_file);
        }
#endif
        close(comm_fd);
        return EXIT_SUCCESS;

    case RES_BAD_IO:
        fprintf(stderr, "i/o error after %zd checkpoints\n", signal_count);
        return EXIT_FAILURE;

    default:
        fprintf(stderr, "unexpected result %d\n", res);
        return EXIT_FAILURE;
    }
}

static int apprentice(void)
{
    RisuResult res = sigsetjmp(jmpbuf, 1);

    switch (res) {
    case RES_OK:
        set_sigill_handler(&apprentice_sigill);
        fprintf(stderr, "starting apprentice image at 0x%"PRIxPTR"\n",
                image_start_address);
        fprintf(stderr, "starting image\n");
        image_start();
        fprintf(stderr, "image returned unexpectedly\n");
        return EXIT_FAILURE;

    case RES_END:
        return EXIT_SUCCESS;

    case RES_MISMATCH_REG:
        fprintf(stderr, "mismatch reg after %zd checkpoints\n", signal_count);
        report_mismatch_reg();
        return EXIT_FAILURE;

    case RES_MISMATCH_MEM:
        fprintf(stderr, "mismatch mem after %zd checkpoints\n", signal_count);
        return EXIT_FAILURE;

    case RES_BAD_IO:
        fprintf(stderr, "i/o error after %zd checkpoints\n", signal_count);
        return EXIT_FAILURE;

    default:
        fprintf(stderr, "unexpected result %d\n", res);
        return EXIT_FAILURE;
    }
}

static int ismaster;

static void usage(void)
{
    fprintf(stderr,
            "Usage: risu [--master] [--host <ip>] [--port <port>] <image file>"
            "\n\n");
    fprintf(stderr,
            "Run through the pattern file verifying each instruction\n");
    fprintf(stderr, "between master and apprentice risu processes.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --master          Be the master (server)\n");
    fprintf(stderr, "  -t, --trace=FILE  Record/playback " TRACE_TYPE " trace file\n");
    fprintf(stderr,
            "  -h, --host=HOST   Specify master host machine (apprentice only)"
            "\n");
    fprintf(stderr,
            "  -p, --port=PORT   Specify the port to connect to/listen on "
            "(default 9191)\n");
    if (arch_extra_help) {
        fprintf(stderr, "%s", arch_extra_help);
    }
}

static struct option * setup_options(char **short_opts)
{
    static struct option default_longopts[] = {
        {"help", no_argument, 0, '?'},
        {"master", no_argument, &ismaster, 1},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"trace", required_argument, 0, 't'},
        {0, 0, 0, 0}
    };
    struct option *lopts = &default_longopts[0];

    *short_opts = "h:p:t:";

    if (arch_long_opts) {
        const size_t osize = sizeof(struct option);
        const int default_count = ARRAY_SIZE(default_longopts) - 1;
        int arch_count;

        /* count additional opts */
        for (arch_count = 0; arch_long_opts[arch_count].name; arch_count++) {
            continue;
        }

        lopts = calloc(default_count + arch_count + 1, osize);

        /* Copy default opts + extra opts */
        memcpy(lopts, default_longopts, default_count * osize);
        memcpy(lopts + default_count, arch_long_opts, arch_count * osize);
    }

    return lopts;
}

int main(int argc, char **argv)
{
    /* some handy defaults to make testing easier */
    uint16_t port = 9191;
    char *hostname = "localhost";
    char *imgfile;
    char *trace_fn = NULL;
    struct option *longopts;
    char *shortopts;

    longopts = setup_options(&shortopts);

    for (;;) {
        int optidx = 0;
        int c = getopt_long(argc, argv, shortopts, longopts, &optidx);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 0:
            /* flag set by getopt_long, do nothing */
            break;
        case 't':
            trace_fn = optarg;
            trace = true;
            break;
        case 'h':
            hostname = optarg;
            break;
        case 'p':
            /* FIXME err handling */
            port = strtol(optarg, 0, 10);
            break;
        case '?':
            usage();
            return EXIT_FAILURE;
        default:
            assert(c >= FIRST_ARCH_OPT);
            process_arch_opt(c, optarg);
            break;
        }
    }

    if (trace) {
        if (strcmp(trace_fn, "-") == 0) {
            comm_fd = ismaster ? STDOUT_FILENO : STDIN_FILENO;
        } else {
            if (ismaster) {
                comm_fd = open(trace_fn, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            } else {
                comm_fd = open(trace_fn, O_RDONLY);
            }
#ifdef HAVE_ZLIB
            gz_trace_file = gzdopen(comm_fd, ismaster ? "wb9" : "rb");
#endif
        }
    } else {
        if (ismaster) {
            fprintf(stderr, "master port %d\n", port);
            comm_fd = master_connect(port);
        } else {
            fprintf(stderr, "apprentice host %s port %d\n", hostname, port);
            comm_fd = apprentice_connect(hostname, port);
        }
    }

    imgfile = argv[optind];
    if (!imgfile) {
        fprintf(stderr, "Error: must specify image file name\n\n");
        usage();
        return EXIT_FAILURE;
    }

    load_image(imgfile);

    if (ismaster) {
        return master();
    } else {
        return apprentice();
    }
}
