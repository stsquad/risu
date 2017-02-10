#!/usr/bin/perl -w
###############################################################################
# Copyright (c) 2016 Laurent Vivier
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v1.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v10.html
###############################################################################


# risugen -- generate a test binary file for use with risu
# See 'risugen --help' for usage information.
package risugen_m68k;

use strict;
use warnings;

use risugen_common;

require Exporter;

our @ISA    = qw(Exporter);
our @EXPORT = qw(write_test_code);

my $periodic_reg_random = 1;

#
# Maximum alignment restriction permitted for a memory op.
my $MAXALIGN = 64;

sub write_risuop($)
{
    my ($op) = @_;
    insn32(0x4afc7000 | $op);
}

sub write_mov_ccr($)
{
    my ($imm) = @_;
    insn16(0x44fc);
    insn16($imm);
}

sub write_movb_di($$)
{
    my ($r, $imm) = @_;

    # move.b #imm,%dr
    insn16(0x103c | ($r << 9));
    insn16($imm)
}

sub write_mov_di($$)
{
    my ($r, $imm) = @_;

    # move.l #imm,%dr
    insn16(0x203c | ($r << 9));
    insn32($imm)
}

sub write_mov_ai($$)
{
    my ($r, $imm) = @_;

    # movea.l #imm,%ar
    insn16(0x207c | ($r << 9));
    insn32($imm)
}

sub write_mov_ri($$)
{
    my ($r, $imm) = @_;

    if ($r < 8) {
        write_mov_di($r, $imm);
    } else {
        write_mov_ai($r - 8, $imm);
    }
}

sub write_random_regdata()
{
    # general purpose registers (except A6 (FP) and A7 (SP))
    for (my $i = 0; $i < 14; $i++) {
        write_mov_ri($i, rand(0xffffffff));
    }
    # initialize condition register
    write_mov_ccr(rand(0x10000));
}

my $OP_COMPARE = 0;        # compare registers
my $OP_TESTEND = 1;        # end of test, stop
my $OP_SETMEMBLOCK = 2;    # r0 is address of memory block (8192 bytes)
my $OP_GETMEMBLOCK = 3;    # add the address of memory block to r0
my $OP_COMPAREMEM = 4;     # compare memory block

sub write_random_register_data()
{
    write_random_regdata();
    write_risuop($OP_COMPARE);
}

sub eval_with_fields($$$$$) {
    # Evaluate the given block in an environment with Perl variables
    # set corresponding to the variable fields for the insn.
    # Return the result of the eval; we die with a useful error
    # message in case of syntax error.
    my ($insnname, $insn, $rec, $blockname, $block) = @_;
    my $evalstr = "{ ";
    for my $tuple (@{ $rec->{fields} }) {
        my ($var, $pos, $mask) = @$tuple;
        my $val = ($insn >> $pos) & $mask;
        $evalstr .= "my (\$$var) = $val; ";
    }
    $evalstr .= $block;
    $evalstr .= "}";
    my $v = eval $evalstr;
    if ($@) {
        print "Syntax error detected evaluating $insnname $blockname string:\n$block\n$@";
        exit(1);
    }
    return $v;
}

sub gen_one_insn($$)
{
    # Given an instruction-details array, generate an instruction
    my $constraintfailures = 0;

    INSN: while(1) {
        my ($forcecond, $rec) = @_;
        my $insn = int(rand(0xffffffff));
        my $insnname = $rec->{name};
        my $insnwidth = $rec->{width};
        my $fixedbits = $rec->{fixedbits};
        my $fixedbitmask = $rec->{fixedbitmask};
        my $constraint = $rec->{blocks}{"constraints"};
        my $memblock = $rec->{blocks}{"memory"};

        $insn &= ~$fixedbitmask;
        $insn |= $fixedbits;

        for my $tuple (@{ $rec->{fields} }) {
            my ($var, $pos, $mask) = @$tuple;
            my $val = ($insn >> $pos) & $mask;
            # Check constraints here:
            # not allowed to use or modify sp (A7) or fp (A6)
            next INSN if ($var =~ /^A/ && (($val == 6) || ($val == 7)));
        }
        if (defined $constraint) {
            # user-specified constraint: evaluate in an environment
            # with variables set corresponding to the variable fields.
            my $v = eval_with_fields($insnname, $insn, $rec, "constraints", $constraint);
            if (!$v) {
                $constraintfailures++;
                if ($constraintfailures > 10000) {
                    print "10000 consecutive constraint failures for $insnname constraints string:\n$constraint\n";
                    exit (1);
                }
                next INSN;
            }
        }

        # OK, we got a good one
        $constraintfailures = 0;

        insn16($insn >> 16);
        if ($insnwidth == 32) {
            insn16($insn & 0xffff);
        }

        return;
    }
}

sub write_test_code($)
{
    my ($params) = @_;

    my $condprob = $params->{ 'condprob' };
    my $numinsns = $params->{ 'numinsns' };
    my $outfile = $params->{ 'outfile' };

    my @pattern_re = @{ $params->{ 'pattern_re' } };
    my @not_pattern_re = @{ $params->{ 'not_pattern_re' } };
    my %insn_details = %{ $params->{ 'details' } };

    # Specify the order to use for insn32() and insn16() writes.
    set_endian(1);

    open_bin($outfile);

    # convert from probability that insn will be conditional to
    # probability of forcing insn to unconditional
    $condprob = 1 - $condprob;

    # TODO better random number generator?
    srand(0);

    # Get a list of the insn keys which are permitted by the re patterns
    my @keys = sort keys %insn_details;
    if (@pattern_re) {
        my $re = '\b((' . join(')|(',@pattern_re) . '))\b';
        @keys = grep /$re/, @keys;
    }
    # exclude any specifics
    if (@not_pattern_re) {
        my $re = '\b((' . join(')|(',@not_pattern_re) . '))\b';
        @keys = grep !/$re/, @keys;
    }
    if (!@keys) {
        print STDERR "No instruction patterns available! (bad config file or --pattern argument?)\n";
        exit(1);
    }
    print "Generating code using patterns: @keys...\n";
    progress_start(78, $numinsns);

    if (grep { defined($insn_details{$_}->{blocks}->{"memory"}) } @keys) {
        write_memblock_setup();
    }

    # memblock setup doesn't clean its registers, so this must come afterwards.
    write_random_register_data();

    for my $i (1..$numinsns) {
        my $insn_enc = $keys[int rand (@keys)];
        my $forcecond = (rand() < $condprob) ? 1 : 0;
        gen_one_insn($forcecond, $insn_details{$insn_enc});
        write_risuop($OP_COMPARE);
        # Rewrite the registers periodically. This avoids the tendency
        # for the VFP registers to decay to NaNs and zeroes.
        if ($periodic_reg_random && ($i % 100) == 0) {
            write_random_register_data();
        }
        progress_update($i);
    }
    write_risuop($OP_TESTEND);
    progress_end();
    close_bin();
}

1;
