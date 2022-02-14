enum states_wwnr {
	not_running = 0,
	running,
	state_max
};

enum events_wwnr {
	switch_in = 0,
	switch_out,
	wakeup,
	event_max
};

struct automaton_wwnr {
	char *state_names[state_max];
	char *event_names[event_max];
	char function[state_max][event_max];
	char initial_state;
	char final_states[state_max];
};

struct automaton_wwnr automaton_wwnr = {
	.state_names = {
		"not_running",
		"running"
	},
	.event_names = {
		"switch_in",
		"switch_out",
		"wakeup"
	},
	.function = {
		{     running,          -1, not_running },
		{          -1, not_running,          -1 },
	},
	.initial_state = not_running,
	.final_states = { 1, 0 },
};