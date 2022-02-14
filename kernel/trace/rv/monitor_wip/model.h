enum states_wip {
	preemptive = 0,
	non_preemptive,
	state_max
};

enum events_wip {
	preempt_disable = 0,
	preempt_enable,
	sched_waking,
	event_max
};

struct automaton_wip {
	char *state_names[state_max];
	char *event_names[event_max];
	char function[state_max][event_max];
	char initial_state;
	char final_states[state_max];
};

struct automaton_wip automaton_wip = {
	.state_names = {
		"preemptive",
		"non_preemptive"
	},
	.event_names = {
		"preempt_disable",
		"preempt_enable",
		"sched_waking"
	},
	.function = {
		{ non_preemptive,             -1,             -1 },
		{             -1,     preemptive, non_preemptive },
	},
	.initial_state = preemptive,
	.final_states = { 1, 0 },
};