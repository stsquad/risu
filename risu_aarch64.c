/******************************************************************************
 * Copyright (c) 2013 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Claudio Fontana (Linaro) - initial implementation
 *     based on Peter Maydell's risu_arm.c
 *****************************************************************************/

#include <stdio.h>
#include <ucontext.h>
#include <string.h>

#include "risu.h"
#include "risu_reginfo_aarch64.h"

/* The master and apprentice views of the register state are arch
 * specific */
struct reginfo master_ri, apprentice_ri;

void sync_master_state(void *uc)
{
    reginfo_init(&master_ri, uc);
}       

int fetch_risu_op(void)
{
   uint32_t insn = master_ri.faulting_insn;
   /* Return the risuop we have been asked to do
    * (or -1 if this was a SIGILL for a non-risuop insn)
    */
   uint32_t op = insn & 0xf;
   uint32_t key = insn & ~0xf;
   uint32_t risukey = 0x00005af0;
   return (key != risukey) ? -1 : op;
}

void advance_pc(void *vuc)
{
    ucontext_t *uc = vuc;
    uc->uc_mcontext.pc += 4;
}

int check_registers_match(void)
{
   return reginfo_is_eq(&master_ri, &apprentice_ri);
}

void * get_appr_reg_ptr(size_t *sz)
{
   *sz = sizeof(apprentice_ri);
   return &apprentice_ri;
}

void * get_master_reg_ptr(size_t *sz)
{
   *sz = sizeof(master_ri);
   return &master_ri;
}

void * set_memblock(void)
{
   return (void *)master_ri.regs[0];
}

void get_memblock(void *memblock, void *vuc)
{
   ucontext_t *uc = vuc;
   uintptr_t new_ptr = master_ri.regs[0] + (uintptr_t)memblock;
   uc->uc_mcontext.regs[0] = new_ptr;
}

/* Print a useful report on the status of the last comparison
 * done in recv_and_compare_register_info(). This is called on
 * exit, so need not restrict itself to signal-safe functions.
 * Should return 0 if it was a good match (ie end of test)
 * and 1 for a mismatch.
 */
int report_match_status(int packet_mismatch, int mem_used)
{
   int resp = 0;
   fprintf(stderr, "match status(%d/%d)...\n", packet_mismatch, mem_used);
   if (packet_mismatch) {
       fprintf(stderr, "packet mismatch (probably disagreement "
               "about UNDEF on load/store)\n");
       /* We don't have valid reginfo from the apprentice side
        * so stop now rather than printing anything about it.
        */
       fprintf(stderr, "master reginfo:\n");
       reginfo_dump(&master_ri, stderr);
       return 1;
   }
   if (memcmp(&master_ri, &apprentice_ri, sizeof(master_ri)) != 0)
   {
       fprintf(stderr, "mismatch on regs!\n");
       resp = 1;
   }
   if (mem_used && !check_memblock_match()) {
       fprintf(stderr, "mismatch on memory!\n");
       resp = 1;
   }
   if (!resp) {
       fprintf(stderr, "match!\n");
       return 0;
   }

   fprintf(stderr, "master reginfo:\n");
   reginfo_dump(&master_ri, stderr);
   fprintf(stderr, "apprentice reginfo:\n");
   reginfo_dump(&apprentice_ri, stderr);

   reginfo_dump_mismatch(&master_ri, &apprentice_ri, stderr);
   return resp;
}

