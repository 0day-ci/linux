# SPDX-License-Identifier: GPL-2.0
#
# Parses KTAP test results from a kernel dmesg log and incrementally prints
# results with reader-friendly format. Stores and returns test results in a
# Test object.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>
# Author: Rae Moar <rmoar@google.com>

from __future__ import annotations
import re

from collections import namedtuple
from datetime import datetime
from enum import Enum, auto
from functools import reduce
from typing import Iterable, Iterator, List, Optional, Tuple

TestResult = namedtuple('TestResult', ['status','test','log'])

class Test(object):
	"""
	A class to represent a test parsed from KTAP results. All KTAP
	results within a test log are stored in a main Test object as
	subtests.

	Attributes:
	status : TestStatus - status of the test
	name : str - name of the test
	expected_count : int - expected number of subtests (0 if single
		test case and None if unknown expected number of subtests)
	subtests : List[Test] - list of subtests
	log : List[str] - log of KTAP lines that correspond to the test
	counts : TestCounts - counts of the test statuses and errors of
		subtests or of the test itself if the test is a single
		test case.
	"""
	def __init__(self) -> None:
		"""
		Contructs the default attributes of a Test class object.

		Parameters:
		None

		Return:
		None
		"""
		self.status = TestStatus.SUCCESS
		self.name = ''
		self.expected_count = 0  # type: Optional[int]
		self.subtests = []  # type: List[Test]
		self.log = []  # type: List[str]
		self.counts = TestCounts()

	def __str__(self) -> str:
		"""
		Returns string representation of a Test class object.

		Parameters:
		None

		Return:
		str - string representation of the Test class object
		"""
		return ('Test(' + str(self.status) + ', ' + self.name + ', ' +
			str(self.expected_count) + ', ' + str(self.subtests) +
			', ' + str(self.log) + ', ' + str(self.counts) + ')')

	def __repr__(self) -> str:
		"""
		Returns string representation of a Test class object.

		Parameters:
		None

		Return:
		str - string representation of the Test class object
		"""
		return str(self)

	def add_error(self, message: str):
		"""
		Adds error to test object by printing the error and
		incrementing the error count.

		Parameters:
		message : str - error message to print

		Return:
		None
		"""
		print_error('Test ' + self.name + ': ' + message)
		self.counts.errors += 1

class TestStatus(Enum):
	"""An enumeration class to represent the status of a test."""
	SUCCESS = auto()
	FAILURE = auto()
	SKIPPED = auto()
	TEST_CRASHED = auto()
	NO_TESTS = auto()
	FAILURE_TO_PARSE_TESTS = auto()

class TestCounts:
	"""
	A class to represent the counts of statuses and test errors of
	subtests or of the test itself if the test is a single test case with
	no subtests. Note that the sum of the counts of passed, failed,
	crashed, and skipped should sum to the total number of subtests for
	the test.

	Attributes:
	passed : int - the number of tests that have passed
	failed : int - the number of tests that have failed
	crashed : int - the number of tests that have crashed
	skipped : int - the number of tests that have skipped
	errors : int - the number of errors in the test and subtests
	"""
	def __init__(self):
		"""
		Contructs the default attributes of a TestCounts class object.
		Sets the counts of all test statuses and test errors to be 0.

		Parameters:
		None

		Return:
		None
		"""
		self.passed = 0
		self.failed = 0
		self.crashed = 0
		self.skipped = 0
		self.errors = 0

	def __str__(self) -> str:
		"""
		Returns total number of subtests or 1 if the test object has
		no subtests. This number is calculated by the sum of the
		passed, failed, crashed, and skipped subtests.

		Parameters:
		None

		Return:
		str - string representing TestCounts object.
		"""
		return ('Passed: ' + str(self.passed) + ', Failed: ' +
			str(self.failed) + ', Crashed: ' + str(self.crashed) +
			', Skipped: ' + str(self.skipped) + ', Errors: ' +
			str(self.errors))

	def total(self) -> int:
		"""
		Returns total number of subtests or 1 if the test object has
		no subtests. This number is calculated by the sum of the
		passed, failed, crashed, and skipped subtests.

		Parameters:
		None

		Return:
		int - the total number of subtests or 1 if the test object has
			no subtests
		"""
		return self.passed + self.failed + self.crashed + self.skipped

	def add_subtest_counts(self, counts: TestCounts) -> None:
		"""
		Adds the counts of another TestCounts object to the current
		TestCounts object. Used to add the counts of a subtest to the
		parent test.

		Parameters:
		counts : TestCounts - another TestCounts object whose counts
		will be added to the counts of the TestCounts object

		Return:
		None
		"""
		self.passed += counts.passed
		self.failed += counts.failed
		self.crashed += counts.crashed
		self.skipped += counts.skipped
		self.errors += counts.errors

	def get_status(self) -> TestStatus:
		"""
		Returns the expected status of a Test using test counts.

		Parameters:
		None

		Return:
		TestStatus - expected status of a Test given test counts
		"""
		if self.crashed:
			# If one of the subtests crash, the expected status of
			# the Test is crashed.
			return TestStatus.TEST_CRASHED
		elif self.failed:
			# Otherwise if one of the subtests fail, the
			# expected status of the Test is failed.
			return TestStatus.FAILURE
		elif self.passed:
			# Otherwise if one of the subtests pass, the
			# expected status of the Test is passed.
			return TestStatus.SUCCESS
		else:
			# Finally, if none of the subtests have failed,
			# crashed, or passed, the expected status of the
			# Test is skipped.
			return TestStatus.SKIPPED

	def add_status(self, status: TestStatus) -> None:
		"""
		Given inputted status, increments corresponding attribute of
		TestCounts object.

		Parameters:
		status : TestStatus - status to be added to the TestCounts
			object

		Return:
		None
		"""
		if status == TestStatus.SUCCESS or \
				status == TestStatus.NO_TESTS:
			# if status is NO_TESTS the most appropriate attribute
			# to increment is passed because the test did not
			# fail, crash or get skipped.
			self.passed += 1
		elif status == TestStatus.FAILURE:
			self.failed += 1
		elif status == TestStatus.SKIPPED:
			self.skipped += 1
		else:
			self.crashed += 1

class LineStream:
	"""
	A class to represent the lines of kernel output.
	Provides a peek()/pop() interface over an iterator of
	(line#, text).

	Attributes:
	_lines : Iterator[Tuple[int, str]] - Iterator containing tuple of
		line number and line of kernel output
	_next : Tuple[int, str] - Tuple containing next line and the
		corresponding line number
	_done : bool - boolean denoting whether the LineStream has reached
		the end of the lines
	"""
	_lines: Iterator[Tuple[int, str]]
	_next: Tuple[int, str]
	_done: bool

	def __init__(self, lines: Iterator[Tuple[int, str]]):
		"""Set defaults for LineStream object and sets _lines
		attribute to lines parameter.
		"""
		self._lines = lines
		self._done = False
		self._next = (0, '')
		self._get_next()

	def _get_next(self) -> None:
		"""Sets _next attribute to the upcoming Tuple of line and
		line number in the LineStream.
		"""
		try:
			self._next = next(self._lines)
		except StopIteration:
			self._done = True

	def peek(self) -> str:
		"""Returns the line stored in the _next attribute."""
		return self._next[1]

	def pop(self) -> str:
		"""Returns the line stored in the _next attribute and sets the
		_next attribute to the following line and line number Tuple.
		"""
		n = self._next
		self._get_next()
		return n[1]

	def __bool__(self) -> bool:
		"""Returns whether the LineStream has reached the end of the
		lines.
		"""
		return not self._done

	# Only used by kunit_tool_test.py.
	def __iter__(self) -> Iterator[str]:
		"""Returns an Iterator object containing all of the lines
		stored in the LineStream object. This method also empties the
		LineStream so it reaches the end of the lines.
		"""
		while bool(self):
			yield self.pop()

	def line_number(self) -> int:
		"""Returns the line number of the upcoming line."""
		return self._next[0]

# Parsing helper methods:

KTAP_START = re.compile(r'KTAP version ([0-9]+)$')
TAP_START = re.compile(r'TAP version ([0-9]+)$')
KTAP_END = re.compile('(List of all partitions:|'
	'Kernel panic - not syncing: VFS:|reboot: System halted)')

def extract_tap_lines(kernel_output: Iterable[str]) -> LineStream:
	"""
	Returns LineStream object of extracted ktap lines within
	inputted kernel output.

	Parameters:
	kernel_output : Iterable[str] - iterable object contains lines
		of kernel output

	Return:
	LineStream - LineStream object containing extracted ktap lines.
	"""
	def isolate_ktap_output(kernel_output: Iterable[str]) \
			-> Iterator[Tuple[int, str]]:
		"""
		Helper method of extract_tap_lines that yields extracted
		ktap lines within inputted kernel output. Output is used to
		create LineStream object in isolate_ktap_output.

		Parameters:
		kernel_output : Iterable[str] - iterable object contains lines
			of kernel output

		Return:
		Iterator[Tuple[int, str]] - Iterator object containing tuples
		with extracted ktap lines and their correesponding line
		number.
		"""
		line_num = 0
		started = False
		for line in kernel_output:
			line_num += 1
			line = line.rstrip()  # remove trailing \n
			if not started and KTAP_START.search(line):
				prefix_len = len(
					line.split('KTAP version')[0])
				started = True
				yield line_num, line[prefix_len:]
			elif not started and TAP_START.search(line):
				prefix_len = len(line.split('TAP version')[0])
				started = True
				yield line_num, line[prefix_len:]
			elif started and KTAP_END.search(line):
				break
			elif started:
				# remove prefix and indention
				line = line[prefix_len:].lstrip()
				yield line_num, line
	return LineStream(lines=isolate_ktap_output(kernel_output))

def raw_output(kernel_output: Iterable[str]) -> None:
	"""
	Prints all of given kernel output.

	Parameters:
	kernel_output : Iterable[str] - iterable object contains lines
		of kernel output

	Return:
	None
	"""
	for line in kernel_output:
		print(line.rstrip())

KTAP_VERSIONS = [1]
TAP_VERSIONS = [13, 14]

def check_version(version_num: int, accepted_versions: List[int], \
		version_type: str, test: Test) -> None:
	"""
	Adds errors to the test if the version number is too high or too low.

	Parameters:
	version_num : int - The inputted version number from the parsed
		ktap or tap header line
	accepted_version : List[int] - List of accepted ktap or tap versions
	version_type : str - 'KTAP' or 'TAP' depending on the type of
		version line.
	test : Test - Test object representing current test object being
		parsed

	Return:
	None
	"""
	if version_num < min(accepted_versions):
		test.add_error(version_type + ' version lower than expected!')
	elif version_num > max(accepted_versions):
		test.add_error(
			version_type + ' version higher than expected!')

def parse_ktap_header(lines: LineStream, test: Test) -> bool:
	"""
	If the next line in LineStream matches the format of ktap or tap
	header line, the version number is checked, the line is popped,
	and returns True. Otherwise the method returns False.

	Accepted formats:
	- 'KTAP version [version number]'
	- 'TAP version [version number]'

	Parameters:
	lines : LineStream - LineStream object containing ktap lines from
		kernel output
	test : Test - Test object representing current test object being
		parsed

	Return:
	bool : Represents if the next line in the LineStream was parsed as
		the ktap or tap header line
	"""
	ktap_match = KTAP_START.match(lines.peek())
	tap_match = TAP_START.match(lines.peek())
	if ktap_match:
		version_num = int(ktap_match.group(1))
		check_version(version_num, KTAP_VERSIONS, 'KTAP', test)
	elif tap_match:
		version_num = int(tap_match.group(1))
		check_version(version_num, TAP_VERSIONS, 'TAP', test)
	else:
		return False
	test.log.append(lines.pop())
	return True

TEST_HEADER = re.compile(r'^# Subtest: (.*)$')

def parse_test_header(lines: LineStream, test: Test) -> bool:
	"""
	If the next line in LineStream matches the format of a test
	header line, the name of test is set, the line is popped,
	and returns True. Otherwise the method returns False.

	Accepted format:
	- '# Subtest: [test name]'

	Parameters:
	lines : LineStream - LineStream object containing ktap lines from
		kernel output
	test : Test - Test object representing current test object being
		parsed

	Return:
	bool : Represents if the next line in the LineStream was parsed as
		a test header
	"""
	match = TEST_HEADER.match(lines.peek())
	if not match:
		return False
	test.log.append(lines.pop())
	test.name = match.group(1)
	return True

TEST_PLAN = re.compile(r'1\.\.([0-9]+)')

def parse_test_plan(lines: LineStream, test: Test) -> bool:
	"""
	If the next line in LineStream matches the format of a test
	plan line, the expected number of subtests is set in test object, an
	error is thrown if there are 0 tests, the line is popped,
	and returns True. Otherwise the method adds an error that the test
	plan is missing to the test object and returns False.

	Accepted format:
	- '1..[number of subtests]'

	Parameters:
	lines : LineStream - LineStream object containing ktap lines from
		kernel output
	test : Test - Test object representing current test object being
		parsed

	Return:
	bool : Represents if the next line in the LineStream was parsed as
		a test plan
	"""
	match = TEST_PLAN.match(lines.peek())
	if not match:
		test.expected_count = None
		test.add_error('missing plan line!')
		return False
	test.log.append(lines.pop())
	expected_count = int(match.group(1))
	test.expected_count = expected_count
	if expected_count == 0:
		test.status = TestStatus.NO_TESTS
		test.add_error('0 tests run!')
	return True

TEST_RESULT = re.compile(r'^(ok|not ok) ([0-9]+) (- )?(.*)$')

TEST_RESULT_SKIP = re.compile(r'^(ok|not ok) ([0-9]+) (- )?(.*) # SKIP(.*)$')

def peek_test_name_match(lines: LineStream, test: Test) -> bool:
	"""
	If the next line in LineStream matches the format of a test
	result line and the name of the result line matches the name of the
	current test, the method returns True. Otherwise it returns False.

	Accepted format:
	- '[ok|not ok] [test number] [-] [test name] [optional skip
		directive]'

	Parameters:
	lines : LineStream - LineStream object containing ktap lines from
		kernel output
	test : Test - Test object representing current test object being
		parsed

	Return:
	bool : Represents if the next line in the LineStream matched a test
		result line and the name matched the test name
	"""
	line = lines.peek()
	match = TEST_RESULT.match(line)
	if not match:
		return False
	name = match.group(4)
	return (name == test.name)

def parse_test_result(lines: LineStream, test: Test, expected_num: int) \
		-> bool:
	"""
	If the next line in LineStream matches the format of a test
	result line, the status in the result line is added to the test
	object, the test number is checked to match the expected test number
	and if not an error is added to the test object, and returns True.
	Otherwise it returns False. Note that the skip diirective is the only
	directive that causes a change in status and otherwise the directive
	is included in the name of the test.

	Accepted format:
	- '[ok|not ok] [test number] [-] [test name] [optional skip
		directive]'

	Parameters:
	lines : LineStream - LineStream object containing ktap lines from
		kernel output
	test : Test - Test object representing current test object being
		parsed
	expected_num : int - expected test number for current test

	Return:
	bool : Represents if the next line in the LineStream was parsed as a
		test result line.
	"""
	line = lines.peek()
	match = TEST_RESULT.match(line)
	skip_match = TEST_RESULT_SKIP.match(line)

	# Check if line matches test result line format
	if not match:
		return False
	test.log.append(lines.pop())

	# Check test num
	num = int(match.group(2))
	if num != expected_num:
		test.add_error('Expected test number ' +
			str(expected_num) + ' but found ' + str(num))

	# Set name of test object
	if skip_match:
		test.name = skip_match.group(4)
	else:
		test.name = match.group(4)

	# Set status of test object
	status = match.group(1)
	if test.status == TestStatus.TEST_CRASHED:
		return True
	elif skip_match:
		test.status = TestStatus.SKIPPED
	elif status == 'ok':
		test.status = TestStatus.SUCCESS
	else:
		test.status = TestStatus.FAILURE
	return True

DIAGNOSTIC_CRASH_MESSAGE = re.compile(r'^# .*?: kunit test case crashed!$')

def parse_diagnostic(lines: LineStream, test: Test) -> None:
	"""
	If the next line in LineStream does not match the format of a test
	case line or test header line, the line is checked if the test has
	crashed and if so adds an error message, pops the line and adds it to
	the log.

	Line formats that are not parsed:
	- '# Subtest: [test name]'
	- '[ok|not ok] [test number] [-] [test name] [optional skip
		directive]'

	Parameters:
	lines : LineStream - LineStream object containing ktap lines from
		kernel output
	test : Test - Test object representing current test object being
		parsed

	Return:
	None
	"""
	while lines and not TEST_RESULT.match(lines.peek()) and not \
			TEST_HEADER.match(lines.peek()):
		if DIAGNOSTIC_CRASH_MESSAGE.match(lines.peek()):
			test.status = TestStatus.TEST_CRASHED
		test.log.append(lines.pop())

# Printing helper methods:

DIVIDER = '=' * 60

RESET = '\033[0;0m'

def red(text: str) -> str:
	"""
	Returns string with added red ansi color code at beginning and reset
	code at end.

	Parameters:
	text: str -> text to be made red with ansi color codes

	Return:
	str - original text made red with ansi color codes
	"""
	return '\033[1;31m' + text + RESET

def yellow(text: str) -> str:
	"""
	Returns string with added yellow ansi color code at beginning and
	reset code at end.

	Parameters:
	text: str -> text to be made yellow with ansi color codes

	Return:
	str - original text made yellow with ansi color codes
	"""
	return '\033[1;33m' + text + RESET

def green(text: str) -> str:
	"""
	Returns string with added green ansi color code at beginning and reset
	code at end.

	Parameters:
	text: str -> text to be made green with ansi color codes

	Return:
	str - original text made green with ansi color codes
	"""
	return '\033[1;32m' + text + RESET

ANSI_LEN = len(red(''))

def print_with_timestamp(message: str) -> None:
	"""
	Prints message with timestamp at beginning.

	Parameters:
	message: str -> message to be printed

	Return:
	None
	"""
	print('[%s] %s' % (datetime.now().strftime('%H:%M:%S'), message))

def format_test_divider(message: str, len_message: int) -> str:
	"""
	Returns string with message centered in fixed width divider.

	Example:
	'===================== message example ====================='

	Parameters:
	message: str -> message to be centered in divider line
	len_message : int -> length of the message to be printed in the
		divider such that the ansi codes are not counted if the
		message is colored.

	Return:
	str - string containing message centered in fixed width divider
	"""
	default_count = 3  # default number of dashes
	len_1 = default_count
	len_2 = default_count
	difference = len(DIVIDER) - len_message - 2  # 2 spaces added
	if difference > 0:
		# calculate number of dashes for each side of the divider
		len_1 = int(difference / 2)
		len_2 = difference - len_1
	return ('=' * len_1) + ' ' + message + ' ' + ('=' * len_2)

def print_test_header(test: Test) -> None:
	"""
	Prints test header with test name and optionally the expected number
	of subtests.

	Example:
	'=================== example (2 subtests) ==================='

	Parameters:
	test: Test -> Test object representing current test object being
		parsed and information used to print test header

	Return:
	None
	"""
	message = test.name
	if test.expected_count:
		message += ' (' + str(test.expected_count) + ' subtests)'
	print_with_timestamp(format_test_divider(message, len(message)))

def print_log(log: Iterable[str]) -> None:
	"""
	Prints all strings in saved log for test in yellow.

	Parameters:
	log: Iterable[str] -> Iterable object with all strings saved in log
		for test

	Return:
	None
	"""
	for m in log:
		print_with_timestamp(yellow(m))
	print_with_timestamp('')

def format_test_result(test: Test) -> str:
	"""
	Returns string with formatted test result with colored status and test
	name.

	Example:
	'[PASSED] example'

	Parameters:
	test: Test -> Test object representing current test object being
		parsed and information used to print test result

	Return:
	str - string containing formatted test result
	"""
	if test.status == TestStatus.SUCCESS:
		return (green('[PASSED] ') + test.name)
	elif test.status == TestStatus.SKIPPED:
		return (yellow('[SKIPPED] ') + test.name)
	elif test.status == TestStatus.TEST_CRASHED:
		print_log(test.log)
		return (red('[CRASHED] ') + test.name)
	else:
		print_log(test.log)
		return (red('[FAILED] ') + test.name)

def print_test_result(test: Test) -> None:
	"""
	Prints result line with status of test.

	Example:
	'[PASSED] example'

	Parameters:
	test: Test -> Test object representing current test object being
		parsed and information used to print test result line

	Return:
	None
	"""
	print_with_timestamp(format_test_result(test))

def print_test_footer(test: Test) -> None:
	"""
	Prints test footer with status of test.

	Example:
	'===================== [PASSED] example ====================='

	Parameters:
	test: Test -> Test object representing current test object being
		parsed and information used to print test footer

	Return:
	None
	"""
	message = format_test_result(test)
	print_with_timestamp(format_test_divider(message,
		len(message) - ANSI_LEN))

def print_summary_line(test: Test) -> None:
	"""
	Prints summary line of test object. Color of line is dependent on
	status of test. Color is green if test passes, yellow if test is
	skipped, and red if the test fails or crashes. Summary line contains
	counts of the statuses of the tests subtests or the test itself if it
	has no subtests.

	Example:
	'Testing complete. Passed: 2, Failed: 0, Crashed: 0, Skipped: 0, \
	Errors: 0'

	Parameters:
	test: Test -> Test object representing current test object being
		parsed and information used to print test summary line

	Return:
	None
	"""
	if test.status == TestStatus.SUCCESS or \
			test.status == TestStatus.NO_TESTS:
		color = green
	elif test.status == TestStatus.SKIPPED:
		color = yellow
	else:
		color = red
	counts = test.counts
	print_with_timestamp(color('Testing complete. ' + str(counts)))

def print_error(message: str) -> None:
	"""
	Prints message with error format.

	Parameters:
	message: str -> message to be used as error message

	Return:
	None
	"""
	print_with_timestamp(red('[ERROR] ') + message)

# Other methods:

def bubble_up_test_results(test: Test) -> None:
	"""
	If the test has subtests, add the test counts of the subtests to the
	test and check if any of the tests crashed and if so set the test
	status to crashed. Otherwise if the test has no subtests add the
	status of the test to the test counts.

	Parameters:
	test : Test - Test object representing current test object being
		parsed

	Return:
	None
	"""
	subtests = test.subtests
	counts = test.counts
	status = test.status
	for t in subtests:
		counts.add_subtest_counts(t.counts)
	if counts.total() == 0:
		counts.add_status(status)
	elif test.counts.get_status() == TestStatus.TEST_CRASHED:
		test.status = TestStatus.TEST_CRASHED

def parse_test(lines: LineStream, expected_num: int) -> Test:
	"""
	Finds next test to parse in LineStream, creates new Test object,
	parses any subtests of the test, populates Test object with all
	information (status, name) about the test and the Test objects for
	any subtests, and then returns the Test object. The method accepts
	three formats of tests:

	Accepted test formats:

	- Main KTAP/TAP header

	Example:

	KTAP version 1
	1..4
	[subtests]

	- Subtest header line

	Example:

	# Subtest: name
	1..3
	[subtests]
	ok 1 name

	- Test result line

	Example:

	ok 1 - test

	Parameters:
	lines : LineStream - LineStream object containing ktap lines from
		kernel output
	expected_num : int - expected test number for test to be parsed

	Return:
	Test : Test object populated with characteristics and containing any
		subtests
	"""
	test = Test()
	parent_test = False
	main = parse_ktap_header(lines, test)
	if main:
		# If KTAP/TAP header is found, attempt to parse
		# test plan
		parse_test_plan(lines, test)
	else:
		# If KTAP/TAP header is not found, test must be subtest
		# header or test result line so parse attempt to parser
		# subtest header
		parse_diagnostic(lines, test)
		parent_test = parse_test_header(lines, test)
		if parent_test:
			# If subtest header is found, attempt to parse
			# test plan and print header
			parse_test_plan(lines, test)
			print_test_header(test)
	expected_count = test.expected_count
	subtests = []
	test_num = 1
	while main or expected_count is None or test_num <= expected_count:
		# Loop to parse any subtests.
		# If test is main test, do not break until no lines left.
		# Otherwise, break after parsing expected number of tests or
		# if expected number of tests is unknown break when found
		# test result line with matching name to subtest header.
		if not lines:
			if expected_count and test_num <= expected_count:
				test.add_error('missing expected subtests!')
			break
		if not expected_count and not main and \
				peek_test_name_match(lines, test):
			break
		subtests.append(parse_test(lines, test_num))
		test_num += 1
	test.subtests = subtests
	if not main:
		# If not main test, look for test result line
		parse_diagnostic(lines, test)
		if (parent_test and peek_test_name_match(lines, test)) or \
				not parent_test:
			parse_test_result(lines, test, expected_num)
			if not parent_test:
				print_test_result(test)
		else:
			test.add_error('missing subtest result line!')
	# Add statuses to TestCounts attribute in Test object
	bubble_up_test_results(test)
	if parent_test:
		# If test has subtests and is not the main test object, print
		# footer.
		print_test_footer(test)
	return test

def parse_run_tests(kernel_output: Iterable[str]) -> TestResult:
	"""
	Using kernel output, extract ktap lines, parse the lines for test
	results and print condensed test results and summary line .

	Parameters:
	kernel_output : Iterable[str] - iterable object contains lines
		of kernel output

	Return:
	TestResult - Tuple containg status of main test object, main test
		object with all subtests, and log of all ktap lines.
	"""
	print_with_timestamp(DIVIDER)
	lines = extract_tap_lines(kernel_output)
	test = Test()
	if not lines:
		test.add_error('invalid KTAP input!')
		test.status = TestStatus.FAILURE_TO_PARSE_TESTS
	else:
		test = parse_test(lines, 0)
		if test.status != TestStatus.NO_TESTS:
			test.status = test.counts.get_status()
	print_with_timestamp(DIVIDER)
	print_summary_line(test)
	return TestResult(test.status, test, lines)
