/*******************************************************************************
 * Copyright (c) 2010 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 ******************************************************************************/

#ifndef RISU_REGINFO_I386_H
#define RISU_REGINFO_I386_H

/*
 * This is the data structure we pass over the socket.
 * It is a simplified and reduced subset of what can
 * be obtained with a ucontext_t*
 */
struct reginfo {
    uint32_t faulting_insn;
    gregset_t gregs;
};

/*
 * For i386, the defines are named REG_EAX, etc.
 * For x86_64, the defines are name REG_RAX, etc.
 */
#ifdef __x86_64__
# define REG_E(X)   REG_R##X
#else
# define REG_E(X)   REG_E##X
#endif

#endif /* RISU_REGINFO_I386_H */
