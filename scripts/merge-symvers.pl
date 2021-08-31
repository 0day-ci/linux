#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0-only

use autodie;
use strict;
use warnings;
use Getopt::Long 'GetOptions';

my $ar;
my $output;

GetOptions(
	'a|ar=s' => \$ar,
	'o|output=s'  => \$output,
);

# Collect all objects
my @objects;

foreach (@ARGV) {
	if (/\.o$/) {
		# Some objects (head-y) are linked to vmlinux directly.
		push(@objects, $_);
	} elsif (/\.a$/) {
		# Most of built-in objects are contained in built-in.a or lib.a.
		# Use 'ar -t' to get the list of the contained objects.
		$_ = `$ar -t $_`;
		push(@objects, split(/\n/));
	} else {
		die "$_: unknown file type\n";
	}
}

open(my $out_fh, '>', "$output");

foreach (@objects) {
	# The symbol CRCs for foo/bar/baz.o is output to foo/bar/baz.o.symversions
	s/(.*)/$1.symversions/;

	if (! -e $_) {
		# .symversions does not exist if the object does not contain
		# EXPORT_SYMBOL at all. Skip it.
		next;
	}

	open(my $in_fh, '<', "$_");
	# Concatenate all the existing *.symversions files.
	print $out_fh do { local $/; <$in_fh> };
	close $in_fh;
}

close $out_fh;
