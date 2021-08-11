#
# gdb helper commands and functions for Linux kernel debugging
#
#  load kernel and module symbols
#
# Copyright (c) Siemens AG, 2011-2013
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL version 2.
#

import gdb
import os
import re
import signal

from linux import modules, utils


if hasattr(gdb, 'Breakpoint'):

    class BreakpointWrapper(gdb.Breakpoint):
        def __init__(self, callback, **kwargs):
            super(BreakpointWrapper, self).__init__(internal=True, **kwargs)
            self.silent = True
            self.callback = callback

        def stop(self):
            self.callback()
            return False

class LxSymbols(gdb.Command):
    """(Re-)load symbols of Linux kernel and currently loaded modules.

The kernel (vmlinux) is taken from the current working directly. Modules (.ko)
are scanned recursively, starting in the same directory. Optionally, the module
search path can be extended by a space separated list of paths passed to the
lx-symbols command."""

    def __init__(self):
        super(LxSymbols, self).__init__("lx-symbols", gdb.COMMAND_FILES,
                                        gdb.COMPLETE_FILENAME)
        self.module_paths = []
        self.module_files = []
        self.module_files_updated = False
        self.loaded_modules = {}
        self.internal_breakpoints = []

    # prepare GDB for loading/unloading a module
    def _prepare_for_module_load_unload(self):

        self.blocked_sigint = False

        # block SIGINT during execution to avoid gdb crash
        sigmask = signal.pthread_sigmask(signal.SIG_BLOCK, [])
        if not signal.SIGINT in sigmask:
            self.blocked_sigint = True
            signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT})

        # disable all breakpoints to workaround a GDB bug where it would
        # not correctly resume from an internal breakpoint we placed
        # in do_module_init/free_module (it leaves the int3
        self.saved_breakpoints = []
        if hasattr(gdb, 'breakpoints') and not gdb.breakpoints() is None:
            for bp in gdb.breakpoints():
                self.saved_breakpoints.append({'breakpoint': bp, 'enabled': bp.enabled})
                bp.enabled = False

        # disable pagination to avoid asking user for continue
        show_pagination = gdb.execute("show pagination", to_string=True)
        self.saved_pagination = show_pagination.endswith("on.\n")
        gdb.execute("set pagination off")

    def _unprepare_for_module_load_unload(self):
        # restore breakpoint state
        for breakpoint in self.saved_breakpoints:
            breakpoint['breakpoint'].enabled = breakpoint['enabled']

        # restore pagination state
        gdb.execute("set pagination %s" % ("on" if self.saved_pagination else "off"))

        # unblock SIGINT
        if self.blocked_sigint:
            sigmask = signal.pthread_sigmask(signal.SIG_UNBLOCK, {signal.SIGINT})
            self.blocked_sigint = False

    def _update_module_files(self):
        self.module_files = []
        for path in self.module_paths:
            gdb.write("scanning for modules in {0}\n".format(path))
            for root, dirs, files in os.walk(path):
                for name in files:
                    if name.endswith(".ko") or name.endswith(".ko.debug"):
                        self.module_files.append(root + "/" + name)
        self.module_files_updated = True

    def _get_module_file(self, module_name):
        module_pattern = ".*/{0}\.ko(?:.debug)?$".format(
            module_name.replace("_", r"[_\-]"))
        for name in self.module_files:
            if re.match(module_pattern, name) and os.path.exists(name):
                return name
        return None

    def _section_arguments(self, module):
        try:
            sect_attrs = module['sect_attrs'].dereference()
        except gdb.error:
            return ""
        attrs = sect_attrs['attrs']
        section_name_to_address = {
            attrs[n]['battr']['attr']['name'].string(): attrs[n]['address']
            for n in range(int(sect_attrs['nsections']))}
        args = []
        for section_name in [".data", ".data..read_mostly", ".rodata", ".bss",
                             ".text", ".text.hot", ".text.unlikely"]:
            address = section_name_to_address.get(section_name)
            if address:
                args.append(" -s {name} {addr}".format(
                    name=section_name, addr=str(address)))
        return "".join(args)

    def _do_load_module_symbols(self, module):
        module_name = module['name'].string()
        module_addr = str(module['core_layout']['base']).split()[0]

        module_file = self._get_module_file(module_name)
        if not module_file and not self.module_files_updated:
            self._update_module_files()
            module_file = self._get_module_file(module_name)

        if module_file:
            if utils.is_target_arch('s390'):
                # Module text is preceded by PLT stubs on s390.
                module_arch = module['arch']
                plt_offset = int(module_arch['plt_offset'])
                plt_size = int(module_arch['plt_size'])
                module_addr = hex(int(module_addr, 0) + plt_offset + plt_size)
            gdb.write("loading @{addr}: {filename}\n".format(
                addr=module_addr, filename=module_file))
            cmdline = "add-symbol-file {filename} {addr}{sections}".format(
                filename=module_file,
                addr=module_addr,
                sections=self._section_arguments(module))
            gdb.execute(cmdline, to_string=True)

            self.loaded_modules[module_name] = {"module_file": module_file,
                                                "module_addr": module_addr}
        else:
            gdb.write("no module object found for '{0}'\n".format(module_name))


    def load_module_symbols(self):
        module = gdb.parse_and_eval("mod")

                # module already loaded, false alarm
        # can happen if 'do_init_module' breakpoint is hit multiple times
        # due to interrupts
        module_name = module['name'].string()
        if module_name in self.loaded_modules:
            gdb.write("spurious module load breakpoint\n")
            return

        # enforce update if object file is not found
        self.module_files_updated = False
        self._prepare_for_module_load_unload()
        try:
            self._do_load_module_symbols(module)
        finally:
            self._unprepare_for_module_load_unload()


    def unload_module_symbols(self):
        module = gdb.parse_and_eval("mod")
        module_name = module['name'].string()

        # module already unloaded, false alarm
        # can happen if 'free_module' breakpoint is hit multiple times
        # due to interrupts
        if not module_name in self.loaded_modules:
            gdb.write("spurious module unload breakpoint\n")
            return

        module_file = self.loaded_modules[module_name]["module_file"]
        module_addr = self.loaded_modules[module_name]["module_addr"]

        self._prepare_for_module_load_unload()
        try:
            gdb.write("unloading @{addr}: {filename}\n".format(
                addr=module_addr, filename=module_file))
            cmdline = "remove-symbol-file {filename}".format(
                filename=module_file)
            gdb.execute(cmdline, to_string=True)
            del self.loaded_modules[module_name]

        finally:
            self._unprepare_for_module_load_unload()

    def load_all_symbols(self):
        gdb.write("loading vmlinux\n")

        self._prepare_for_module_load_unload()
        try:
            # drop all current symbols and reload vmlinux
            orig_vmlinux = 'vmlinux'
            for obj in gdb.objfiles():
                if obj.filename.endswith('vmlinux'):
                    orig_vmlinux = obj.filename
            gdb.execute("symbol-file", to_string=True)
            gdb.execute("symbol-file {0}".format(orig_vmlinux))
            self.loaded_modules = {}
            module_list = modules.module_list()
            if not module_list:
                gdb.write("no modules found\n")
            else:
                [self._do_load_module_symbols(module) for module in module_list]
        finally:
            self._unprepare_for_module_load_unload()

        self._unprepare_for_module_load_unload()

    def invoke(self, arg, from_tty):
        self.module_paths = [os.path.abspath(os.path.expanduser(p))
                             for p in arg.split()]
        self.module_paths.append(os.getcwd())

        try:
            gdb.parse_and_eval("*start_kernel").fetch_lazy()
        except gdb.MemoryError:
            gdb.write("Error: Kernel is not yet loaded\n")
            return

        # enforce update
        self.module_files = []
        self.module_files_updated = False

        for bp in self.internal_breakpoints:
            bp.delete()
        self.internal_breakpoints = []

        self.load_all_symbols()

        if hasattr(gdb, 'Breakpoint'):
            self.internal_breakpoints.append(
                BreakpointWrapper(self.load_module_symbols,
                                  spec="kernel/module.c:do_init_module",
                                  ))
            self.internal_breakpoints.append(
                BreakpointWrapper(self.unload_module_symbols,
                                  spec="kernel/module.c:free_module",
                                  ))
        else:
            gdb.write("Note: symbol update on module loading not supported "
                      "with this gdb version\n")


LxSymbols()
