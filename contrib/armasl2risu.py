#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Convert an ARM ASL diffined instruction description into RISU's form
#
# Copyright (c) 2017 Linaro Limited
# Author: Alex Bennée <alex.bennee@linaro.org>
#
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v1.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v10.html
#
# For more on ASL see:
#  https://alastairreid.github.io/specification_languages/
#  https://github.com/alastairreid/mra_tools
#
# Example invocation:
#  ./contrib/armasl2risu.py --only-desc "half" \
#      --insn-suffix "_FP16" \
#      xml/ISA_v82A_A64_xml_00bet3.1/*.xml
#

from argparse import ArgumentParser
import xml.etree.cElementTree as xetree
from copy import copy, deepcopy


def get_opts():
    "Return the parsed arguments"

    parser = ArgumentParser(description="Parse an ASL file for RISU patterns")

    parser.add_argument("xml", metavar="<xml>", nargs="+",
                        help="instruction xml")
    parser.add_argument("--only-variant", default=None,
                        help="Filter instructions on architecture variant")
    parser.add_argument("--only-desc", default=None, action="append",
                        help="Filter instructions on description (match all)")
    parser.add_argument("--only-insn", default=None,
                        help="Filter instructions on insn mnemonic")
    parser.add_argument("--insn-suffix", default=None,
                        help="Add suffix to instruction")
    parser.add_argument("--encode-registers", default=None,
                        help="Add register pattern suffix to instruction")

    return parser.parse_args()


class ASLObject:
    """
    A class of helper functions for interacting with ASL XML.

    Other objects use this to get access to the helpers.
    """

    def __init__(self, xml):
        self.xml = xml

    def _get_xml_attrib(self, attrib_name, default=None):
        "Return attribute or default value"
        try:
            return self.xml.attrib[attrib_name]
        except KeyError:
            return default

    def _decode_docvars(self):
        "Return a hash of details from the docvar"
        docvars = self.xml.find("docvars")
        h = {}

        for d in docvars.getchildren():
            h[d.attrib["key"]] = d.attrib["value"]

        return h

    def _get_docvar(self, key, default=None):
        try:
            return self.docvars[key]
        except KeyError:
            return default


class Bits(ASLObject):
    """
    A class to represent a range of bits.

    The bit range is defined by <box> elements in the XML.
    sub-encodings will enhance the base encoding from the instruction
    class so we need to merge/split as appropriate.
    """

    def __init__(self, box_xml):
        self.xml = box_xml
        self.hibit = int(self._get_xml_attrib("hibit"))
        self.constraint = self._get_xml_attrib("constraint", None)
        self.bits = list(box_xml)

        width = self._get_xml_attrib("width", 1)
        try:
            self.width = int(width)
        except ValueError:
            # sometimes we see width="", so count entries instead
            self.width = len(self.bits)

        self.fname = self._get_xml_attrib("name", "unknown")
        # Are any of these registers
        if self.fname[0] in "XRVPZ":
            self.reg = self.fname[0].lower()
        else:
            self.reg = None

    def __len__(self):
        assert(self.width == len(self.bits))
        return self.width

    def __str__(self):
        """
        Format as a string.

        This is the bitpattern format we export to RISU
        """
        if len(self.bits) < self.width:
            # empty <box> elements, usually <colspan N>
            return " %s:%d " % (self.fname, self.width)
        else:
            bit_str = ""
            for b in self.bits:
                if not b.text:
                    # FIX: concat adjacent empty entries
                    bit_str += " %s:1 " % (self.fname)
                else:
                    s = b.text
                    s = s.replace("(", "")
                    s = s.replace(")", "")
                    s = s.replace("x", " x:1 ")
                    bit_str += s
            return bit_str

    # Merge a new set of bits in, probably from a sub-encoding which
    # might fill in some details.
    def merge(self, new_bits):
        assert(self.width == len(new_bits))
        # print ("Merging: %s and %s" % (self, new_bits))
        # print ("self.bits are: %s" % (type(self.bits)))
        # print ("new_bits are: %s" % (type(new_bits)))
        if len(self.bits) < self.width:
            self.bits = copy(new_bits.bits)
            # print ("Replaced with %s" % (new_bits))
        else:
            for pos, (obit, nbit) in enumerate(zip(self.bits,
                                                   new_bits.bits)):
                # print ("%d: %s and %s" % (pos,obit.text,nbit.text))
                if nbit.text is not None:
                    # print ("Setting %d: %s to %s" % (pos, obit.text, nbit.text))
                    self.bits[pos] = copy(nbit)

    def split(self, split_pos):
        rem_bits = Bits(copy(self.xml))
        rem_bits.bits = rem_bits.bits[split_pos:]
        rem_bits.hibit = self.hibit + split_pos

        return rem_bits


class Instruction(ASLObject):
    """
    A class to represent a single instruction.
    """

    def _merge_patterns(self, base_pattern):
        "Update regdiagram with variant"
        pattern = deepcopy(base_pattern)

        # First get the new pattern bits
        updates = []
        for b in self.xml.findall("box"):
            updates.append(Bits(b))

        # Now iterate over the original pattern merging in any changes
        for i, obits in enumerate(pattern):
            for nbits in updates:
                if nbits.hibit == obits.hibit:
                    if nbits.width == obits.width:
                        pattern[i].merge(nbits)
                    else:
                        # they don't match, so split up the smaller
                        remainder = obits.split(obits.width - nbits.width)
                        pattern.insert(i + 1, remainder)
                        # print ("split %s into %s and %s" % (obits,
                        # self.pattern[i], self.pattern[i+1]))

        return pattern

    def __init__(self, insn_xml, base_pattern):
        self.xml = insn_xml
        self.name = self._get_xml_attrib("name", "UNK")
        self.docvars = self._decode_docvars()
        self.isa = self._get_docvar("isa", "???")
        self.pattern = self._merge_patterns(base_pattern)

        arch_variant = self.xml.find(".//arch_variants/arch_variant")
        if arch_variant is not None:
            self.variant = arch_variant.attrib["name"]
        else:
            self.variant = None

    def __str__(self):
        return self.name

    def reg_pattern(self):
        """
        Return the register pattern of an instruction.
        """
        regpattern = ""
        for bit in self.pattern:
            if bit.reg is not None:
                fname = bit.fname
                if (fname.endswith("t") or
                    fname.endswith("d") or
                    fname.endswith("dn")):
                    regpattern = regpattern + bit.reg
                else:
                    regpattern = bit.reg + regpattern

        return regpattern

    def bit_pattern(self):
        """
        Return the encoding pattern of an instruction
        """

        # assume the bits come in order high to low
        current_bit = 31
        bit_string = ""

        for bit in self.pattern:
            if bit.hibit > current_bit:
                print ("Error decoding %d > %d" % (bit.hibit, current_bit))
                return
            else:
                current_bit = bit.hibit

            bit_string += ("%s" % (bit))

        return (bit_string)

    def encoding(self):
        "Return the instruction encoding"

        if self.isa == "A64":
            return "A64_V"
        elif self.isa == "A32":
            return self.docvars["armarmheading"]
        elif self.isa == "T32":
            return self.docvars["armarmheading"]
        else:
            return "??? %s" % (self.isa)

    # Matching helpers
    #
    # These are used for selecting patterns we want to include in our
    # output. See also InsnSelector

    def match_name(self, name):
        "Match against mnumonic/name"
        return self.name.startswith(name)

    def match_desc(self, desc):
        "Match against any docvar value"
        if desc in self.docvars.values():
            return True
        else:
            return False

    def match_variant(self, variant):
        "Match against architecture variant"
        if self.variant is not None:
            return variant in self.variant
        else:
            return False


class InsnClass(Instruction):
    """
    A class to represent a set of instructions.

    This maps to .//classes/iclass in the XML. It defines the base bit
    pattern and a number of encodings.
    """

    def _decode_pattern(self, diagram):
        pattern = []
        if diagram is not None:
            for bits in diagram:
                pattern.append(Bits(bits))

        return pattern

    def __init__(self, ins_class, heading):
        "Decode an instruction class"
        self.xml = ins_class
        self.name = self._get_xml_attrib("name").lower()
        self.heading = heading
        self.docvars = self._decode_docvars()
        self.isa = self._get_docvar("isa", "???")
        regdiag = ins_class.find("regdiagram")
        self.pattern = self._decode_pattern(regdiag)

    def encodings(self):
        "Returns all the encodings"
        encs = []
        for e in self.xml.findall("encoding"):
            insn = Instruction(e, deepcopy(self.pattern))
            encs.append(insn)

        return encs


class InsnDict(dict):

    # We keep all the default methods of the parent dict
    def __init__(self, *args, **kw):
        super(InsnDict, self).__init__(*args, **kw)
        self.alias_count = 0

    def add_insn(self, insn):
        "Add an instruction, returning it's key"
        key = "%s" % (insn)
        if key in self.keys():
            old_insn = self.get(key)
            if old_insn.bit_pattern() == insn.bit_pattern():
                self.alias_count += 1
                if old_insn.encoding() != insn.encoding():
                    print("Aliased encoding")
            else:
                print ("already got a %s" % key)
                print ("old: %s with %s" % (old_insn, old_insn.bit_pattern()))
                print ("new: %s with %s" % (insn, insn.bit_pattern()))
        else:
            self.__setitem__(key, insn)


class InsnSelector:
    """
    A simple class for deciding if we select or drop a given
    instruction definition.
    """

    def __init__(self, match_insn=None,
                 match_variant=None, match_desc=None):
        self.match_insn = match_insn
        self.match_variant = match_variant
        self.match_desc = match_desc

    def skip_insn(self, insn):
        # Skip instruction if not in description filter spec
        matched = True

        if self.match_insn is not None:
            matched &= insn.match_name(self.match_insn)

        if self.match_variant is not None:
            matched &= insn.match_variant(self.match_variant)

        # all descriptions must match
        if self.match_desc is not None:
            for d in self.match_desc:
                matched &= insn.match_desc(d)

        return not matched


def decode_instructions(insn_xml, selector, insn_map):

    """Decode all the instructions in this XML"""
    header = False
    try:
        heading = insn_xml.getroot().attrib["title"]
    except KeyError:
        heading = "oops"

    for iclass in insn_xml.findall('.//classes/iclass'):

        insn = InsnClass(iclass, heading)
        encodings = insn.encodings()

        # print("insn %s has %d encodings" % (insn.name, len(encodings)))

        for e in encodings:
            if selector.skip_insn(e):
                continue

            insn_map.add_insn(e)

if __name__ == "__main__":
    args = get_opts()

    all_insns = InsnDict()
    selector = InsnSelector(args.only_insn, args.only_variant, args.only_desc)

    for asl_file in args.xml:
        xml = xetree.parse(asl_file)
        decode_instructions(xml, selector, all_insns)

    print "# Input file for risugen defining AArch64 instructions"
    print ".mode arm.aarch64"

    for name, insn in all_insns.items():
        print ("%s %s %s" % (name, insn.encoding(), insn.bit_pattern()))
