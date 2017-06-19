/*****************************************************************************
 * Copyright (c) 2016 Laurent Vivier
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *****************************************************************************/

#ifndef RISU_REGINFO_M68K_H
#define RISU_REGINFO_M68K_H

struct reginfo {
    uint32_t faulting_insn;
    uint32_t pc;
    gregset_t gregs;
    fpregset_t fpregs;
};

#endif /* RISU_REGINFO_M68K_H */
