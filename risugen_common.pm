#!/usr/bin/perl -w
###############################################################################
# Copyright (c) 2017 Linaro Limited
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v1.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v10.html
#
# Contributors:
#     Peter Maydell (Linaro) - initial implementation
###############################################################################

# risugen_common -- common utility routines for CPU modules.
# We don't declare ourselves in a package so all these functions
# and variables are available for use.

package risugen_common;

use strict;
use warnings;

BEGIN {
    require Exporter;

    our @ISA = qw(Exporter);
    our @EXPORT = qw(open_bin close_bin set_endian insn32 insn16 bytecount
                   progress_start progress_update progress_end
                   eval_with_fields is_pow_of_2 sextract ctz
                   dump_insn_details);
}

our $bytecount;

my $bigendian = 0;

# Set the endianness when insn32() and insn16() write to the output
# (default is little endian, 0).
sub set_endian
{
    $bigendian = @_;
}

sub open_bin
{
    my ($fname) = @_;
    open(BIN, ">", $fname) or die "can't open %fname: $!";
    $bytecount = 0;
}

sub close_bin
{
    close(BIN) or die "can't close output file: $!";
}

sub insn32($)
{
    my ($insn) = @_;
    print BIN pack($bigendian ? "N" : "V", $insn);
    $bytecount += 4;
}

sub insn16($)
{
    my ($insn) = @_;
    print BIN pack($bigendian ? "n" : "v", $insn);
    $bytecount += 2;
}

# Progress bar implementation
my $lastprog;
my $proglen;
my $progmax;

sub progress_start($$)
{
    ($proglen, $progmax) = @_;
    $proglen -= 2; # allow for [] chars
    $| = 1;        # disable buffering so we can see the meter...
    print "[" . " " x $proglen . "]\r";
    $lastprog = 0;
}

sub progress_update($)
{
    # update the progress bar with current progress
    my ($done) = @_;
    my $barlen = int($proglen * $done / $progmax);
    if ($barlen != $lastprog) {
        $lastprog = $barlen;
        print "[" . "-" x $barlen . " " x ($proglen - $barlen) . "]\r";
    }
}

sub progress_end()
{
    print "[" . "-" x $proglen . "]\n";
    $| = 0;
}

sub eval_with_fields($$$$$) {
    # Evaluate the given block in an environment with Perl variables
    # set corresponding to the variable fields for the insn.
    # Return the result of the eval; we die with a useful error
    # message in case of syntax error.
    #
    # At the moment we just evaluate the string in the environment
    # of the calling package.
    # What we *ought* to do here is to give the config snippets
    # their own package, and explicitly import into it only the
    # functions that we want to be accessible to the config.
    # That would provide better separation and an explicitly set up
    # environment that doesn't allow config file code to accidentally
    # change state it shouldn't have access to, and avoid the need to
    # use 'caller' to get the package name of our calling function.
    my ($insnname, $insn, $rec, $blockname, $block) = @_;
    my $calling_package = caller;
    my $evalstr = "{ package $calling_package; ";
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

sub is_pow_of_2($)
{
    my ($x) = @_;
    return ($x > 0) && (($x & ($x - 1)) == 0);
}

# sign-extract from a nbit optionally signed bitfield
sub sextract($$)
{
    my ($field, $nbits) = @_;

    my $sign = $field & (1 << ($nbits - 1));
    return -$sign + ($field ^ $sign);
}

sub ctz($)
{
    # Count trailing zeros, similar semantic to gcc builtin:
    # undefined return value if input is zero.
    my ($in) = @_;

    # XXX should use log2, popcount, ...
    my $imm = 0;
    for (my $cnt = $in; $cnt > 1; $cnt >>= 1) {
        $imm += 1;
    }
    return $imm;
}

sub dump_insn_details($$)
{
    # Dump the instruction details for one insn
    my ($insn, $rec) = @_;
    print "insn $insn: ";
    my $insnwidth = $rec->{width};
    my $fixedbits = $rec->{fixedbits};
    my $fixedbitmask = $rec->{fixedbitmask};
    my $constraint = $rec->{blocks}{"constraints"};
    print sprintf(" insnwidth %d fixedbits %08x mask %08x ", $insnwidth, $fixedbits, $fixedbitmask);
    if (defined $constraint) {
        print "constraint $constraint ";
    }
    for my $tuple (@{ $rec->{fields} }) {
        my ($var, $pos, $mask) = @$tuple;
        print "($var, $pos, " . sprintf("%08x", $mask) . ") ";
    }
    print "\n";
}

1;
