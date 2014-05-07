/*******************************************************************************
 * Copyright (c) 2010 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 *******************************************************************************/

#ifndef RISU_H
#define RISU_H

#include <inttypes.h>

#include "config.h"

#ifndef HAVE_SOCKLEN_T
#define socklen_t int
#endif /* HAVE_SOCKLEN_T */

#ifndef HAVE_UINTPTR_T
#define uintptr_t size_t
#endif /* HAVE_UINTPTR_T */

/* Socket related routines */
int master_connect(int port);
int apprentice_connect(const char *hostname, int port);
int send_data_pkt(int sock, void *pkt, int pktlen);
int recv_data_pkt(int sock, void *pkt, int pktlen);
void send_response_byte(int sock, int resp);

/* Check the memblock is consistent with it's apprentice */
int check_memblock_match(void);

extern uintptr_t image_start_address;

extern int test_fp_exc;

/* Ops code under test can request from risu: */
#define OP_COMPARE 0
#define OP_TESTEND 1
#define OP_SETMEMBLOCK 2
#define OP_GETMEMBLOCK 3
#define OP_COMPAREMEM 4

/* The memory block should be this long */
#define MEMBLOCKLEN 8192

/* Interface provided by CPU-specific code: */

/* Print a useful report on the status of the last comparison
 * done by master_sigill(). This is called on exit, so need not
 * restrict itself to signal-safe functions.
 *
 * @param packet_mismatch: Comms got out of sync
 * @param mem_used: We have done a memory compare op
 *
 * @return: 0 if it was a good match (ie end of test) and 1 for a mismatch.
 */
int report_match_status(int packet_mismatch, int mem_used);

/* Move the PC past this faulting insn by adjusting ucontext
 *
 * @param uc: anonymous user context from signal
 */
void advance_pc(void *uc);

/* Synchronise state from system ucontext for later comparison.
 *
 * Both master and apprentice call this to save the current execution
 * state (mainly registers) for later analysis.
 *
 * @param uc: anonymous user context from signal
 */
void sync_master_state(void *uc);

/* Fetch the current risu operation.
 *
 * This returns the current operation which will be encoded
 * differently depending on the architecture. This assumes the system
 * state has already been synchronised with sync_master_state.
 *
 * @return: base RISU operation (OP_* above)
 */
int fetch_risu_op(void);

/* Check master and apprentice registers match.
 *
 * This assumes the internal copies of the register state have been
 * updated.
 *
 * @return: true if registers match
 */
int check_registers_match(void);

/* Get pointer to the apprentice register info
 *
 * This is used so the master risu can update its reference copy from
 * the apprentice.
 *
 * @param sz: reference for returning size of structure
 * @return: pointer to the $ARCH specific register info
 */
void * get_appr_reg_ptr(size_t *sz);

/* Get pointer to the master register info
 *
 * This is used so the apprentice risu can send it's register state to
 * the master instance.
 *
 * @param sz: reference for returning size of structure
 * @return: pointer to the $ARCH specific register info
 */
void * get_master_reg_ptr(size_t *sz);

/*
 * Notify the system of the location of the memory block
 */
void * set_memblock(void);

/* Set a $ARCH dependant register to point at a new section of the
 * memblock based on an offset determined by another $ARCH dependent
 * register.
 *
 * @param memblock: base address of the memory block
 * @param vuc: anonymous user context from signal
 */
void get_memblock(void *memblock, void *vuc);

#endif /* RISU_H */
