.. SPDX-License-Identifier: GPL-2.0+

.. _trslt:

===========================================
Kernel Documentation Translation File tool
===========================================

:Author: Wu XiangCheng <bobwxc@email.cn>

This document is for ``scripts/trslt.py``.

Motivation
-----------

For a long time, kernel documentation translations lacks a way to control the
version corresponding to the source files. If you translate a file and then
someone updates the source file, there will be a problem. It's hard to know
which version the existing translation corresponds to, and even harder to sync
them. The common way now is to check the date, but this is not exactly accurate,
especially for documents that are often updated.

So, some translators write corresponding commit ID in the commit log for
reference, it is a good way, but still a little troublesome.

Thus, the purpose of ``trslt.py`` is to add a new annotating tag to the file to
indicate the corresponding version of the source file::

	.. translation_origin_commit: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

The script will automatically copy file and generate tag when creating new
translation, and give update suggestions based on those tags when updating
translations.

Dependency
-----------

:Language: Python 3.x

:Python Libraries:

 os

 argparse

 subprocess

Usage
------

``trslt.py`` comes with a help message:: 

	➜ scripts/trslt.py -h                                                         
	usage: trslt.py [-h] [-v] [-l {it_IT,ja_JP,ko_KR,zh_CN}] (-c | -u) file
	
	Linux Kernel Documentation Translation File Tool
	
	positional arguments:
	  file                  specific file path
	
	optional arguments:
	  -h, --help            show this help message and exit
	  -v, --verbose         enable verbose mode
	  -l {it_IT,ja_JP,ko_KR,zh_CN}, --language {it_IT,ja_JP,ko_KR,zh_CN}
	                        choose translation language, default: zh_CN
	  -c, --copy            copy a origin file to translation directory
	  -u, --update          get a translation file's update information

We could learn some basic operation methods from this help message. See below
for details.

.. note::

	``trslt.py`` should be called in Linux kernel source **ROOT** directory or 
	"Documentation/", "Documentation/translations/", "Documentation/translations/ll_NN/".
	Anyway, don't worry, it will remind you when using a wrong directory.

Verbose mode
~~~~~~~~~~~~~

``-v, --verbose``

As its name said, ``-v`` is used to turn on the verbose mode. Then will show
more informations, something is better than nothing.


Choose language
~~~~~~~~~~~~~~~~

``-l, --language``

As a translator, you need to select the language you prefer. And this script 
also need to decide which language directory should be used.

Simply give the language after ``-l``, like ``-l zh_CN``. If you do not give
a choice, default is ``zh_CN``. 

Now, we have four langugue(it_IT,ja_JP,ko_KR,zh_CN) to use, if you need others,
please feel free to add it, only need to modify language choice list in
``arg()`` of ``trslt.py`` and this document.

Copy mode
~~~~~~~~~~

``-c, --copy``

This action is used to copy a origin file to translation directory. If the file
is existing, it will give a warning::

	➜ scripts/trslt.py -c Documentation/admin-guide/perf-security.rst 
	INFO: Documentation/translations/zh_CN/admin-guide/perf-security.rst has been created, please remember to edit it.

	➜ scripts/trslt.py -c Documentation/admin-guide/perf-security.rst          
	WARNING: Documentation/translations/zh_CN/admin-guide/perf-security.rst is existing, can not use copy, please try -u/--update!

Also, it will auto add a commit-id tag and language special header::

	:Original: Documentation/admin-guide/perf-security.rst

	.. translation_origin_commit: a15cb2c1658417f9e8c7e84fe5d6ee0b63cbb9b0

	:Translator: Name <email@example.com>

The header could be used to include a unified declaration or localization tag.
If you need a special header for your language, please modify ``la_head(fp, la)``
in ``trslt.py``, simply add a ``elif`` condition.


Update mode
~~~~~~~~~~~~

``-u, --update``

This action is used to update a existing translation file. The translation file
must have a commit-id tag for generating origin text diff file. If there is no
commit-id tag or no need to update, it will remind you::

	➜ scripts/trslt.py -u Documentation/translations/zh_CN/admin-guide/perf-security.rst
	INFO: Documentation/translations/zh_CN/admin-guide/perf-security.rst.diff file has generated
	INFO: if you want to update Documentation/translations/zh_CN/admin-guide/perf-security.rst, please Do Not Forget to update the translation_origin_commit tag. 

	.. translation_origin_commit: a15cb2c1658417f9e8c7e84fe5d6ee0b63cbb9b0

	➜ scripts/trslt.py -u Documentation/translations/zh_CN/admin-guide/perf-security.rst
	INFO: Documentation/admin-guide/perf-security.rst does not have any change since a15cb2c1658417f9e8c7e84fe5d6ee0b63cbb9b0

	➜ scripts/trslt.py -u Documentation/translations/zh_CN/admin-guide/index.rst 
	WARNING: Documentation/translations/zh_CN/admin-guide/index.rst does not have a translation_origin_commit tag, can not generate a diff file, please add a tag if you want to update it.

	.. translation_origin_commit: da514157c4f063527204adc8e9642a18a77fccc9

.. important::

	Please note, this action will auto generate a diff file, but it **will not
	automatically add or change the commit-id**, only print it, you need to add
	or change it by yourself!

Workflow
----------

Describes two common workflows — start new and update existing.

Start a new translation
~~~~~~~~~~~~~~~~~~~~~~~~

To start a new translation, please use ``-c`` action::

	➜ scripts/trslt.py -c Documentation/any-file

If it's ok, translation file created successfully::

	INFO: Documentation/translations/ll_NN/any-file has been created, please remember to edit it.

Then you can start translation work.

Or, get a warning::

	WARNING: Documentation/translations/ll_NN/any-file is existing, can not use copy, please try -u/--update!

	WARNING: seems you are copying a file only exist in translations/ dir

Or, get a error::

	ERROR: file does not in Linux Kernel source Documentation

Update a existing translation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To update a existing translation, please use ``-u`` action::

	➜ scripts/trslt.py -u Documentation/translations/ll_NN/any-file

If everything is ok, script will generate a diff file of origin text from the 
commit-id tag's id to newest, and print the newset commit-id tag::

	INFO: Documentation/translations/ll_NN/any-file.diff file has generated
	INFO: if you want to update Documentation/translations/ll_NN/any-file, please Do Not Forget to update the translation_origin_commit tag. 

	.. translation_origin_commit: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

So simply take a look to diff and update translation, also do not forget to 
modify commit-id tag.

Or the translation no need to update::

	INFO: Documentation/any-file does not have any change since xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

If the translation file does not have a commit-id tag::

	WARNING: Documentation/translations/ll_NN/any-file does not have a translation_origin_commit tag, can not generate a diff file, please add a tag if you want to update it.

	.. translation_origin_commit: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

Please add the tag by hand if you want to update it.

If you give a wrong file::

	ERROR: Documentation/any-file does not belong to ll_NN translation!

Why the name?
--------------

``trslt.py`` — tr(an)sl(a)t(or).

Issues
-------

If you find any problem, please report issues to Wu XiangCheng <bobwxc@email.cn>

Thanks
--------

Will be completed after RFC.
