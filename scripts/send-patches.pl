#!/usr/bin/perl
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2015- by Mauro Carvalho Chehab <mchehab@kernel.org>

use strict;
use warnings;
use Email::Simple;
use Email::Address;
use Getopt::Long;
use Pod::Usage;
use File::Path 'make_path';
require Tk;
require Tk::Text;
require Tk::Font;
require Tk::Toplevel;

# Where the patch series will be stored. If doesn't exits,
# it will be automatically created
my $tmp_dir = "patches/tmp";

# Used together with --avoid-resend
my $resend_cache_dir = "patches/last_patches";
my $version_ctrl = ".version_control";

#
# File editor function. Currently relies on Tk to open
# a separate edit window.
#
sub edit_text($) {
	my $fname = shift;
	my $string = qx(cat $fname);
	my $edited_text;

	my $toplvl = MainWindow->new();
	my $font = $toplvl->Font(family  => 'fixed', size => 12);

	my $frame_txt = $toplvl->Frame();
	my $frame_btn = $toplvl->Frame();

	$toplvl->configure(-title => "Editing $fname");

	my $text = $frame_txt->Scrolled('Text')->pack;
	$text->configure(-height      => 30,
			-background  => 'black',
			-foreground  => 'gray',
			-insertbackground => 'white',
			-width       => 80,
			-wrap        => 'word',
			-font        => $font);

	my $Button1 = $frame_btn->Button();
	$Button1->configure(-text    => 'OK',
		       -bg      => 'lightblue',
		       -width   => 5,
		       -height  => 1,
		       -command => sub{$edited_text = $text->get("1.0", "end"); $toplvl->destroy} );

	$text->insert('1.0', $string);

	# Pack the widgets in the frames
	$text->pack();
	$frame_txt->pack();
	$frame_btn->pack();
	$Button1->pack();

	$text->waitWindow();

	return $edited_text;
}

#
# Argument handling
#
my $edit = 0;
my $cover = 0;
my $man = 0;
my $help = 0;
my $cmd_line = "git format-patch -o $tmp_dir --stat --summary --patience --signoff --thread=shallow";
my $changeset = 0;
my $dont_send = 0;
my $avoid_resend = 0;
my $reply_patches = 0;
my $subject_prefix = "PATCH";
my $dont_get_maintainer = 0;
my $dont_get_reviewer = 0;
my $reroll_count = "";
my $git = 0;
my $nogit = 0;
my $add_everyone = 0;
my $unify = "";
my $to_maintainers = 0;

GetOptions(
	"cover|letter" => sub { $cmd_line .= " --cover-letter"; $cover = 1},
	"no-merge|no-merges|no-renames" =>  sub { $cmd_line .= " --no-renames" },
	"merge|M" =>  sub { $cmd_line .= " -M01" },
	"delete|D" =>  sub { $cmd_line .= " -D" },
	"unify|U=s" =>  \$unify,
	"to=s" => sub { my ($opt, $arg) = @_; $cmd_line .= " --to '$arg'" },
	"cc=s" => sub { my ($opt, $arg) = @_; $cmd_line .= " --cc '$arg'" },
	"prefix|subject-prefix=s" => \$subject_prefix,
	"edit|annotate" => \$edit,
	"dry-run|dont-send" => \$dont_send,
	"reply-patches" => sub { $reply_patches = 1; $avoid_resend = 1; $cmd_line .= " -N" },
	"avoid-resend" => sub { $avoid_resend = 1; $cmd_line .= " -N" },
	"dont-get-maintainer" => \$dont_get_maintainer,
	"dont-get-reviewer" => \$dont_get_reviewer,
	"everyone|add-everyone" => \$add_everyone,
	"git" => \$git,
	"no-git-fallback" => \$nogit,
	"to-maintainers" => \$to_maintainers,
	"v|reroll_count=s" => \$reroll_count,
	"help" => \$help,
	"man" => \$man,
) or pod2usage(2);

$help = 1 if (@ARGV < 1);

if ($avoid_resend && $cover) {
	printf ("Sorry, you can't avoid resend patches and add a cover yet.\n");
}

pod2usage(1) if $help;
pod2usage(-verbose => 2) if $man;

$cmd_line .= " --subject-prefix '$subject_prefix'";
$cmd_line .= " -v $reroll_count" if ($reroll_count);
$cmd_line .= " -U$unify" if ($unify);
$cmd_line = join(' ', $cmd_line, @ARGV);

$dont_get_reviewer = 1 if ($dont_get_maintainer);

#
# Prepare to avoid resending patches
#

my %cgid_to_msgid;
my %msgid_to_file;
my %msgid_to_subject;

sub msgid_from_last_patch($)
{
	my $change_id = shift;

	return 0 if (!$change_id);
	return 0 if (!$cgid_to_msgid{$change_id});

	return $cgid_to_msgid{$change_id};
}

if ($avoid_resend) {
	open IN, $version_ctrl or print "Can't find $version_ctrl\n";
	while (<IN>) {
		if (m/([^\t]+)\t([^\t]+).*\n/) {
			$cgid_to_msgid{$1} = $2;
		}
	}
	close IN;

	opendir(my $dh, $resend_cache_dir) || die "can't read patches at $resend_cache_dir";
	while(readdir $dh) {
		my $name = "$resend_cache_dir/$_";
		next if (-d $name);

		my $raw_email = qx(cat $name);
		my $email = Email::Simple->new($raw_email);
		my $msgid = $email->header("Message-Id");
		my $subject = $email->header("Subject");

		$msgid_to_file{$msgid} = $name if ($msgid);
		$msgid_to_subject{$msgid} = $subject if ($subject && $msgid);
	}
	closedir $dh;
}

sub get_maintainer($$)
{
	my $cmd = $_[0];
	my @file_cc = @{$_[1]};
	my $role;
	my $e_mail;
	my %cc = (
		"linux-kernel\@vger.kernel.org" => 1
	);

	foreach $e_mail (@file_cc) {
		my @addresses = Email::Address->parse($e_mail);
		for my $address (@addresses) {
			$cc{$address} = "cc";
		}
	}

	$cmd = "./scripts/get_maintainer.pl " . $cmd;

	print "$cmd\n";
	open IN, "$cmd |" or die "can't run $cmd";
	while (<IN>) {
		$e_mail = $_;
		$e_mail =~ s/(.*\@\S+)\s*\(.*/$1/;
		$e_mail =~ s/\s*$//;


		if (m/\(.*(open list|moderated list|subscriber list|maintainer|reviewer|modified|chief|commit_signer).*\)/) {
			$role = $1;
		} else {
			$role = "cc";
		}
		# Discard myself
		next if ($e_mail =~ m/(mchehab|mauro.?chehab)\S+\@\S+/);
		$cc{$e_mail} = $role;
	}
	close IN;

	return %cc;
}

#
# Generate patches with git format-patch
#
make_path($tmp_dir);
unlink glob "$tmp_dir/*";

print "\$ $cmd_line\n";
system ("$cmd_line >>/dev/null") && die("Failed to run git format-patch");

#
# Add Cc: based on get_maintainer.pl script
#
my @patches;
my @send_patches;

opendir(my $dh, $tmp_dir) || die "can't read patches at $tmp_dir";
while(readdir $dh) {
	my $name = "$tmp_dir/$_";
	push @patches,$name if (-f $name);
}
closedir $dh;

my %changeids;

my $has_cover;
my %cover_cc;

foreach my $f(sort @patches) {
	print "Checking $f\n";
	if ($f =~ m,0000-cover-letter.patch$,) {
		push @send_patches, $f;
		$has_cover = 1;
		next;
	}

	my $raw_email = qx(cat $f);
	die "Can't read $f" if (!$raw_email);

	my $email = Email::Simple->new($raw_email);

	my $msgid = 0;
	my $oldsubject;
	my $change_id;

	if ($raw_email =~ m/\nChange-Id:\s+([^\n]+)\n/) {
		$change_id = $1;
	}

	if ($avoid_resend) {
		$msgid = msgid_from_last_patch($change_id);
		if ($msgid) {
			if ($msgid_to_subject{$msgid}) {
				$oldsubject = $msgid_to_subject{$msgid};

				my $file = $msgid_to_file{$msgid};

				my $old_md5 = qx(filterdiff $file | md5sum);
				my $new_md5 = qx(filterdiff $f | md5sum);

				$old_md5 =~ s/(\S+).*$/$1/;
				$new_md5 =~ s/(\S+).*$/$1/;

				if ($old_md5 eq $new_md5) {
					printf "   Skipping patch as it is identical to previous version\n";
					unlink $f;
					next;
				}
			}
			my $new_msgid = $email->header("Message-Id");
			$changeids{$change_id} = $new_msgid if ($new_msgid);
		}
	}

	# Patch was not avoided. Push to the list of patches to send
	push @send_patches, $f;

	my $cmd = "";
	$cmd .= "--git" if ($git);
	$cmd .="--nogit-fallback --nogit-blame --nogit" if ($nogit);
	$cmd .=" $f";

	my @file_cc = $email->header("Cc");
	if ($to_maintainers) {
		push @file_cc, $email->header("To") if ($email->header("To"));
	}
	my %cc_email_map = get_maintainer $cmd, \@file_cc;
	my %maintainers;

	@file_cc = ();
	my @file_to = ();
	foreach my $cc (sort keys %cc_email_map) {
		my $ml_added = 0;
		my $role = $cc_email_map{$cc};

		my $type = "Cc";
		$type = "To" if ($to_maintainers && $role =~ "maintainer");

		if ($role =~ "maintainer") {
			if (!$dont_get_maintainer) {
				$ml_added = 1;
				$cover_cc{$cc} = 1;
			}
		} elsif ($role =~ "reviewer") {
			if (!$dont_get_reviewer) {
				$ml_added = 1;
				$cover_cc{$cc} = 1;
			}
		} elsif ($role =~ "list") {
			$ml_added = 1;
			$cover_cc{$cc} = 1;
		} elsif ($add_everyone) {
			$ml_added = 1;
			$cover_cc{$cc} = 1;
		}

		if ($type eq "To") {
			push @file_to, $cc;
		} else {
			push @file_cc, $cc;
		}
		if ($ml_added && $cover) {
			printf "    $type + cover Cc: $cc (%s)\n", $role;
		} else {
			printf "    $type: $cc (%s)\n", $role;
		}
	}

	$email->header_set("To", @file_to) if (@file_to);
	$email->header_set("Cc", @file_cc) if (@file_cc);

	# Remove Change-Id meta-data from the e-mail to be submitted
	my $body = $email->body;
	$body =~ s/(\nChange-Id:\s+[^\n]+\n)/\n/;
	$email->body_set($body);

	if ($avoid_resend) {
		if (!$reply_patches && $msgid) {
			$email->body_set("New version of $oldsubject\n\n$body");
		} else {
			die "New patches in the series. Can't proceed." if (!$msgid);
			die "Failed to find old subject. Can't proceed." if (!$oldsubject);

			$email->header_set("Subject", "Re: $oldsubject");
		}
		$email->header_set("In-Reply-To", $msgid);
		$email->header_set("References", $msgid);
	}

	open OUT, ">$f";
	print OUT $email->as_string;
	close OUT;
}

# Sanity check
die "Something wrong when generating/detecting a cover" if ($cover && !$has_cover);

#
# Add everyone at the cover's to: field
#
if ($has_cover) {
	my $count_cc = 0;
	foreach my $f(sort @patches) {
		next if (!($f =~ m,0000-cover-letter.patch$,));

		print "$f:\n";
		my $raw_email = qx(cat $f);
		die "Can't read $f" if (!$raw_email);

		my $email = Email::Simple->new($raw_email);

		my @file_cc = $email->header("Cc");

		foreach my $e_mail (@file_cc) {
			my @addresses = Email::Address->parse($e_mail);
			for my $address (@addresses) {
				$cover_cc{$address} = 1;
			}
		}

		foreach my $to(sort keys %cover_cc) {
			print "    Cc: $to\n";
			push @file_cc, $to;
			$count_cc++;
		}

		$email->header_set("Cc", @file_cc);

		open OUT, ">$f";
		print OUT $email->as_string;
		close OUT;

		print "Number of Cc at cover: $count_cc\n";
	}
}

#
# Renumber the patches
#
if ($avoid_resend && !$reply_patches) {
	my $tot_patch = @send_patches;

	$tot_patch-- if ($cover);

	my $digits = int(log($tot_patch)/log(10)+0.99999);
	my $patch = 1;

	foreach my $f(@send_patches) {
		next if ($f =~ m,0000-cover-letter.patch$,);

		my $raw_email = qx(cat $f);
		die "Can't read $f" if (!$raw_email);

		my $email = Email::Simple->new($raw_email);

		my $subject = $email->header("Subject");

		my $number = sprintf("%0${digits}d/%0${digits}d", $patch, $tot_patch);

		$subject =~ s/^\[[^\]]+\]\s*//;
		$subject = "[$subject_prefix $number] " . $subject;
		printf("$subject\n");
		$email->header_set("Subject", $subject);

		$patch++;

		open OUT, ">$f";
		print OUT $email->as_string;
		close OUT;
	}
}

#
# Open an editor if needed
#
if ($edit || $cover) {
	foreach my $f(sort @send_patches) {
		my $new_text;

		do {
			$new_text = edit_text($f);
		} while (!$new_text);

		open OUT, ">$f";
		print OUT $new_text;
		close OUT;

		last if ($cover);
	}
}

#
# Send the emails
#

if (!$dont_send) {
	printf("\$ git send-email $tmp_dir\n");
	system("git send-email $tmp_dir");
} else {
	printf("Use git send-email $tmp_dir to send the patches\n");
}

#
# Update the change IDs with the new patches
#
foreach my $chgid (keys %changeids) {
	$cgid_to_msgid{$chgid} = $changeids{$chgid};
}

open OUT,">$version_ctrl.new";
foreach my $chgid (sort keys %cgid_to_msgid) {
	printf OUT "%s\t%s\n", $chgid, $cgid_to_msgid{$chgid};
}
close OUT;

if ($dont_send) {
	printf("New version control stored as: .version_control.new\n" .
	       "Don't forget rename it to .version_control for the next patch series after sending it.\n");
} else {
	rename $version_ctrl, "$version_ctrl.old";
	rename "$version_ctrl.new", $version_ctrl;
}


__END__

=head1 NAME

send-patches.pl - Send patches upstream

=head1 SYNOPSIS

send-patches.pl [options] [changeset] -- [options for git format-patch]

Options:

--cover/--cover-letter
--no-renames/--no-merges/--no-renames
--merge/-M
--delete/-D
--unify/-U [level]
--to [e@mail]
--cc [e@mail]
--prefix/--subject-prefix
--edit
--dont-send/--dry-run
--avoid-resend
--reply-patches
--dont-get-maintainer
--not-everyone/--dont-add-everyone
--no-git-fallback
--to-maintainers
--help
--man
--reroll-count/-v [version number]

=head1 OPTIONS

=over 8

=item B<--cover> or B<--cover-letter>

Patch series will have a cover letter. Automatically enables edition

=item B<--no-renames> or B<--no-merges> or B<--no-merge>

Disables git merge detection with git show --no-renames

=item B<--merge> or B<--M>

Enables aggressive git merge detection with git show -M01

=item B<--delete> or B<--D>

Omit the previous content on deletes, printing only the header but
not the diff between the removed files and /dev/null.

=item B<--unify> or B<--U>

Set the unify diff level (default=3).

=item B<--to>

Add one more recipient destination for the e-mail
(at the To: part of the email)

=item B<--cc>

Add one more recipient carbon copy destination for the e-mail
(at the Cc: part of the email)

=item B<--prefix> or B<--subject-prefix>

By default, the subject prefix will be "PATCH". This otpion allows changing
it.

=item B<--edit>

Allows editing each patch in the series, and the cover letter.

=item B<--dont-send> or B<--dry-run>

Do everything but calling git send-email. Useful to test the tool or
when you need to do more complex things.

=item B<--reply-patches>

Instead of sending a new series, reply to an existing one. This only
works if no new patches were added at the series.

=item B<--avoid-resend>

Don't resend patches that are identical to the previosly send
series of patches. The patches that will be send will be renumbered.

Please notice that this option is currently incompatible with
a --cover, as we need to teach this script how to remove the
removed patches from the letter summary.

=item B<--dont-get-maintainer>

Ignore maintainers at the cover letter.

=item B<--dont-get-reviewer>

Ignore reviewers at the cover letter.

=item B<--everyone>/<--add-everyone>

The script/get_maintainers.pl returns maintainers, reviewers and mailing lists.
It also returns a list of usual contributors.

By default, the usual contributors are ignored at the cover letter, being
added only at the patches themselves. When this flag is used, they'll also
be c/c to the cover letter.

=item B<--git>

Include recent git *-by: signers.

=item B<--no-git-fallback>

Use git when no exact MAINTAINERS pattern. This disables detection of the
usual contributors.

=item B<--to-maintainers>

Instead of placing patches on a series, send them individually
to their own maintainers.

=item B<-v>/<--reroll-count>

Change the version number on a patch series, by passing --reroll-count
to git format-patch.

=item B<--help>

Print a brief help message and exits.

=item B<--man>

Prints the manual page and exits.

=back

=head1 DESCRIPTION
B<This program> will submit a patch series upstream.
=cut

=head1 BUGS

Report bugs to Mauro Carvalho Chehab <mchehab@kernel.org>

=head1 COPYRIGHT

Copyright (c) 2015- by Mauro Carvalho Chehab <mcheha@kernel.org>.

License GPLv2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>.

This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

=cut
