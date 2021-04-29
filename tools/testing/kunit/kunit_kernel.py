# SPDX-License-Identifier: GPL-2.0
#
# Runs UML kernel, collects output, and handles errors.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import logging
import subprocess
import os
import shutil
import signal
from typing import Iterator

from contextlib import ExitStack

from collections import namedtuple

import kunit_config
import kunit_parser

KCONFIG_PATH = '.config'
KUNITCONFIG_PATH = '.kunitconfig'
DEFAULT_KUNITCONFIG_PATH = 'arch/um/configs/kunit_defconfig'
BROKEN_ALLCONFIG_PATH = 'tools/testing/kunit/configs/broken_on_uml.config'
OUTFILE_PATH = 'test.log'

def get_file_path(build_dir, default):
	if build_dir:
		default = os.path.join(build_dir, default)
	return default

class ConfigError(Exception):
	"""Represents an error trying to configure the Linux kernel."""


class BuildError(Exception):
	"""Represents an error trying to build the Linux kernel."""


class LinuxSourceTreeOperations(object):
	"""An abstraction over command line operations performed on a source tree."""

	def __init__(self, linux_arch, cross_compile):
		self._linux_arch = linux_arch
		self._cross_compile = cross_compile

	def make_mrproper(self) -> None:
		try:
			subprocess.check_output(['make', 'mrproper'], stderr=subprocess.STDOUT)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output.decode())

	def make_arch_qemuconfig(self, build_dir):
		pass

	def make_olddefconfig(self, build_dir, make_options) -> None:
		command = ['make', 'ARCH=' + self._linux_arch, 'olddefconfig']
		if self._cross_compile:
			command += ['CROSS_COMPILE=' + self._cross_compile]
		if make_options:
			command.extend(make_options)
		if build_dir:
			command += ['O=' + build_dir]
		print(' '.join(command))
		try:
			subprocess.check_output(command, stderr=subprocess.STDOUT)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output.decode())

	def make(self, jobs, build_dir, make_options) -> None:
		command = ['make', 'ARCH=' + self._linux_arch, '--jobs=' + str(jobs)]
		if make_options:
			command.extend(make_options)
		if self._cross_compile:
			command += ['CROSS_COMPILE=' + self._cross_compile]
		if build_dir:
			command += ['O=' + build_dir]
		print(' '.join(command))
		try:
			proc = subprocess.Popen(command,
						stderr=subprocess.PIPE,
						stdout=subprocess.DEVNULL)
		except OSError as e:
			raise BuildError('Could not call execute make: ' + e)
		except subprocess.CalledProcessError as e:
			raise BuildError(e.output)
		_, stderr = proc.communicate()
		if proc.returncode != 0:
			raise BuildError(stderr.decode())
		if stderr:  # likely only due to build warnings
			print(stderr.decode())

	def run(self, params, timeout, build_dir, outfile) -> None:
		pass


QemuArchParams = namedtuple('QemuArchParams', ['linux_arch',
					       'qemuconfig',
					       'qemu_arch',
					       'kernel_path',
					       'kernel_command_line',
					       'extra_qemu_params'])


QEMU_ARCHS = {
	'i386'		: QemuArchParams(linux_arch='i386',
				qemuconfig='CONFIG_SERIAL_8250=y\nCONFIG_SERIAL_8250_CONSOLE=y',
				qemu_arch='x86_64',
				kernel_path='arch/x86/boot/bzImage',
				kernel_command_line='console=ttyS0',
				extra_qemu_params=['']),
	'x86_64'	: QemuArchParams(linux_arch='x86_64',
				qemuconfig='CONFIG_SERIAL_8250=y\nCONFIG_SERIAL_8250_CONSOLE=y',
				qemu_arch='x86_64',
				kernel_path='arch/x86/boot/bzImage',
				kernel_command_line='console=ttyS0',
				extra_qemu_params=['']),
	'arm'		: QemuArchParams(linux_arch='arm',
				qemuconfig='''CONFIG_ARCH_VIRT=y
CONFIG_SERIAL_AMBA_PL010=y
CONFIG_SERIAL_AMBA_PL010_CONSOLE=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y''',
				qemu_arch='arm',
				kernel_path='arch/arm/boot/zImage',
				kernel_command_line='console=ttyAMA0',
				extra_qemu_params=['-machine virt']),
	'arm64'		: QemuArchParams(linux_arch='arm64',
				qemuconfig='''CONFIG_SERIAL_AMBA_PL010=y
CONFIG_SERIAL_AMBA_PL010_CONSOLE=y
CONFIG_SERIAL_AMBA_PL011=y
CONFIG_SERIAL_AMBA_PL011_CONSOLE=y''',
				qemu_arch='aarch64',
				kernel_path='arch/arm64/boot/Image.gz',
				kernel_command_line='console=ttyAMA0',
				extra_qemu_params=['-machine virt', '-cpu cortex-a57']),
	'alpha'		: QemuArchParams(linux_arch='alpha',
				qemuconfig='CONFIG_SERIAL_8250=y\nCONFIG_SERIAL_8250_CONSOLE=y',
				qemu_arch='alpha',
				kernel_path='arch/alpha/boot/vmlinux',
				kernel_command_line='console=ttyS0',
				extra_qemu_params=['']),
	'powerpc'	: QemuArchParams(linux_arch='powerpc',
				qemuconfig='CONFIG_PPC64=y\nCONFIG_SERIAL_8250=y\nCONFIG_SERIAL_8250_CONSOLE=y\nCONFIG_HVC_CONSOLE=y',
				qemu_arch='ppc64',
				kernel_path='vmlinux',
				kernel_command_line='console=ttyS0',
				extra_qemu_params=['-M pseries', '-cpu power8']),
	'riscv'		: QemuArchParams(linux_arch='riscv',
				qemuconfig='CONFIG_SOC_VIRT=y\nCONFIG_SERIAL_8250=y\nCONFIG_SERIAL_8250_CONSOLE=y\nCONFIG_SERIAL_OF_PLATFORM=y\nCONFIG_SERIAL_EARLYCON_RISCV_SBI=y',
				qemu_arch='riscv64',
				kernel_path='arch/riscv/boot/Image',
				kernel_command_line='console=ttyS0',
				extra_qemu_params=['-machine virt', '-cpu rv64', '-bios opensbi-riscv64-generic-fw_dynamic.bin']),
	's390'		: QemuArchParams(linux_arch='s390',
				qemuconfig='CONFIG_EXPERT=y\nCONFIG_TUNE_ZEC12=y\nCONFIG_NUMA=y\nCONFIG_MODULES=y',
				qemu_arch='s390x',
				kernel_path='arch/s390/boot/bzImage',
				kernel_command_line='console=ttyS0',
				extra_qemu_params=[
						'-machine s390-ccw-virtio',
						'-cpu qemu',]),
	'sparc'		: QemuArchParams(linux_arch='sparc',
				qemuconfig='CONFIG_SERIAL_8250=y\nCONFIG_SERIAL_8250_CONSOLE=y',
				qemu_arch='sparc',
				kernel_path='arch/sparc/boot/zImage',
				kernel_command_line='console=ttyS0 mem=256M',
				extra_qemu_params=['-m 256']),
}

class LinuxSourceTreeOperationsQemu(LinuxSourceTreeOperations):

	def __init__(self, qemu_arch_params, cross_compile):
		super().__init__(linux_arch=qemu_arch_params.linux_arch,
				 cross_compile=cross_compile)
		self._qemuconfig = qemu_arch_params.qemuconfig
		self._qemu_arch = qemu_arch_params.qemu_arch
		self._kernel_path = qemu_arch_params.kernel_path
		print(self._kernel_path)
		self._kernel_command_line = qemu_arch_params.kernel_command_line + ' kunit_shutdown=reboot'
		self._extra_qemu_params = qemu_arch_params.extra_qemu_params

	def make_arch_qemuconfig(self, build_dir):
		qemuconfig = kunit_config.Kconfig()
		qemuconfig.parse_from_string(self._qemuconfig)
		qemuconfig.write_to_file(get_kconfig_path(build_dir))

	def run(self, params, timeout, build_dir, outfile):
		kernel_path = os.path.join(build_dir, self._kernel_path)
		qemu_command = ['qemu-system-' + self._qemu_arch,
				'-nodefaults',
				'-m', '1024',
				'-kernel', kernel_path,
				'-append', '\'' + ' '.join(params + [self._kernel_command_line]) + '\'',
				'-no-reboot',
				'-nographic',
				'-serial stdio'] + self._extra_qemu_params
		print(' '.join(qemu_command))
		with open(outfile, 'w') as output:
			process = subprocess.Popen(' '.join(qemu_command),
						   stdin=subprocess.PIPE,
						   stdout=output,
						   stderr=subprocess.STDOUT,
						   text=True, shell=True)
		try:
			process.wait(timeout=timeout)
		except Exception as e:
			print(e)
			process.terminate()
		return process

class LinuxSourceTreeOperationsUml(LinuxSourceTreeOperations):
	"""An abstraction over command line operations performed on a source tree."""

	def __init__(self):
		super().__init__(linux_arch='um', cross_compile=None)

	def make_allyesconfig(self, build_dir, make_options) -> None:
		kunit_parser.print_with_timestamp(
			'Enabling all CONFIGs for UML...')
		command = ['make', 'ARCH=um', 'allyesconfig']
		if make_options:
			command.extend(make_options)
		if build_dir:
			command += ['O=' + build_dir]
		process = subprocess.Popen(
			command,
			stdout=subprocess.DEVNULL,
			stderr=subprocess.STDOUT)
		process.wait()
		kunit_parser.print_with_timestamp(
			'Disabling broken configs to run KUnit tests...')
		with ExitStack() as es:
			config = open(get_kconfig_path(build_dir), 'a')
			disable = open(BROKEN_ALLCONFIG_PATH, 'r').read()
			config.write(disable)
		kunit_parser.print_with_timestamp(
			'Starting Kernel with all configs takes a few minutes...')

	def run(self, params, timeout, build_dir, outfile):
		"""Runs the Linux UML binary. Must be named 'linux'."""
		linux_bin = get_file_path(build_dir, 'linux')
		outfile = get_outfile_path(build_dir)
		with open(outfile, 'w') as output:
			process = subprocess.Popen([linux_bin] + params,
						   stdin=subprocess.PIPE,
						   stdout=output,
						   stderr=subprocess.STDOUT,
						   text=True)
			process.wait(timeout)

def get_kconfig_path(build_dir) -> str:
	return get_file_path(build_dir, KCONFIG_PATH)

def get_kunitconfig_path(build_dir) -> str:
	return get_file_path(build_dir, KUNITCONFIG_PATH)

def get_outfile_path(build_dir) -> str:
	return get_file_path(build_dir, OUTFILE_PATH)

class LinuxSourceTree(object):
	"""Represents a Linux kernel source tree with KUnit tests."""

	def __init__(self, build_dir: str, load_config=True, kunitconfig_path='', arch=None, cross_compile=None) -> None:
		signal.signal(signal.SIGINT, self.signal_handler)
		self._ops = None
		if arch is None or arch == 'um':
			self._arch = 'um'
			self._ops = LinuxSourceTreeOperationsUml()
		elif arch in QEMU_ARCHS:
			self._arch = arch
			self._ops = LinuxSourceTreeOperationsQemu(QEMU_ARCHS[arch], cross_compile=cross_compile)
		else:
			raise ConfigError(arch + ' is not a valid arch')

		if not load_config:
			return

		if kunitconfig_path:
			if not os.path.exists(kunitconfig_path):
				raise ConfigError(f'Specified kunitconfig ({kunitconfig_path}) does not exist')
		else:
			kunitconfig_path = get_kunitconfig_path(build_dir)
			if not os.path.exists(kunitconfig_path):
				shutil.copyfile(DEFAULT_KUNITCONFIG_PATH, kunitconfig_path)

		self._kconfig = kunit_config.Kconfig()
		self._kconfig.read_from_file(kunitconfig_path)

	def clean(self) -> bool:
		try:
			self._ops.make_mrproper()
		except ConfigError as e:
			logging.error(e)
			return False
		return True

	def validate_config(self, build_dir) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		validated_kconfig = kunit_config.Kconfig()
		validated_kconfig.read_from_file(kconfig_path)
		if not self._kconfig.is_subset_of(validated_kconfig):
			invalid = self._kconfig.entries() - validated_kconfig.entries()
			message = 'Provided Kconfig is not contained in validated .config. Following fields found in kunitconfig, ' \
					  'but not in .config: %s' % (
					', '.join([str(e) for e in invalid])
			)
			logging.error(message)
			return False
		return True

	def build_config(self, build_dir, make_options) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		if build_dir and not os.path.exists(build_dir):
			os.mkdir(build_dir)
		self._kconfig.write_to_file(kconfig_path)
		try:
			self._ops.make_arch_qemuconfig(build_dir)
			self._ops.make_olddefconfig(build_dir, make_options)
		except ConfigError as e:
			logging.error(e)
			return False
		return self.validate_config(build_dir)

	def build_reconfig(self, build_dir, make_options) -> bool:
		"""Creates a new .config if it is not a subset of the .kunitconfig."""
		kconfig_path = get_kconfig_path(build_dir)
		if os.path.exists(kconfig_path):
			existing_kconfig = kunit_config.Kconfig()
			existing_kconfig.read_from_file(kconfig_path)
			if not self._kconfig.is_subset_of(existing_kconfig):
				print('Regenerating .config ...')
				os.remove(kconfig_path)
				return self.build_config(build_dir, make_options)
			else:
				return True
		else:
			print('Generating .config ...')
			return self.build_config(build_dir, make_options)

	def build_kernel(self, alltests, jobs, build_dir, make_options) -> bool:
		try:
			if alltests:
				if self._arch != 'um':
					raise ConfigError('Only the "um" arch is supported for alltests')
				self._ops.make_allyesconfig(build_dir, make_options)
			self._ops.make_arch_qemuconfig(build_dir)
			self._ops.make_olddefconfig(build_dir, make_options)
			self._ops.make(jobs, build_dir, make_options)
		except (ConfigError, BuildError) as e:
			logging.error(e)
			return False
		return self.validate_config(build_dir)

	def run_kernel(self, args=None, build_dir='', filter_glob='', timeout=None) -> Iterator[str]:
		if not args:
			args = []
		args.extend(['mem=1G', 'console=tty','kunit_shutdown=halt'])
		if filter_glob:
			args.append('kunit.filter_glob='+filter_glob)
		outfile = get_outfile_path(build_dir)
		self._ops.run(args, timeout, build_dir, outfile)
		subprocess.call(['stty', 'sane'])
		with open(outfile, 'r') as file:
			for line in file:
				yield line

	def signal_handler(self, sig, frame) -> None:
		logging.error('Build interruption occurred. Cleaning console.')
		subprocess.call(['stty', 'sane'])
