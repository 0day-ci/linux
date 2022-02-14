#include <linux/mutex.h>

struct rv_interface {
	struct dentry *root_dir;
	struct dentry *monitors_dir;
};

#include "../trace.h"
#include <linux/tracefs.h>
#include <linux/rv.h>

#define rv_create_dir		tracefs_create_dir
#define rv_create_file		tracefs_create_file
#define rv_remove		tracefs_remove

#define MAX_RV_MONITOR_NAME_SIZE	100
#define MAX_RV_REACTOR_NAME_SIZE	100

extern struct mutex rv_interface_lock;

#ifdef CONFIG_RV_REACTORS
struct rv_reactor_def {
	struct list_head list;
	struct rv_reactor *reactor;
	/* protected by the monitor interface lock */
	int counter;
};
#endif

struct rv_monitor_def {
	struct list_head list;
	struct rv_monitor *monitor;
#ifdef CONFIG_RV_REACTORS
	struct rv_reactor_def *rdef;
#endif
	struct dentry *root_d;
	bool enabled;
	bool reacting;
};

extern bool monitoring_on;
struct dentry *get_monitors_root(void);
void reset_all_monitors(void);
int init_rv_monitors(struct dentry *root_dir);

#ifdef CONFIG_RV_REACTORS
extern bool reacting_on;
int reactor_create_monitor_files(struct rv_monitor_def *mdef);
int init_rv_reactors(struct dentry *root_dir);
#endif
