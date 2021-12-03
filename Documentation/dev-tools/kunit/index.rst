.. SPDX-License-Identifier: GPL-2.0

=================================
KUnit - Linux Kernel Unit Testing
=================================

.. toctree::
	:maxdepth: 2
	:caption: Contents:

	start
	usage
	kunit-tool
	api/index
	style
	faq
	tips
	running_tips

This section details the kernel unit testing framework.

Introduction
============

KUnit (Kernel unit testing framework) prvoides a common framework for
unit tests within the Linux kernel. Using KUnit, you can define groups
of test cases called test suites. The tests either run on kernel boot
if built-in, or load as a module. KUnit automatically flags and reports
failed test cases in the kernel log. The test results appear in TAP
(Test Anything Protocol) format. It is inspired by JUnit, Python’s
unittest.mock, and GoogleTest/GoogleMock (C++ unit testing framework).

KUnit tests are part of the kernel, written in the C (programming)
language, and test parts of the Kernel implementation (example: a C
language function). Excluding build time, from invocation to
completion, KUnit can run around 100 tests in less than 10 seconds.
KUnit can test all kernel components, example: file system, system
calls, memory management, device drivers and so on.

KUnit follows the white-box testing approach. The test has access to
internal system functionality. KUnit runs in kernel space and is not
restricted to things exposed to user-space.

Features
--------

- Perform unit tests.
- Run tests on any kernel architecture.
- Runs test in milliseconds.

Prerequisites
-------------

- Any Linux kernel compatible hardware.
- For Kernel under test, Linux kernel version 5.5 or greater.

Unit Testing
============

A unit test verifies a single code unit. For example: a function or
codepath. The test executes a single test method multiple times with
different parameters. It is recommended to run unit test
independently of any other unit test or code.

Write Unit Tests
----------------

To write good unit tests, there is a simple but powerful pattern:
Arrange-Act-Asert. This is a great way to structure test cases and
defines an order of operations.

- Arrange inputs and targets: At the start of the test, arrange the data
  that allows a function to work. Example: initialize a statement or
  object.
- Act on the target behavior: Call your function/code under test.
- Assert expected outcome: Verify the initial state and result as
  expected or not.

Unit Testing Advantages
-----------------------

- Increases testing speed and development in the long run.
- Detects bugs at initial stage and therefore decreases bug fix cost
  compared to acceptance testing.
- Improves code quality.
- Encourages writing testable code.

How do I use it?
================

*   Documentation/dev-tools/kunit/start.rst - for KUnit new users.
*   Documentation/dev-tools/kunit/usage.rst - KUnit features.
*   Documentation/dev-tools/kunit/tips.rst - best practices with
    examples.
*   Documentation/dev-tools/kunit/api/index.rst - KUnit APIs
    used for testing.
*   Documentation/dev-tools/kunit/kunit-tool.rst - kunit_tool helper
    script.
*   Documentation/dev-tools/kunit/faq.rst - KUnit common questions and
    answers.
