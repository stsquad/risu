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
                   progress_start progress_update progress_end);
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

1;
