enum states_safe_wtd {
	init = 0,
	closed_running,
	closed_running_nwo,
	nwo,
	opened,
	opened_nwo,
	reopened,
	safe,
	safe_nwo,
	set,
	set_nwo,
	started,
	started_nwo,
	stopped,
	state_max
};

enum events_safe_wtd {
	close = 0,
	nowayout,
	open,
	other_threads,
	ping,
	set_safe_timeout,
	start,
	stop,
	event_max
};

struct automaton_safe_wtd {
	char *state_names[state_max];
	char *event_names[event_max];
	char function[state_max][event_max];
	char initial_state;
	char final_states[state_max];
};

struct automaton_safe_wtd automaton_safe_wtd = {
	.state_names = {
		"init",
		"closed_running",
		"closed_running_nwo",
		"nwo",
		"opened",
		"opened_nwo",
		"reopened",
		"safe",
		"safe_nwo",
		"set",
		"set_nwo",
		"started",
		"started_nwo",
		"stopped"
	},
	.event_names = {
		"close",
		"nowayout",
		"open",
		"other_threads",
		"ping",
		"set_safe_timeout",
		"start",
		"stop"
	},
	.function = {
		{                 -1,                nwo,             opened,               init,                 -1,                 -1,                 -1,                 -1 },
		{                 -1, closed_running_nwo,           reopened,     closed_running,                 -1,                 -1,                 -1,                 -1 },
		{                 -1, closed_running_nwo,        started_nwo, closed_running_nwo,                 -1,                 -1,                 -1,                 -1 },
		{                 -1,                nwo,         opened_nwo,                nwo,                 -1,                 -1,                 -1,                 -1 },
		{               init,                 -1,                 -1,                 -1,                 -1,                 -1,            started,                 -1 },
		{                nwo,                 -1,                 -1,                 -1,                 -1,                 -1,        started_nwo,                 -1 },
		{     closed_running,                 -1,                 -1,                 -1,                 -1,                set,                 -1,             opened },
		{     closed_running,                 -1,                 -1,                 -1,               safe,                 -1,                 -1,             stopped },
		{ closed_running_nwo,                 -1,                 -1,                 -1,           safe_nwo,                 -1,                 -1,                 -1 },
		{                 -1,                 -1,                 -1,                 -1,               safe,                 -1,                 -1,                 -1 },
		{                 -1,                 -1,                 -1,                 -1,           safe_nwo,                 -1,                 -1,                 -1 },
		{     closed_running,                 -1,                 -1,                 -1,                 -1,                set,                 -1,             stopped },
		{ closed_running_nwo,                 -1,                 -1,                 -1,                 -1,            set_nwo,                 -1,                 -1 },
		{               init,                 -1,                 -1,                 -1,                 -1,                 -1,                 -1,                 -1 },
	},
	.initial_state = init,
	.final_states = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};