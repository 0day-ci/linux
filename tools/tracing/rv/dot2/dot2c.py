#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# dot2c: transform dot files into C structures.
# For more information, see:
#   https://bristot.me/efficient-formal-verification-for-the-linux-kernel/
#
# This program was written in the development of this paper:
#  de Oliveira, D. B. and Cucinotta, T. and de Oliveira, R. S.
#  "Efficient Formal Verification for the Linux Kernel." International
#  Conference on Software Engineering and Formal Methods. Springer, Cham, 2019.
#
# Copyright 2018-2020 Red Hat, Inc.
#
# Author:
#  Daniel Bristot de Oliveira <bristot@kernel.org>

from dot2.automata import Automata

class Dot2c(Automata):
    enum_states_def="states"
    enum_events_def="events"
    struct_automaton_def="automaton"
    var_automaton_def="aut"

    def __init__(self, file_path):
        super().__init__(file_path)
        self.line_length=80

    def __buff_to_string(self, buff):
        string=""

        for line in buff:
            string=string + line + "\n"

        # cut off the last \n
        return string[:-1]

    def __get_enum_states_content(self):
        buff=[]
        buff.append("\t%s = 0," % self.initial_state)
        for state in self.states:
            if state != self.initial_state:
                buff.append("\t%s," % state)
        buff.append("\tstate_max")

        return buff

    def get_enum_states_string(self):
        buff=self.__get_enum_states_content()
        return self.__buff_to_string(buff)

    def format_states_enum(self):
        buff=[]
        buff.append("enum %s {" % self.enum_states_def)
        buff.append(self.get_enum_states_string())
        buff.append("};\n")

        return buff

    def __get_enum_events_content(self):
        buff=[]
        first=True
        for event in self.events:
            if first:
                buff.append("\t%s = 0," % event)
                first=False
            else:
                buff.append("\t%s," % event)
        buff.append("\tevent_max")

        return buff

    def get_enum_events_string(self):
        buff=self.__get_enum_events_content()
        return self.__buff_to_string(buff)

    def format_events_enum(self):
        buff=[]
        buff.append("enum %s {" % self.enum_events_def)
        buff.append(self.get_enum_events_string())
        buff.append("};\n")

        return buff

    def get_minimun_type(self):
        min_type="char"

        if self.states.__len__() > 255:
            min_type="short"

        if self.states.__len__() > 65535:
            min_type="int"

        return min_type

    def format_automaton_definition(self):
        min_type = self.get_minimun_type()
        buff=[]
        buff.append("struct %s {" % self.struct_automaton_def)
        buff.append("\tchar *state_names[state_max];")
        buff.append("\tchar *event_names[event_max];")
        buff.append("\t%s function[state_max][event_max];" % min_type)
        buff.append("\t%s initial_state;" % min_type)
        buff.append("\tchar final_states[state_max];")
        buff.append("};\n")
        return buff

    def format_aut_init_header(self):
        buff=[]
        buff.append("struct %s %s = {" % (self.struct_automaton_def, self.var_automaton_def))
        return buff

    def __get_string_vector_per_line_content(self, buff):
        first=True
        string=""
        for entry in buff:
            if first:
                string = string + "\t\t\"" + entry
                first=False;
            else:
                string = string + "\",\n\t\t\"" + entry
        string = string + "\""

        return string

    def get_aut_init_events_string(self):
        return self.__get_string_vector_per_line_content(self.events)

    def get_aut_init_states_string(self):
        return self.__get_string_vector_per_line_content(self.states)

    def format_aut_init_events_string(self):
        buff=[]
        buff.append("\t.event_names = {")
        buff.append(self.get_aut_init_events_string())
        buff.append("\t},")
        return buff

    def format_aut_init_states_string(self):
        buff=[]
        buff.append("\t.state_names = {")
        buff.append(self.get_aut_init_states_string())
        buff.append("\t},")

        return buff

    def __get_max_strlen_of_states(self):
        return max(self.states, key=len).__len__()

    def __get_state_string_length(self):
        maxlen = self.__get_max_strlen_of_states()
        return "%" + str(maxlen) + "s"

    def get_aut_init_function(self):
        nr_states=self.states.__len__()
        nr_events=self.events.__len__()
        buff=[]

        strformat = self.__get_state_string_length()

        for x in range(nr_states):
            line="\t\t{ "
            for y in range(nr_events):
                if y != nr_events-1:
                    line = line + strformat % self.function[x][y] + ", "
                else:
                    line = line + strformat % self.function[x][y] + " },"
            buff.append(line)

        return self.__buff_to_string(buff)

    def format_aut_init_function(self):
        buff=[]
        buff.append("\t.function = {")
        buff.append(self.get_aut_init_function())
        buff.append("\t},")

        return buff

    def get_aut_init_initial_state(self):
        return self.initial_state

    def format_aut_init_initial_state(self):
        buff=[]
        initial_state=self.get_aut_init_initial_state()
        buff.append("\t.initial_state = " + initial_state + ",")

        return buff


    def get_aut_init_final_states(self):
        line=""
        first=True
        for state in self.states:
            if first == False:
                line = line + ', '
            else:
                first = False

            if self.final_states.__contains__(state):
                line = line + '1'
            else:
                line = line + '0'
        return line

    def format_aut_init_final_states(self):
       buff=[]
       buff.append("\t.final_states = { %s }," % self.get_aut_init_final_states())

       return buff

    def __get_automaton_initialization_footer_string(self):
        footer="};"
        return footer

    def format_aut_init_footer(self):
        buff=[]
        buff.append(self.__get_automaton_initialization_footer_string())

        return buff

    def format_model(self):
        buff=[]
        buff += self.format_states_enum()
        buff += self.format_events_enum()
        buff += self.format_automaton_definition()
        buff += self.format_aut_init_header()
        buff += self.format_aut_init_states_string()
        buff += self.format_aut_init_events_string()
        buff += self.format_aut_init_function()
        buff += self.format_aut_init_initial_state()
        buff += self.format_aut_init_final_states()
        buff += self.format_aut_init_footer()

        return buff

    def print_model_classic(self):
        buff=self.format_model()
        print(self.__buff_to_string(buff))
