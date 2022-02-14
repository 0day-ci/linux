#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# dot2k: transform dot files into a monitor for the Linux kernel.
#
# For more information, see:
#   https://bristot.me/efficient-formal-verification-for-the-linux-kernel/
#
# Copyright 2018-2020 Red Hat, Inc.
#
# Author:
#  Daniel Bristot de Oliveira <bristot@kernel.org>

from dot2.dot2c import Dot2c
import platform
import os

class dot2k(Dot2c):
    monitor_types={ "global" : 1, "per_cpu" : 2, "per_task" : 3 }
    monitor_templates_dir="dot2k/rv_templates/"
    monitor_type="per_cpu"

    def __init__(self, file_path, MonitorType):
        super().__init__(file_path)

        self.monitor_type=self.monitor_types.get(MonitorType)
        if self.monitor_type == None:
            raise Exception("Unknown monitor type: %s" % MonitorType)

        self.monitor_type=MonitorType
        self.__fill_rv_templates_dir()
        self.main_h = self.__open_file(self.monitor_templates_dir + "main_" + MonitorType + ".h")
        self.main_c = self.__open_file(self.monitor_templates_dir + "main_" + MonitorType + ".c")

    def __fill_rv_templates_dir(self):

        if os.path.exists(self.monitor_templates_dir) == True:
            return

        if platform.system() != "Linux":
            raise Exception("I can only run on Linux.")

        kernel_path="/lib/modules/%s/build/tools/rv/%s" % (platform.release(), self.monitor_templates_dir)

        if os.path.exists(kernel_path) == True:
            self.monitor_templates_dir=kernel_path
            return

        if os.path.exists("/usr/share/dot2/dot2k_templates/") == True:
            self.monitor_templates_dir="/usr/share/dot2/dot2k_templates/"
            return

        raise Exception("Could not find the template directory, do you have the kernel source installed?")


    def __open_file(self, path):
        try:
            fd = open(path)
        except OSError:
            raise Exception("Cannot open the file: %s" % path)

        content = fd.read()

        return content

    def __buff_to_string(self, buff):
        string=""

        for line in buff:
            string=string + line + "\n"

        # cut off the last \n
        return string[:-1]

    def fill_monitor_h(self):
        monitor_h = self.monitor_h

        min_type=self.get_minimun_type()

        monitor_h = monitor_h.replace("MIN_TYPE", min_type)

        return monitor_h

    def fill_tracepoint_handlers_skel(self):
        buff=[]
        for event in self.events:
            buff.append("static void handle_%s(void *data, /* XXX: fill header */)" % event)
            buff.append("{")
            if self.monitor_type == "per_task":
                buff.append("\tpid_t pid = /* XXX how do I get the pid? */;");
                buff.append("\tda_handle_event_%s(pid, %s);" % (self.name, event));
            else:
                buff.append("\tda_handle_event_%s(%s);" % (self.name, event));
            buff.append("}")
            buff.append("")
        return self.__buff_to_string(buff)

    def fill_tracepoint_hook_helper(self):
        buff=[]
        for event in self.events:
            buff.append("\t{")
            buff.append("\t\t.probe = handle_%s," % event)
            buff.append("\t\t.name = /* XXX: tracepoint name here */,")
            buff.append("\t\t.registered = 0")
            buff.append("\t},")
        return self.__buff_to_string(buff)

    def fill_main_c(self):
        main_c = self.main_c
        min_type=self.get_minimun_type()
        nr_events=self.events.__len__()
        tracepoint_handlers=self.fill_tracepoint_handlers_skel()
        tracepoint_hook_helpers=self.fill_tracepoint_hook_helper()

        main_c = main_c.replace("MIN_TYPE", min_type)
        main_c = main_c.replace("MODEL_NAME", self.name)
        main_c = main_c.replace("NR_EVENTS", str(nr_events))
        main_c = main_c.replace("TRACEPOINT_HANDLERS_SKEL", tracepoint_handlers)
        main_c = main_c.replace("TRACEPOINT_HOOK_HELPERS", tracepoint_hook_helpers)

        return main_c

    def fill_main_h(self):
        main_h = self.main_h
        main_h = main_h.replace("MIN_TYPE", self.get_minimun_type())
        main_h = main_h.replace("MODEL_NAME_BIG", self.name.upper())
        main_h = main_h.replace("MODEL_NAME", self.name)

        return main_h

    def fill_model_h(self):
        #
        # Adjust the definition names
        #
        self.enum_states_def="states_%s" % self.name
        self.enum_events_def="events_%s" % self.name
        self.struct_automaton_def="automaton_%s" % self.name
        self.var_automaton_def="automaton_%s" % self.name

        buff=self.format_model()

        return self.__buff_to_string(buff)

    def __create_directory(self):
        try:
            os.mkdir(self.name)
        except FileExistsError:
            return
        except:
            print("Fail creating the output dir: %s" % self.name)

    def __create_file(self, file_name, content):
        path="%s/%s" % (self.name, file_name)
        try:
            file = open(path, 'w')
        except FileExistsError:
            return
        except:
            print("Fail creating file: %s" % path)

        file.write(content)

        file.close()

    def __get_main_name(self):
        path="%s/%s" % (self.name, "main.c")
        if os.path.exists(path) == False:
           return "main.c"
        return "__main.c"

    def print_files(self):
        main_h=self.fill_main_h()
        main_c=self.fill_main_c()
        model_h=self.fill_model_h()

        self.__create_directory()

        path="%s.h" % self.name
        self.__create_file(path, main_h)

        path="%s.c" % self.name
        self.__create_file(path, main_c)

        self.__create_file("model.h", model_h)
