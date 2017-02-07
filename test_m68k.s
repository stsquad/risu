/*******************************************************************************
 * Copyright (c) 2016 Laurent Vivier
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *******************************************************************************

/* Initialise the gp regs */
moveq.l #0, %d0
move.l %d0, %d1
move.l %d0, %d2
move.l %d0, %d3
move.l %d0, %d4
move.l %d0, %d5
move.l %d0, %d6
move.l %d0, %d7
move.l %d0, %a0
move.l %d0, %a1
move.l %d0, %a2
move.l %d0, %a3
move.l %d0, %a4
move.l %d0, %a5

/* do compare */
.int 0x4afc7000
/* exit test */
.int 0x4afc7001
