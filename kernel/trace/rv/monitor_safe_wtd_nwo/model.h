enum states_safe_wtd_nwo {
	init = 0,
	closed_running,
	nwo,
	opened,
	safe,
	set,
	started,
	state_max
};

enum events_safe_wtd_nwo {
	close = 0,
	nowayout,
	open,
	other_threads,
	ping,
	set_safe_timeout,
	start,
	event_max
};

struct automaton_safe_wtd_nwo {
	char *state_names[state_max];
	char *event_names[event_max];
	char function[state_max][event_max];
	char initial_state;
	char final_states[state_max];
};

struct automaton_safe_wtd_nwo automaton_safe_wtd_nwo = {
	.state_names = {
		"init",
		"closed_running",
		"nwo",
		"opened",
		"safe",
		"set",
		"started"
	},
	.event_names = {
		"close",
		"nowayout",
		"open",
		"other_threads",
		"ping",
		"set_safe_timeout",
		"start"
	},
	.function = {
		{             -1,            nwo,             -1,             -1,             -1,             -1,             -1 },
		{             -1, closed_running,        started, closed_running,             -1,             -1,             -1 },
		{             -1,            nwo,         opened,            nwo,             -1,             -1,             -1 },
		{            nwo,             -1,             -1,             -1,             -1,             -1,        started },
		{ closed_running,             -1,             -1,             -1,           safe,             -1,             -1 },
		{             -1,             -1,             -1,             -1,           safe,             -1,             -1 },
		{ closed_running,             -1,             -1,             -1,             -1,            set,             -1 },
	},
	.initial_state = init,
	.final_states = { 1, 0, 0, 0, 0, 0, 0 },
};
