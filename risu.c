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
#include <getopt.h>
#include <setjmp.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#include "risu.h"

void *memblock = 0;

int apprentice_socket, master_socket;
int trace_file = 0;

sigjmp_buf jmpbuf;

/* Should we test for FP exception status bits? */
int test_fp_exc = 0;

long executed_tests = 0;
void report_test_status(void *pc)
{
   if (ismaster || trace_file) {
      executed_tests += 1;
      if (executed_tests % 100 == 0) {
         fprintf(stderr,"Executed %ld test instructions (pc=%p)\r",
                 executed_tests, pc);
      }
   }
}

/* Master functions */

int read_sock(void *ptr, size_t bytes)
{
   return recv_data_pkt(master_socket, ptr, bytes);
}

int write_trace(void *ptr, size_t bytes)
{
   size_t res = write(trace_file, ptr, bytes);
   return (res == bytes) ? 0 : 1;
}

void respond_sock(int r)
{
   send_response_byte(master_socket, r);
}

/* Apprentice function */

int write_sock(void *ptr, size_t bytes)
{
   return send_data_pkt(apprentice_socket, ptr, bytes);
}

int read_trace(void *ptr, size_t bytes)
{
   size_t res = read(trace_file, ptr, bytes);
   return (res == bytes) ? 0 : 1;
}

void respond_trace(int r)
{
   switch (r) {
      case 0: /* test ok */
      case 1: /* end of test */
         break;
      default:
         /* should not get here */
         abort();
         break;
   }
}

void master_sigill(int sig, siginfo_t *si, void *uc)
{
   int r;

   if (trace_file) {
      r = send_register_info(write_trace, uc);
   } else {
      r = recv_and_compare_register_info(read_sock, respond_sock, uc);
   }

   switch (r)
   {
      case 0:
         /* match OK */
         advance_pc(uc);
         return;
      default:
         /* mismatch, or end of test */
         siglongjmp(jmpbuf, 1);
   }
}

void apprentice_sigill(int sig, siginfo_t *si, void *uc)
{
   int r;

   if (trace_file) {
      r = recv_and_compare_register_info(read_trace, respond_trace, uc);
   } else {
      r = send_register_info(write_sock, uc);
   }

   switch (r)
   {
      case 0:
         /* match OK */
         advance_pc(uc);
         return;
      case 1:
         /* end of test */
         fprintf(stderr, "\nend of test\n");
         exit(0);
      default:
         /* mismatch */
         if (trace_file) {
            report_match_status();
         }
         exit(1);
   }
}

static void set_sigill_handler(void (*fn)(int, siginfo_t *, void *))
{
   struct sigaction sa;
   memset(&sa, 0, sizeof(struct sigaction));

   sa.sa_sigaction = fn;
   sa.sa_flags = SA_SIGINFO;
   sigemptyset(&sa.sa_mask);
   if (sigaction(SIGILL, &sa, 0) != 0)
   {
      perror("sigaction");
      exit(1);
   }
}

typedef void entrypoint_fn(void);

uintptr_t image_start_address;
entrypoint_fn *image_start;

void load_image(const char *imgfile)
{
   /* Load image file into memory as executable */
   struct stat st;
   fprintf(stderr, "loading test image %s...\n", imgfile);
   int fd = open(imgfile, O_RDONLY);
   if (fd < 0)
   {
      fprintf(stderr, "failed to open image file %s\n", imgfile);
      exit(1);
   }
   if (fstat(fd, &st) != 0)
   {
      perror("fstat");
      exit(1);
   }
   size_t len = st.st_size;
   void *addr;

   /* Map writable because we include the memory area for store
    * testing in the image.
    */
   addr = mmap(0, len, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE, fd, 0);
   if (!addr)
   {
      perror("mmap");
      exit(1);
   }
   close(fd);
   image_start = addr;
   image_start_address = (uintptr_t)addr;
}

int master(int sock)
{
   if (sigsetjmp(jmpbuf, 1))
   {
      if (trace_file) {
         close(trace_file);
         fprintf(stderr,"\nDone...\n");
         return 0;
      } else {
         return report_match_status();
      }
   }
   master_socket = sock;
   set_sigill_handler(&master_sigill);
   fprintf(stderr, "starting master image at 0x%"PRIxPTR"\n", image_start_address);
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}

int apprentice(int sock)
{
   apprentice_socket = sock;
   set_sigill_handler(&apprentice_sigill);
   fprintf(stderr, "starting apprentice image at 0x%"PRIxPTR"\n", image_start_address);
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}

int ismaster;

void usage (void)
{
   fprintf(stderr, "Usage: risu [--master] [--host <ip>] [--port <port>] <image file>\n\n");
   fprintf(stderr, "Run through the pattern file verifying each instruction\n");
   fprintf(stderr, "between master and apprentice risu processes.\n\n");
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "  --master          Be the master (server)\n");
   fprintf(stderr, "  -t, --trace=FILE  Record/playback trace file\n");
   fprintf(stderr, "  -h, --host=HOST   Specify master host machine (apprentice only)\n");
   fprintf(stderr, "  -p, --port=PORT   Specify the port to connect to/listen on (default 9191)\n");
}

int main(int argc, char **argv)
{
   // some handy defaults to make testing easier
   uint16_t port = 9191;
   char *hostname = "localhost";
   char *imgfile;
   char *trace_fn = NULL;
   int sock;

   // TODO clean this up later
   
   for (;;)
   {
      static struct option longopts[] = 
         {
            { "help", no_argument, 0, '?'},
            { "master", no_argument, &ismaster, 1 },
            { "trace", required_argument, 0, 't' },
            { "host", required_argument, 0, 'h' },
            { "port", required_argument, 0, 'p' },
            { "test-fp-exc", no_argument, &test_fp_exc, 1 },
            { 0,0,0,0 }
         };
      int optidx = 0;
      int c = getopt_long(argc, argv, "h:p:t:", longopts, &optidx);
      if (c == -1)
      {
         break;
      }
      
      switch (c)
      {
         case 0:
         {
            /* flag set by getopt_long, do nothing */
            break;
         }
         case 't':
         {
           trace_fn = optarg;
           break;
         }
         case 'h':
         {
            hostname = optarg;
            break;
         }
         case 'p':
         {
            // FIXME err handling
            port = strtol(optarg, 0, 10);
            break;
         }
         case '?':
         {
            usage();
            exit(1);
         }
         default:
            abort();
      }
   }

   imgfile = argv[optind];
   if (!imgfile)
   {
      fprintf(stderr, "Error: must specify image file name\n\n");
      usage();
      exit(1);
   }

   load_image(imgfile);

   if (ismaster)
   {
      if (trace_fn)
      {
         trace_file = open(trace_fn, O_WRONLY|O_CREAT, S_IRWXU);
      } else {
         fprintf(stderr, "master port %d\n", port);
         sock = master_connect(port);
      }
      return master(sock);
   }
   else
   {
      if (trace_fn)
      {
         trace_file = open(trace_fn, O_RDONLY);
      } else {
         fprintf(stderr, "apprentice host %s port %d\n", hostname, port);
         sock = apprentice_connect(hostname, port);
      }
      return apprentice(sock);
   }
}

   
