.. SPDX-License-Identifier: (GPL-2.0+ OR CC-BY-4.0)
..
   If you want to distribute this text under CC-BY-4.0 only, please use 'The
   Linux kernel developers' for author attribution and link this as source:
   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/Documentation/admin-guide/regressions.rst
..
   Note: Only the content of this RST file as found in the Linux kernel sources
   is available under CC-BY-4.0, as versions of this text that were processed
   (for example by the kernel's build system) might contain content taken from
   files which use a more restrictive license.


Regressions
+++++++++++

The first rule of Linux kernel development: '*We don't cause regressions*'.
Linux founder and lead developer Linus Torvalds strictly enforces the rule
himself. This document describes what this means in practice and how the Linux
kernel's development model ensues all reported regressions get addressed; it
covers aspects relevant for both users and developers.

The important bits for people affected by regressions
=====================================================

It's a regression if something running fine with one Linux kernel works worse or
not at all with a newer version. Note, the newer kernel has to be compiled using
a similar configuration -- for this and other fine print, check out below
section "What is a 'regression' and what is the 'no regressions rule'?".

Report your regression as outlined in
`Documentation/admin-guide/reporting-issues.rst`, it already covers all aspects
important for regressions. Below section "How do I report a regression?"
highlights them for convenience.

The most important aspect: CC for forward the report to `the regression mailing
list <https://lore.kernel.org/regressions/>`_ (regressions@lists.linux.dev).
When doing so, consider mentioning the version range where the regression
started using a paragraph like this::

       #regzbot introduced v5.13..v5.14-rc1

The Linux kernel regression tracking bot 'regzbot' will then add the report to
the list of tracked regressions. This is in your interest, as it gets the report
on the radar of people ensuring all regressions are acted upon in timely manner.

The important bits for people fixing regressions
================================================

When getting regression reports by mail, check if the reporter CCed `the
regression mailing list <https://lore.kernel.org/regressions/>`_
(regressions@lists.linux.dev). If not, forward or bounce the report to the Linux
kernel's regression tracker (regressions@leemhuis.info), unless you plan sending
a reply to the report anyway. In that case simply CC the list in a direct reply
to the report. Also check, if the report included a 'regzbot command' like
``#regzbot introduced v5.13..v5.14-rc1`` (see above); if not, please include a
paragraph like the following, to get the regression tracked by the Linux kernel
regression tracking bot 'regzbot'::

       #regzbot ^introduced v5.13..v5.14-rc1

If the report was filed in a public bug-tracker, forward it to the regression
list; add the aforementioned paragraph, just omit the caret (the ^) before the
``introduced``, which make regzbot treat your mail (and not the one you reply
to) as the report.

When submitting fixes for regressions, always include 'Link:' tags in the commit
message that point to all places where the issue was reported, as explained in
`Documentation/process/submitting-patches.rst` and
:ref:`Documentation/process/5.Posting.rst <development_posting>`. Hence, link to
any mails in the archive with reports about the issue as well as all bug-tracker
entries::

       Link: https://lore.kernel.org/r/30th.anniversary.repost@klaava.Helsinki.FI/
       Link: https://bugzilla.kernel.org/show_bug.cgi?id=215375

This is important for regression tracking, as this allows regzbot to
automatically associate tracked regression reports with patch postings and
commits fixing it.


All the details on handling Linux kernel regressions
====================================================

The important basics
--------------------

What is a 'regression' and what is the 'no regressions rule'?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It's a regression if some application or practical use case running fine on one
Linux kernel works worse or not at all with a newer version compiled using a
similar configuration. The 'no regressions rule' forbids this to happen. If a
regression happens by accident, developers that caused it are expected to
quickly fix the issue.

It thus is a regression when a WiFi driver from Linux 5.13 works fine, but with
5.14 doesn't work at all, works significantly slower, or misbehaves somehow.
It's also a regression if a perfectly working application suddenly shows erratic
behavior with a newer kernel version, which can be caused by changes in procfs,
sysfs, or one of the many other interfaces Linux provides to userland software.
But keep in mind, as mentioned earlier: 5.14 in this example needs to be build
from a configuration similar to the one from 5.13. This can be achieved using
``make olddefconfig``, as explained in more detail below.

Note the 'practical use case' in the first sentence of this section: developers
despite the 'no regressions' rule are free to change any aspect of the kernel
and even APIs or ABIs to userland, as long as no existing application or
use-case breaks.

Also be aware the 'no regressions' rule covers only interfaces the kernel
provides to the userland. It thus does not apply to kernel-internal interfaces
like the module API, which some externally developed drivers use to hook into
the kernel.

What is the goal of the 'no regressions rule'?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Users should feel safe when updating kernel versions and not have to worry
something might break. This is in the interest of the kernel developers to make
updating attractive: they don't want users to stay on stable or longterm Linux
series either abandoned or more than one and a half year old, as `those might
have known problems, security issues, or other aspects already improved in later
versions
<http://www.kroah.com/log/blog/2018/08/24/what-stable-kernel-should-i-use/>`_.

How hard is the rule enforced?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Extraordinarily strict, as can be seen by many mailing list posts from Linux
creator and lead-developer Linus Torvalds, some of which are quoted at the end
of this document.

Exceptions to this rule are extremely rare; in the past developers almost always
turned out to be wrong when they assumed a particular situation was warranting
an exception.

How is the rule enforced?
~~~~~~~~~~~~~~~~~~~~~~~~~

It's the duty of the subsystem maintainers, which are watched and supported by
Linus Torvalds for mainline or stable/longterm tree maintainers like Greg
Kroah-Hartman. All of them are supported by Thorsten Leemhuis: he's acting as
'regressions tracker' for the Linux kernel and trying to ensure all regression
reports are acted upon in timely manner.

The distributed and slightly unstructured nature of the Linux kernel's
development makes tracking regressions hard. That's why Thorsten relies on the
help of his Linux kernel regression tracking robot 'regzbot'. It watches mailing
lists and git trees to semi-automatically associate regression reports to patch
submissions and commits fixing the issue, as this provides all necessary
insights into the fixing progress.

To ensure no regression falls through the cracks, the regression tracker or his
bot need to get aware of every report. That's why you need to get them into the
loop for regressions, as explained in the next section.

How do I report a regression?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Just report the issue as outlined in
`Documentation/admin-guide/reporting-issues.rst`, it already describes the
important points. The following aspects described there are especially relevant
for regressions:

 * When checking for existing reports to join, first check the `archives of the
   Linux regressions mailing list <https://lore.kernel.org/regressions/>`_ and
   `regzbot's web-interface <https://linux-regtracking.leemhuis.info/regzbot/>`_.

 * In your report, mention the last kernel version that worked fine and the
   first broken one. Even better: try to find the commit causing the regression
   using a bisection.

 * Remember to let the Linux regressions mailing list
   (regressions@lists.linux.dev) known about your report:

  * If you report the regression by mail, CC the regressions list.

  * If you report your regression to some bug tracker, forward the filed report
    by mail to the regressions list while CCing the maintainer and the mailing
    list for the subsystem in question.

Additionally, you in both cases should directly get the aforementioned Linux
kernel regression tracking bot into the loop. To do that, include a paragraph
like this in your report to tell the bot when the regression started to happen::

       #regzbot introduced: v5.13..v5.14-rc1

In this example, v5.13 was the last version that worked, while v5.14-rc1 was the
first broken one. The smaller the range, the better, as that makes it easier to
find out what's wrong and who's responsible. That's why you ideally should
perform a bisection to find the commit causing the regression (the 'culprit').
If you did, specify it instead::

       #regzbot introduced: 1f2e3d4c5d

Placing such a 'regzbot command' is in your interest, as it will ensure the
report won't fall through the cracks unnoticed. If you omit this, the Linux
kernel's regressions tracker will take care of telling regzbot about your
regression, as long as you sent a copy to the regressions mailing lists. But the
regression tracker is just one human which sometimes has to rest or occasionally
might even enjoy some time away from computers (as crazy as that might sound).
Relying on this person thus will result in an unnecessary delay before the
regressions becomes mentioned `on the list of tracked and unresolved Linux
kernel regressions <https://linux-regtracking.leemhuis.info/regzbot/>`_ and the
weekly regression reports sent by regzbot. Such delays can result in Linus
Torvalds being unaware of important regressions when deciding between 'continue
development or call this finished by performing a release?'.

How to add a regression to regzbot's tracking somebody else reported?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use your mailers 'Reply-all' function to send a reply where you CC the
regressions list (regressions@lists.linux.dev). In that reply create a new
paragraph with a regzbot command like this::

       #regzbot ^introduced: v5.13..v5.14-rc1

The caret (^) before the 'introduced' makes regzbot treat the parent mail (the
one you reply to) as the report for the regression you want to see tracked.
Instead of a version range you can also specify the commit causing the
regression, as outlined in the previous section.

If the report came in private from a bug tracker, forward it to the list;
include the aforementioned line, just omit the caret (the ^) before the
'introduced'; consider adding a line with the line '#regzbot link: <url>' (see
below) pointing to the place with the initial report.

Alternatively to all the above you can just forward or bounce the report to the
Linux kernel's regression tracker, but consider the downsides already outlined
in the previous section.

Do really all regressions get fixed?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Nearly all of them are, as long as the change causing the regression (the
'culprit commit') gets reliably identified. Some regressions can be fixed
without this, but often it's required.

Who needs to find the commit causing a regression?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It's the reporter's duty to find the culprit, but developers of the affected
subsystem should offer advice and reasonably help where they can.

How can I find the change causing a regression?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Perform a bisection, as roughly outlined in
`Documentation/admin-guide/reporting-issues.rst` and described in more detail by
`Documentation/admin-guide/bug-bisect.rst`. It might sound like a lot of work,
but in many cases finds the culprit relative quickly. If it's hard or
time-consuming to reliably reproduce the issue, consider teaming up with others
affected by the problem to narrow down the search range together.

Who can I ask for advice when it comes to regressions?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Send a mail to the regressions mailing list (regressions@lists.linux.dev) while
CCing the Linux kernel's regression tracker (regressions@leemhuis.info); if the
issue might better be dealt with in private, feel free to omit the list.


More details about regressions relevant for reporters
-----------------------------------------------------

Does a regression need to be fixed, if it can be avoided by updating some other software?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Almost always: yes. If a developer tell you otherwise, ask the regression
tracker for advice as outlined above.

Does it qualify as a regression if a newer kernel works slower or makes the system consumes more energy?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It does, but the difference has to be significant. A five percent slow-down in a
micro-benchmark thus is unlikely to qualify as regression, unless it also
influences the results of a broad benchmark by more than one percent. If in a
doubt, ask for advice.

Is it a regression, if an externally developed kernel module is incompatible with a newer kernel?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No, as the 'no regression' rule is about interfaces and services the Linux
kernel provides to the userland. It thus does not cover building or running
externally developed kernel modules, as they run in kernel-space and use
occasionally changed internal interfaces to hook into the kernel.

How are regressions handled that are caused by a fix for security vulnerability?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In extremely rare situations security issues can't be fixed without causing
regressions; those are given way, as they are the lesser evil in the end.
Luckily this almost always can be avoided, as key developers for the affected
area and often Linus Torvalds himself try very hard to fix security issues
without causing regressions.

If you nevertheless face such a case, check the mailing list archives if people
tried their best to avoid the regression; if in a doubt, ask for advice as
outlined above.

What happens if fixing a regression is impossible without causing another regression?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sadly these things happen, but luckily not very often; if they occur, expert
developers of the affected code area should look into the issue to find a fix
that avoids regressions or at least their impact. If you run into such a
situation you thus do what was outlined already for regressions caused by
security fixes: check earlier discussions if people already tried their best and
ask for advice if in a doubt.

A quick note while at it: these situations could be avoided, if you would
regularly give mainline pre-releases (say v5.15-rc1 or -rc3) from each cycle a
test run. This is best explained by imagining a change integrated between Linux
v5.14 and v5.15-rc1 which causes a regression, but at the same time is a hard
requirement for some other improvement applied for 5.15-rc1. All these changes
often can simply be reverted and the regression thus solved, if someone finds
and reports it before 5.15 is released. A few days or weeks later after the
release this solution might become impossible, if some software starts to rely
on aspects introduced by one of the follow-up changes: reverting all changes
would cause regressions for users of said software and thus out of the question.

A feature I relied on was removed months ago, but I only noticed now. Does that qualify as regression?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It does, but often it's hard to fix them due to the aspects outlined in the
previous section. It hence needs to be dealt with on a case-by-case basis; this
is another reason why it's in your interest to regular test mainline releases.

Does the 'no regression' rule apply if I seem to be the only person in the world that is affected by a regression?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It does, but only for practical usage: the Linux developers want to be free to
remove support for hardware only to be found in attics and museums anymore.

Note, sometimes regressions can't be avoided to make progress -- and the latter
is needed to prevent Linux from stagnation. Hence, if only very few users seem
to be affected by a regression, it for the greater good might be in their and
everyone else interest to not insist on the rule. Especially if there is a easy
way to circumvent the regression somehow, for example by updating some software
or using a kernel parameter created just for this purpose.

Does the regression rule apply for code in the staging tree as well?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Not according to the `help text for the configuration option covering all
staging code <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/staging/Kconfig>`_,
which since its early days states::

       Please note that these drivers are under heavy development, may or
       may not work, and may contain userspace interfaces that most likely
       will be changed in the near future.

The staging developers nevertheless often adhere the 'no regressions' rule, but
sometimes bend it to make progress. That's for example why some users had to
deal with (often negligible) regressions when a WiFi driver from the staging
tree got replaced by a totally different one written from scratch.

Why do later versions have to be 'compiled with a similar configuration'?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Because the Linux kernel developers sometimes integrate changes known to cause
regressions, but make them optional and disable them in the kernel's default
configuration. This trick allows progress, as the 'no regressions' rule
otherwise would lead to stagnation. Consider for example a new security feature
which blocks access to some kernel interfaces often abused by malware, but at
the same time are required to run a few rarely used applications. The outlined
trick makes both camps happy: people using these applications can leave the new
security feature off, while everyone else can enable it without running into
trouble.

How to create a configuration similar to the one of an older kernel?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start a known-good kernel and configure the newer Linux version with ``make
olddefconfig``. This makes the kernel's build scripts pick up the configuration
file (the `.config` file) from the running kernel as base for the new one you
are about to compile; afterwards they set all new configuration options to their
default value, which disables new features that might cause regressions.

Can I report a regression with vanilla kernels provided by someone else to the upstream Linux kernel developers?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Only if the newer kernel was compiled with a similar configuration file as the
older one (see above), as your provider might have enabled some known-to-be
incompatible feature in the newer kernel. If in a doubt, report this problem to
the provider and ask for advice.


More details about regressions relevant for developers
------------------------------------------------------

What should I do, if I suspect a change I'm working on might cause regressions?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Evaluate how big the risk of regressions is, for example by performing a code
search in Linux distributions and Git forges. Also consider asking other
developers or projects likely to be affected to evaluate or even test the
proposed change; if problems surface, maybe some middle ground acceptable for
all can be found.

If the risk of regressions in the end seems to be relative small, go ahead with
the change, but let all involved parties know about the risk. Hence, make sure
your patch description makes this aspect obvious. Once the change got merged,
tell the Linux kernel's regression tracker and the regressions mailing list
about the risk, so everyone has the change on the radar in case reports trickle
in. Depending on the risk, you also might want to ask the subsystem maintainer
to mention the issue in his pull request to mainline.


Everything developers need to know about regression tracking
------------------------------------------------------------

Do I have to use regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~

It's in the interest of everyone if you do, as kernel maintainers like Linus
Torvalds partly rely on regzbot's tracking in their work -- for example when
deciding to release a new version or extend the development phase. For this they
need to be aware of all unfixed regression; to do that, Linus is known to look
into the weekly reports sent by regzbot.

Do I have to tell regzbot about every regression I stumble upon?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Ideally yes: we are all humans and easily forget problems when something more
important unexpectedly comes up -- for example a bigger problem in the Linux
kernel or something in real life that's keeping us away from keyboards for a
while. Hence, it's best to tell regzbot about every regression, except when you
immediately write a fix and commit it to a tree regularly merged to the affected
kernel series.

Why does the Linux kernel need a regression tracker, and why does he utilize regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rules like 'no regressions' need someone to enforce them, otherwise they are
broken either accidentally or on purpose. History has shown that this is true
for the Linux kernel as well. That's why Thorsten volunteered to keep an eye on
things.

Tracking regressions completely manually has proven to be exhausting and
demotivating, which is why earlier attempts to establish it failed after a
while. To prevent this from happening again, Thorsten developed Regzbot to
facilitate the work, with the long term goal to automate regression tracking as
much as possible for everyone involved.

How does regression tracking work with regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The bot keeps track of all the reports and monitor their fixing progress. It
tries to do that with as little overhead as possible for both reporters and
developers.

In fact, only reporters or someone helping them gets an extra duty: they need to
tell regzbot about the regression report using one of the ``#regzbot
introduced`` commands outlined above.

For developers there normally is no extra work involved, they just need to do
something that's expected from them already: add 'Link:' tags to the patch
description pointing to all reports about the issue fixed.

Thanks to these tags regzbot can associate regression reports with patches to
fix the issue, whenever they get posted for review or applied to a git tree. The
bot additionally watches out for replies to the report. All this data combined
provides a good impression about the current status of the fixing process.

How to see which regressions regzbot tracks currently?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Check `regzbot's web-interface <https://linux-regtracking.leemhuis.info/regzbot/>`_
for the latest info; alternatively, `search for the latest regression report
<https://lore.kernel.org/lkml/?q=%22Linux+regressions+report%22+f%3Aregzbot>`_,
which regzbot normally sends out once a week on Sunday evening (UTC), which is a
few hours before Linus usually publishes new (pre-)releases.

What places is regzbot monitoring?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Regzbot is watching the most important Linux mailing lists as well as the Linux
next, mainline and stable/longterm git repositories.

How to interact with regzbot?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Everyone can interact with the bot using mails containing `regzbot commands`,
which need to be in their own paragraph (IOW: they need to be separated from the
rest of the mail using blank lines). One such command is ``#regzbot introduced
<version or commit>``, which adds a report to the tracking, as already described
above; ``#regzbot ^introduced <version or commit>`` is another such command,
which makes regzbot consider the parent mail as a report for a regression which
it starts to track.

Once one of those two commands has been utilized, other regzbot commands can be
used. You can write them below one of the `introduced` commands or in replies to
the mail that used one of them or itself is a reply to that mail:

 * Set or update the title::

       #regzbot title: foo

 * Link to a related discussion (for example the posting of a patch to fix the
   issue) and monitor it::

       #regzbot monitor: https://lore.kernel.org/all/30th.anniversary.repost@klaava.Helsinki.FI/

   Monitoring only works for lore.kernel.org; regzbot will consider all messages
   in that thread as related to the fixing process.

 * Point to a place with further details, like a bug-tracker or a related
   mailing list post::

       #regzbot link: https://bugzilla.kernel.org/show_bug.cgi?id=123456789

 * Mark a regression as fixed by a commit that is heading upstream or already
   landed::

       #regzbot fixed-by: 1f2e3d4c5d

 * Mark a regression as a duplicate of another one already tracked by regzbot::

       #regzbot dup-of: https://lore.kernel.org/all/30th.anniversary.repost@klaava.Helsinki.FI/

 * Mark a regression as invalid::

       #regzbot invalid: wasn't a regression, problem has always existed

Is there more to tell about regzbot and its commands?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

More detailed and up-to-date information about the Linux kernels regression
tracking bot can be found on its `project page <https://gitlab.com/knurd42/regzbot>`_,
which among others contains a
`getting started guide <https://gitlab.com/knurd42/regzbot/-/blob/main/docs/getting_started.md>`_
and `reference documentation <https://gitlab.com/knurd42/regzbot/-/blob/main/docs/reference.md>`_
which both are more in-depth.


Quotes from Linus about regression
----------------------------------

Find below a few real life examples of how Linus Torvalds expects regressions to
be handled:

 * From `2017-10-26 (1/2) <https://lore.kernel.org/lkml/CA+55aFwiiQYJ+YoLKCXjN_beDVfu38mg=Ggg5LFOcqHE8Qi7Zw@mail.gmail.com/>`_::

       If you break existing user space setups THAT IS A REGRESSION.

       It's not ok to say "but we'll fix the user space setup".

       Really. NOT OK.

       [...]

       The first rule is:

        - we don't cause regressions

       and the corollary is that when regressions *do* occur, we admit to
       them and fix them, instead of blaming user space.

       The fact that you have apparently been denying the regression now for
       three weeks means that I will revert, and I will stop pulling apparmor
       requests until the people involved understand how kernel development
       is done.

 * From `2017-10-26 (2/2) <https://lore.kernel.org/lkml/CA+55aFxW7NMAMvYhkvz1UPbUTUJewRt6Yb51QAx5RtrWOwjebg@mail.gmail.com/>`_::

       People should basically always feel like they can update their kernel
       and simply not have to worry about it.

       I refuse to introduce "you can only update the kernel if you also
       update that other program" kind of limitations. If the kernel used to
       work for you, the rule is that it continues to work for you.

       There have been exceptions, but they are few and far between, and they
       generally have some major and fundamental reasons for having happened,
       that were basically entirely unavoidable, and people _tried_hard_ to
       avoid them. Maybe we can't practically support the hardware any more
       after it is decades old and nobody uses it with modern kernels any
       more. Maybe there's a serious security issue with how we did things,
       and people actually depended on that fundamentally broken model. Maybe
       there was some fundamental other breakage that just _had_ to have a
       flag day for very core and fundamental reasons.

       And notice that this is very much about *breaking* peoples environments.

       Behavioral changes happen, and maybe we don't even support some
       feature any more. There's a number of fields in /proc/<pid>/stat that
       are printed out as zeroes, simply because they don't even *exist* in
       the kernel any more, or because showing them was a mistake (typically
       an information leak). But the numbers got replaced by zeroes, so that
       the code that used to parse the fields still works. The user might not
       see everything they used to see, and so behavior is clearly different,
       but things still _work_, even if they might no longer show sensitive
       (or no longer relevant) information.

       But if something actually breaks, then the change must get fixed or
       reverted. And it gets fixed in the *kernel*. Not by saying "well, fix
       your user space then". It was a kernel change that exposed the
       problem, it needs to be the kernel that corrects for it, because we
       have a "upgrade in place" model. We don't have a "upgrade with new
       user space".

       And I seriously will refuse to take code from people who do not
       understand and honor this very simple rule.

       This rule is also not going to change.

       And yes, I realize that the kernel is "special" in this respect. I'm
       proud of it.

       I have seen, and can point to, lots of projects that go "We need to
       break that use case in order to make progress" or "you relied on
       undocumented behavior, it sucks to be you" or "there's a better way to
       do what you want to do, and you have to change to that new better
       way", and I simply don't think that's acceptable outside of very early
       alpha releases that have experimental users that know what they signed
       up for. The kernel hasn't been in that situation for the last two
       decades.

       We do API breakage _inside_ the kernel all the time. We will fix
       internal problems by saying "you now need to do XYZ", but then it's
       about internal kernel API's, and the people who do that then also
       obviously have to fix up all the in-kernel users of that API. Nobody
       can say "I now broke the API you used, and now _you_ need to fix it
       up". Whoever broke something gets to fix it too.

       And we simply do not break user space.

 * From `2020-05-21 <https://lore.kernel.org/all/CAHk-=wiVi7mSrsMP=fLXQrXK_UimybW=ziLOwSzFTtoXUacWVQ@mail.gmail.com/>`_::

       The rules about regressions have never been about any kind of
       documented behavior, or where the code lives.

       The rules about regressions are always about "breaks user workflow".

       Users are literally the _only_ thing that matters.

       No amount of "you shouldn't have used this" or "that behavior was
       undefined, it's your own fault your app broke" or "that used to work
       simply because of a kernel bug" is at all relevant.

       Now, reality is never entirely black-and-white. So we've had things
       like "serious security issue" etc that just forces us to make changes
       that may break user space. But even then the rule is that we don't
       really have other options that would allow things to continue.

       And obviously, if users take years to even notice that something
       broke, or if we have sane ways to work around the breakage that
       doesn't make for too much trouble for users (ie "ok, there are a
       handful of users, and they can use a kernel command line to work
       around it" kind of things) we've also been a bit less strict.

       But no, "that was documented to be broken" (whether it's because the
       code was in staging or because the man-page said something else) is
       irrelevant. If staging code is so useful that people end up using it,
       that means that it's basically regular kernel code with a flag saying
       "please clean this up".

       The other side of the coin is that people who talk about "API
       stability" are entirely wrong. API's don't matter either. You can make
       any changes to an API you like - as long as nobody notices.

       Again, the regression rule is not about documentation, not about
       API's, and not about the phase of the moon.

       It's entirely about "we caused problems for user space that used to work".

 * From `2012-07-06 <https://lore.kernel.org/all/CA+55aFwnLJ+0sjx92EGREGTWOx84wwKaraSzpTNJwPVV8edw8g@mail.gmail.com/>`_::

       > Now this got me wondering if Debian _unstable_ actually qualifies as a
       > standard distro userspace.

       Oh, if the kernel breaks some standard user space, that counts. Tons
       of people run Debian unstable (and from my limited interactions with
       it, for damn good reasons: -stable tends to run so old versions of
       everything that you have to sometimes deal with cuneiform writing when
       using it)

 * From `2017-11-05 <https://lore.kernel.org/all/CA+55aFzUvbGjD8nQ-+3oiMBx14c_6zOj2n7KLN3UsJ-qsd4Dcw@mail.gmail.com/>`_::

       And our regression rule has never been "behavior doesn't change".
       That would mean that we could never make any changes at all.

       For example, we do things like add new error handling etc all the
       time, which we then sometimes even add tests for in our kselftest
       directory.

       So clearly behavior changes all the time and we don't consider that a
       regression per se.

       The rule for a regression for the kernel is that some real user
       workflow breaks. Not some test. Not a "look, I used to be able to do
       X, now I can't".

 * From `2018-08-03 <https://lore.kernel.org/all/CA+55aFwWZX=CXmWDTkDGb36kf12XmTehmQjbiMPCqCRG2hi9kw@mail.gmail.com/>`_::

       YOU ARE MISSING THE #1 KERNEL RULE.

       We do not regress, and we do not regress exactly because your are 100% wrong.

       And the reason you state for your opinion is in fact exactly *WHY* you
       are wrong.

       Your "good reasons" are pure and utter garbage.

       The whole point of "we do not regress" is so that people can upgrade
       the kernel and never have to worry about it.

       > Kernel had a bug which has been fixed

       That is *ENTIRELY* immaterial.

       Guys, whether something was buggy or not DOES NOT MATTER.

       Why?

       Bugs happen. That's a fact of life. Arguing that "we had to break
       something because we were fixing a bug" is completely insane. We fix
       tens of bugs every single day, thinking that "fixing a bug" means that
       we can break something is simply NOT TRUE.

       So bugs simply aren't even relevant to the discussion. They happen,
       they get found, they get fixed, and it has nothing to do with "we
       break users".

       Because the only thing that matters IS THE USER.

       How hard is that to understand?

       Anybody who uses "but it was buggy" as an argument is entirely missing
       the point. As far as the USER was concerned, it wasn't buggy - it
       worked for him/her.

       Maybe it worked *because* the user had taken the bug into account,
       maybe it worked because the user didn't notice - again, it doesn't
       matter. It worked for the user.

       Breaking a user workflow for a "bug" is absolutely the WORST reason
       for breakage you can imagine.

       It's basically saying "I took something that worked, and I broke it,
       but now it's better". Do you not see how f*cking insane that statement
       is?

       And without users, your program is not a program, it's a pointless
       piece of code that you might as well throw away.

       Seriously. This is *why* the #1 rule for kernel development is "we
       don't break users". Because "I fixed a bug" is absolutely NOT AN
       ARGUMENT if that bug fix broke a user setup. You actually introduced a
       MUCH BIGGER bug by "fixing" something that the user clearly didn't
       even care about.

       And dammit, we upgrade the kernel ALL THE TIME without upgrading any
       other programs at all. It is absolutely required, because flag-days
       and dependencies are horribly bad.

       And it is also required simply because I as a kernel developer do not
       upgrade random other tools that I don't even care about as I develop
       the kernel, and I want any of my users to feel safe doing the same
       time.

       So no. Your rule is COMPLETELY wrong. If you cannot upgrade a kernel
       without upgrading some other random binary, then we have a problem.

 * From `2021-06-05 <https://lore.kernel.org/all/CAHk-=wiUVqHN76YUwhkjZzwTdjMMJf_zN4+u7vEJjmEGh3recw@mail.gmail.com/>`_::

       THERE ARE NO VALID ARGUMENTS FOR REGRESSIONS.

       Honestly, security people need to understand that "not working" is not
       a success case of security. It's a failure case.

       Yes, "not working" may be secure. But security in that case is *pointless*.

 * From `2021-07-30 <https://lore.kernel.org/lkml/CAHk-=witY33b-vqqp=ApqyoFDpx9p+n4PwG9N-TvF8bq7-tsHw@mail.gmail.com/>`_::

       But we have the policy that regressions aren't about documentation or
       even sane behavior.

       Regressions are about whether a user application broke in a noticeable way.

 * From `2011-05-06 (1/3) <https://lore.kernel.org/all/BANLkTim9YvResB+PwRp7QTK-a5VNg2PvmQ@mail.gmail.com/>`_::

       Binary compatibility is more important.

       And if binaries don't use the interface to parse the format (or just
       parse it wrongly - see the fairly recent example of adding uuid's to
       /proc/self/mountinfo), then it's a regression.

       And regressions get reverted, unless there are security issues or
       similar that makes us go "Oh Gods, we really have to break things".

       I don't understand why this simple logic is so hard for some kernel
       developers to understand. Reality matters. Your personal wishes matter
       NOT AT ALL.

       If you made an interface that can be used without parsing the
       interface description, then we're stuck with the interface. Theory
       simply doesn't matter.

       You could help fix the tools, and try to avoid the compatibility
       issues that way. There aren't that many of them.

 * From `2011-05-06 (2/3) <https://lore.kernel.org/all/BANLkTi=KVXjKR82sqsz4gwjr+E0vtqCmvA@mail.gmail.com/>`_::

       it's clearly NOT an internal tracepoint. By definition. It's being
       used by powertop.

 * From `2011-05-06 (3/3) <https://lore.kernel.org/all/BANLkTinazaXRdGovYL7rRVp+j6HbJ7pzhg@mail.gmail.com/>`_::

       We have programs that use that ABI and thus it's a regression if they break.

 * From `2006-02-21 <https://lore.kernel.org/lkml/Pine.LNX.4.64.0602211631310.30245@g5.osdl.org/>`_::

       The fact is, if changing the kernel breaks user-space, it's a regression.
       IT DOES NOT MATTER WHETHER IT'S IN /sbin/hotplug OR ANYTHING ELSE. If it
       was installed by a distribution, it's user-space. If it got installed by
       "vmlinux", it's the kernel.

       The only piece of user-space code we ship with the kernel is the system
       call trampoline etc that the kernel sets up. THOSE interfaces we can
       really change, because it changes with the kernel.

 * From `2019-09-15 <https://lore.kernel.org/lkml/CAHk-=wiP4K8DRJWsCo=20hn_6054xBamGKF2kPgUzpB5aMaofA@mail.gmail.com/>`_::

       One _particularly_ last-minute revert is the top-most commit (ignoring
       the version change itself) done just before the release, and while
       it's very annoying, it's perhaps also instructive.

       What's instructive about it is that I reverted a commit that wasn't
       actually buggy. In fact, it was doing exactly what it set out to do,
       and did it very well. In fact it did it _so_ well that the much
       improved IO patterns it caused then ended up revealing a user-visible
       regression due to a real bug in a completely unrelated area.

       The actual details of that regression are not the reason I point that
       revert out as instructive, though. It's more that it's an instructive
       example of what counts as a regression, and what the whole "no
       regressions" kernel rule means. The reverted commit didn't change any
       API's, and it didn't introduce any new bugs. But it ended up exposing
       another problem, and as such caused a kernel upgrade to fail for a
       user. So it got reverted.

       The point here being that we revert based on user-reported _behavior_,
       not based on some "it changes the ABI" or "it caused a bug" concept.
       The problem was really pre-existing, and it just didn't happen to
       trigger before. The better IO patterns introduced by the change just
       happened to expose an old bug, and people had grown to depend on the
       previously benign behavior of that old issue.

       And never fear, we'll re-introduce the fix that improved on the IO
       patterns once we've decided just how to handle the fact that we had a
       bad interaction with an interface that people had then just happened
       to rely on incidental behavior for before. It's just that we'll have
       to hash through how to do that (there are no less than three different
       patches by three different developers being discussed, and there might
       be more coming...). In the meantime, I reverted the thing that exposed
       the problem to users for this release, even if I hope it will be
       re-introduced (perhaps even backported as a stable patch) once we have
       consensus about the issue it exposed.

       Take-away from the whole thing: it's not about whether you change the
       kernel-userspace ABI, or fix a bug, or about whether the old code
       "should never have worked in the first place". It's about whether
       something breaks existing users' workflow.

       Anyway, that was my little aside on the whole regression thing.  Since
       it's that "first rule of kernel programming", I felt it is perhaps
       worth just bringing it up every once in a while.
