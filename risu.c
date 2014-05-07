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

/* The memblock is a block of RAM that risu instructions can access.
 * From time to time risugen will emit code to move the base pointer
 * around in the overall block. The apprentice memblock is only used
 * by the master to keep a copy of it's remote end.
 */
void *memblock, *apprentice_memblock;

/* used when talking to remote risu */
int comms_sock;
int packet_mismatch, mem_used;

sigjmp_buf jmpbuf;

/* Should we test for FP exception status bits? */
int test_fp_exc = 0;

/* Read/write state */
static int write_state(void *ptr, size_t bytes)
{
   return send_data_pkt(comms_sock, ptr, bytes);
}

static int read_state(void *ptr, size_t bytes)
{
   return recv_data_pkt(comms_sock, ptr, bytes);
}

int check_memblock_match(void)
{
   return memcmp(memblock, apprentice_memblock, MEMBLOCKLEN)==0;
}


/* Master SIGILL
 *
 * The master is always checking it's state against the apprentice and
 * will report when the thing goes out of sync.
 */

static void master_sigill(int sig, siginfo_t *si, void *uc)
{
   int resp = 0;
   int op;
   
   sync_master_state(uc);
   op = fetch_risu_op();

   switch (op) {
      case OP_COMPARE:
      case OP_TESTEND:
      default:
      {
         /* Do a simple register compare on (a) explicit request
          * (b) end of test (c) a non-risuop UNDEF
          */
         size_t arch_reg_size;
         void *appr_reg_ptr = get_appr_reg_ptr(&arch_reg_size);
         if (read_state(appr_reg_ptr, arch_reg_size)) {
            packet_mismatch = 1;
            resp = 2;
         } else if (!check_registers_match()) {
            /* register mismatch */
            resp = 2;
         } else if (op == OP_TESTEND) {
            resp = 1;
         }
       
         send_response_byte(comms_sock, resp);
         break;
      }
      case OP_SETMEMBLOCK:
         memblock = set_memblock();
         break;
      case OP_GETMEMBLOCK:
         get_memblock(memblock, uc);
         break;
      case OP_COMPAREMEM:
      {
         mem_used = 1;
         if (read_state(apprentice_memblock, MEMBLOCKLEN)) {
            packet_mismatch = 1;
            resp = 2;
         } else if (!check_memblock_match()) {
            /* memory mismatch */
            resp = 2;
         }      
         send_response_byte(comms_sock, resp);
         break;
      }
   }

   switch (resp) {
      case 0:
         /* match OK */
         advance_pc(uc);
         return;
      default:
         /* mismatch, or end of test */
         siglongjmp(jmpbuf, 1);
   }
}

/* The apprentice just carries on reporting it's state for the
 * apprentice to check.
 */
void apprentice_sigill(int sig, siginfo_t *si, void *uc)
{
   int resp = 0;
   int op;

   sync_master_state(uc);
   op = fetch_risu_op();

   switch (op) {
      case OP_COMPARE:
      case OP_TESTEND:
      default:
      {
         size_t arch_reg_size;
         void *master_reg_ptr = get_master_reg_ptr(&arch_reg_size);

         /* Do a simple register compare on (a) explicit request
          * (b) end of test (c) a non-risuop UNDEF
          */
         resp = write_state(master_reg_ptr, arch_reg_size);
         break;
      }
      case OP_SETMEMBLOCK:
         memblock = set_memblock();
         break;
      case OP_GETMEMBLOCK:
         get_memblock(memblock, uc);
         break;
      case OP_COMPAREMEM:
         resp = write_state(memblock, MEMBLOCKLEN);
         break;
   }

   switch (resp)
   {
      case 0:
         /* match OK */
         advance_pc(uc);
         return;
      case 1:
         /* end of test */
         exit(0);
      default:
         /* mismatch */
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

int master()
{
   if (sigsetjmp(jmpbuf, 1))
   {
      return report_match_status(packet_mismatch, mem_used);
   }
   apprentice_memblock = malloc(MEMBLOCKLEN);
   set_sigill_handler(&master_sigill);
   fprintf(stderr, "starting image\n");
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}

int apprentice()
{
   set_sigill_handler(&apprentice_sigill);
   fprintf(stderr, "starting image\n");
   image_start();
   fprintf(stderr, "image returned unexpectedly\n");
   exit(1);
}

int ismaster;

int main(int argc, char **argv)
{
   // some handy defaults to make testing easier
   uint16_t port = 9191;
   char *hostname = "localhost";
   char *imgfile;

   // TODO clean this up later
   
   for (;;)
   {
      static struct option longopts[] = 
         {
            { "master", no_argument, &ismaster, 1 },
            { "host", required_argument, 0, 'h' },
            { "port", required_argument, 0, 'p' },
            { "test-fp-exc", no_argument, &test_fp_exc, 1 },
            { 0,0,0,0 }
         };
      int optidx = 0;
      int c = getopt_long(argc, argv, "h:p:", longopts, &optidx);
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
            /* error message printed by getopt_long */
            exit(1);
         }
         default:
            abort();
      }
   }

   imgfile = argv[optind];
   if (!imgfile)
   {
      fprintf(stderr, "must specify image file name\n");
      exit(1);
   }

   load_image(imgfile);
   
   if (ismaster)
   {
      fprintf(stderr, "master port %d\n", port);
      comms_sock = master_connect(port);
      return master();
   }
   else
   {
      fprintf(stderr, "apprentice host %s port %d\n", hostname, port);
      comms_sock = apprentice_connect(hostname, port);
      return apprentice();
   }
}

   
