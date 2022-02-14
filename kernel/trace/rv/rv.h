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

extern struct mutex rv_interface_lock;

struct rv_monitor_def {
	struct list_head list;
	struct rv_monitor *monitor;
	struct dentry *root_d;
	bool enabled;
	bool reacting;
};

extern bool monitoring_on;
struct dentry *get_monitors_root(void);
void reset_all_monitors(void);
int init_rv_monitors(struct dentry *root_dir);
