.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/dev-tools/testing-overview.rst
:Translator: 胡皓文 Hu Haowen <src.res@email.cn>

============
内核测试指南
============

有许多不同的工具可以用于测试Linux内核，因此知道什么时候使用它们可能
很困难。该文档提供了它们之间区别的一个粗略概览，并阐释了它们怎样糅
合在一起的。

编写和运行测试
==============

大多数内核测试都是用kselftest或KUnit框架之一编写的。它们都让运行测试
更加简化，并为编写新测试提供帮助。

如果你想验证内核的行为——尤其是内核的特定部分——那你就要使用kUnit或
kselftest。

KUnit和kselftest的区别
----------------------

KUnit（Documentation/zh_CN/dev-tools/kunit/index.rst）是用于“白箱”测
试的一个完整的内核内部系统：因为测试代码是内核的一部分，所以它能够访
问用户空间不能访问到的内部结构和功能。

因此，KUnit测试最好不要写得太小，也不要有内核中本身包含的部分，以便
能够独立地测试。‘单元’测试的概念亦是如此。

比如，一个KUnit测试可能测试一个单独的内核功能（甚至通过一个函数测试
一个单一的代码路径），而不是整个地测试一个特性。

这也使得KUnit测试构建和运行非常地快，从而能够作为开发流程的一部分被
频繁地运行。

有关更详细的介绍，请参阅KUnit测试代码风格指南
Documentation/translations/zh_CN/dev-tools/kunit/style.rst

kselftest（Documentation/translations/zh_CN/dev-tools/kselftest.rst），
另一方面，大量地在用户空间被实现，并且测试通常是用户空间的脚本或程序。

这使得编写复杂的测试，或者需要操作更多全局系统状态的测试更加容易（诸
如生成进程之类）。然而，从kselftest直接调用内核函数是不行的。这也就
意味着只有通过某种方式（如系统调用、驱动设备、文件系统等）导出到了用
户空间的内核功能才能使用kselftest来测试。为此，有些测试包含了一个伴
生的内核模块用于导出更多的信息和功能。不过，对于基本上或者完全在内核
中运行的测试，KUnit可能是更佳工具。

比如，一个新的系统调用应该伴随有新的kselftest测试。

代码覆盖率工具
==============

Linux内核支持不同代码之间的覆盖率测量工具。这能被用来验证一个测试是
执行的特定的函数或者特定行的代码。这有助于决定内核被测试了多少，或
用来查找合适的测试中没有覆盖到的极端情况。

:doc:`gcov` 是GCC的覆盖率测试工具，能用于获取内核的全局或每个模块的
覆盖率。与KCOV不同的是，这个工具不记录每个任务的覆盖率。覆盖率数据可
以通过debugfs读取，并通过常规的gcov工具进行解释。

:doc:`kcov` 是能够构建在内核之中，用于在每个任务的层面捕捉覆盖率的一
个功能。因此，对于代码执行位置信息的其它情况，它是非常有用的，比如在
一个单一系统调用里使用它就很有用。

动态分析工具
============

内核也支持许多动态分析工具，用以检测正在运行的内核中出现的多种类型的
问题。这些工具尤其用于不同类型的漏洞，比如非法内存访问，诸如数据竞争
的并发问题，或者整型溢出的其它未定义的行为。

一些工具如下：

* kmemleak检测可能的内存泄漏。参阅
  Documentation/dev-tools/kmemleak.rst
* KASAN检测非法内存访问，如数组越界和释放内存后再使用的错误。参阅
  Documentation/dev-tools/kasan.rst
* UBSAN检测C标准中未定义的行为，如整型溢出。参阅
  Documentation/dev-tools/ubsan.rst
* KCSAN检测数据竞争。参阅 Documentation/dev-tools/kcsan.rst
* KFENCE是一个低开销的内存问题检测器，比KASAN更快而能被用于批量构建。
  参阅 Documentation/dev-tools/kfence.rst
* lockdep是一个锁定正确性检测器。参阅
  Documentation/locking/lockdep-design.rst
* 除此以外，在内核中还有一些其它的调试工具，大多数能在
  lib/Kconfig.debug 中找到。

这些工具倾向于对内核进行整体测试，并且不像kselftest和KUnit一样“传递”。
它们能与kselftest和KUnit结合起来，通过激活这些工具在一个内核上运行测
试：之后你就能确保这些错误在测试过程中都不会发生了。

一些工具与KUnit和kselftest集成，并且当问题被检测时测试会自动失败。

