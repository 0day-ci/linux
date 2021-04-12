#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
#
# Kernel Documentation Translation File tool
# Document see: Documentation/doc-guide/trslt.rst
#
# Wu XiangCheng <bobwxc@email.cn>, 2021.

import os
import argparse
import subprocess

# global verbose mode flag
VERBOSE_FLAG = False

# change to source root dir
def cdpath():
    # at source ROOT
    if os.path.isdir("Documentation/translations") and os.path.isfile("MAINTAINERS"):
        return 0
    # at Documentation/
    elif os.path.isdir("translations") and os.path.isfile("../MAINTAINERS"):
        os.chdir("../")
        return 0
    # at Documentation/translation/
    elif os.path.isdir("zh_CN") and os.path.isfile("../../MAINTAINERS"):
        os.chdir("../../")
        return 0
    # at Documentation/translations/ll_NN/
    elif os.path.isdir("translations") == False and os.path.isdir("../../translations") and os.path.isfile("../../../MAINTAINERS"):
        os.chdir("../../../")
        return 0
    # anywhere else
    else:
        print("ERROR: Please run this script under linux kernel source ROOT dir")
        return -1

# argv
def arg():
    parser = argparse.ArgumentParser(
        description='Linux Kernel Documentation Translation File Tool')
    # file path
    parser.add_argument('file', help="specific file path")
    # verbose mode
    parser.add_argument('-v', '--verbose',
                        help="enable verbose mode",
                        action='store_true')
    # language choose
    parser.add_argument('-l', '--language',
                        help="choose translation language, default: zh_CN",
                        type=str,
                        choices=["it_IT", "ja_JP", "ko_KR", "zh_CN"],
                        default="zh_CN")
    # required action group
    ch = parser.add_mutually_exclusive_group(required=True)
    # \_ copy
    ch.add_argument('-c', '--copy',
                    help="copy a origin file to translation directory",
                    action='store_true')
    # \_ update
    ch.add_argument('-u', '--update',
                    help="get a translation file's update information",
                    action='store_true')

    argv_ = parser.parse_args()

    # modify global VERBOSE_FLAG
    if argv_.verbose:
        global VERBOSE_FLAG
        VERBOSE_FLAG = True
        print(argv_)

    return argv_

# get newest commit id of a origin doc file
def get_newest_commit(fp):
    cmd = "git log --format=oneline --no-merges "+fp
    p = subprocess.Popen(cmd,
                         shell=True,
                         stdout=subprocess.PIPE,
                         errors="replace")
    log = p.stdout.readline()
    commit_id = log[:log.find(' ')]
    return commit_id

# add language special header
def la_head(fp, la):
    if la == "zh_CN":
        cfp = fp[0:14]+"translations/"+la+'/'+fp[14:]
        r = ".. include:: " + \
            os.path.relpath(
                "Documentation/translations/zh_CN/disclaimer-zh_CN.rst",
                cfp[0:cfp.rfind('/')]) + "\n\n"
        r += ":Original: "+fp+"\n\n"
        r += ".. translation_origin_commit: "+get_newest_commit(fp)+"\n\n"
        r += ":译者: 姓名 EnglishName <email@example.com>\n\n"
    else:
        r = ":Original: "+fp+"\n\n"
        r += ".. translation_origin_commit: "+get_newest_commit(fp)+"\n\n"
        r += ":Translator: Name <email@example.com>\n\n"

    return r

# copy mode
def copy(fp, la):
    if os.path.isfile(fp) == False:
        return -2

    if fp.find("/translations/") != fp.rfind("/translations/"):
        print("WARNING: seems you are copying a file only exist in translations/ dir")
        return -3

    f = open(fp, 'r')
    try:
        first = f.read(2048)
    except:
        print("ERROR: can not read file", fp)
        return -2

    spdx_id = first.find(".. SPDX-License-Identifier: ")
    if spdx_id != -1:
        insert_id = first.find('\n', spdx_id)+1
        first = first[:insert_id]+'\n'+la_head(fp, la)+first[insert_id:]
    else:
        first = la_head(fp, la)+first

    if fp[0:14] == "Documentation/":
        cfp = fp[0:14]+"translations/"+la+'/'+fp[14:]

        if cfp[cfp.rfind('.'):] != ".rst":
            print("WARNING: this is not a rst file, may cause problems.",
                  "copy will continue, but please \033[31mcheck it!\033[0m")

        cfp_dir = cfp[0:cfp.rfind('/')]

        if not os.path.exists(cfp_dir):
            os.makedirs(cfp_dir)

        if os.path.isfile(cfp):
            print("WARNING:\033[31m", cfp,
                  "\033[0mis existing, can not use copy, please try -u/--update!")
            return -3

        cf = open(cfp, 'w')
        cf.write(first)

        while True:
            a = f.read(2048)
            if a != '':
                cf.write(a)
            else:
                break

        cf.close()
        print("INFO: \033[32m" + cfp +
              "\033[0m has been created, please remember to edit it.")
    else:
        return -2

    return 0

# generete origin text diff file for update
def gen_diff(ofp, old_id):
    new_id = get_newest_commit(ofp)
    if old_id == new_id:
        return 1

    cmd = "git show "+old_id+".."+new_id+" "+ofp
    p = subprocess.Popen(cmd,
                         shell=True,
                         stdout=subprocess.PIPE,
                         errors="replace")
    log = p.stdout.read()
    log = cmd+"\n\n"+log
    return log

# update mode
def update(fp, la):
    if os.path.isfile(fp) == False:
        return -2
    if fp.find("Documentation/translations/"+la) == -1:
        print("ERROR:", fp, "does not belong to", la, "translation!")
        return -3

    # origin file path
    ofp = fp[:fp.find("translations/"+la)] + \
        fp[fp.find("translations/"+la)+14+len(la):]

    if not os.path.isfile(ofp):
        print("ERROR: origin file",ofp,"does not exist or not a file")
        return -2

    f = open(fp, 'r')
    try:
        first = f.read(3072)
    except:
        print("ERROR: can not read file", fp)
        return -2

    commit_id = first.find("\n.. translation_origin_commit: ")
    if commit_id == -1:
        print("WARNING:", fp, "\033[31mdoes not have a translation_origin_commit tag,",
              "can not generate a diff file\033[0m, please add a tag if you want to update it.")
        print("\n\033[33m.. translation_origin_commit: " +
              get_newest_commit(ofp) + "\033[0m")
        return -4
    else:
        commit_id = commit_id+1  # '\n'
        commit_id = first[commit_id:first.find('\n', commit_id)]
        commit_id = commit_id[commit_id.find(' ')+1:]
        commit_id = commit_id[commit_id.find(' ')+1:]

    diff = gen_diff(ofp, commit_id)
    if diff == 1:
        print("INFO:", ofp, "does not have any change since", commit_id)
    else:
        with open(fp+".diff", 'w') as d:
            d.write(diff)
        print("INFO: \033[32m"+fp+".diff\033[0m file has generated",)
        print("INFO: if you want to update " + fp +
              ", please \033[31mDo Not Forget\033[0m to update the translation_origin_commit tag.",
              "\n\n\033[33m.. translation_origin_commit: " +
              get_newest_commit(ofp) + "\033[0m")

    return 0

# main entry
def main():
    argv_ = arg()

    # get file's abspath before cdpath
    file_path = os.path.abspath(argv_.file)
    if VERBOSE_FLAG:
        print(file_path)

    if cdpath() != 0:
        return -1

    # if file_path valid
    if file_path.find("Documentation") == -1:
        print("ERROR: file does not in Linux Kernel source Documentation")
        return -2
    elif os.path.isfile(file_path[file_path.find("Documentation"):]) == False:
        print("ERROR: file does not exist or not a file")
        return -2
    else:
        file_path = file_path[file_path.find("Documentation"):]

        if VERBOSE_FLAG:
            print(file_path)

    if argv_.copy:
        return copy(file_path, argv_.language)
    elif argv_.update:
        return update(file_path, argv_.language)

    return 0


if __name__ == "__main__":
    exit_code = main()
    if VERBOSE_FLAG:
        if exit_code == 0:
            print("exit with code:\033[32m", exit_code, "\033[0m")
        else:
            print("exit with code:\033[31m", exit_code, "\033[0m")
    exit(exit_code)
