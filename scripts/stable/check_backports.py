#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 Google, Inc.

import os
import re
import sys

import pygit2 as pg


def get_head_branch(repo):
    # Walk the branches to find which is HEAD.
    for branch_name in repo.branches:
        branch = repo.branches[branch_name]
        if branch.is_head():
            return branch


def get_local_commits(repo):
    head_branch = get_head_branch(repo)
    # Walk the HEAD ref until we hit the first commit from the upstream.
    walker = repo.walk(repo.head.target)
    upstream_branch = head_branch.upstream
    upstream_commit, _ = repo.resolve_refish(upstream_branch.name)
    walker.hide(upstream_commit.id)
    commits = [commit for commit in walker]
    if not len(commits):
        raise Exception("No local commits")
    return commits


def get_upstream_shas(commits):
    upstream_shas = []
    prog = re.compile('commit ([0-9a-f]{40}) upstream.')
    # For each line of each commit message, record the
    # "commit <sha40> upstream." line.
    for commit in commits:
        found_upstream_line = False
        for line in commit.message.splitlines():
            result = prog.search(line)
            if result:
                upstream_shas.append(result.group(1)[:12])
                found_upstream_line = True
                break
        if not found_upstream_line:
            raise Exception("Missing 'commit <sha40> upstream.' line")
    return upstream_shas


def get_oldest_commit_time(repo, shas):
    commit_times = [repo.resolve_refish(sha)[0].commit_time for sha in shas]
    return sorted(commit_times)[0]


def get_fixes_for(shas):
    shas = set(shas)
    prog = re.compile("Fixes: ([0-9a-f]{12,40})")
    # Walk commits in the master branch.
    master_commit, master_ref = repo.resolve_refish("master")
    walker = repo.walk(master_ref.target)
    oldest_commit_time = get_oldest_commit_time(repo, shas)
    fixes = []
    for commit in walker:
        # It's not possible for a Fixes: to be committed before a fixed tag, so
        # don't iterate all of git history.
        if commit.commit_time < oldest_commit_time:
            break
        for line in reversed(commit.message.splitlines()):
            result = prog.search(line)
            if not result:
                continue
            fixes_sha = result.group(1)[:12]
            if fixes_sha in shas and commit.id.hex[:12] not in shas:
                fixes.append((commit.id.hex[:12], fixes_sha))
    return fixes


def report(fixes):
    if len(fixes):
        for fix, broke in fixes:
            print("Please consider backporting %s as a fix for %s" % (fix, broke))
        sys.exit(1)


if __name__ == "__main__":
    repo = pg.Repository(os.getcwd())
    commits = get_local_commits(repo)
    print("Checking %d local commits for additional Fixes: in master" % (len(commits)))
    upstream_shas = get_upstream_shas(commits)
    fixes = get_fixes_for(upstream_shas)
    report(fixes)
