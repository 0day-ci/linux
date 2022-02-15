======================
VFIO AP Locks Overview
======================
This document describes the locks that are pertinent to the secure operation
of the vfio_ap device driver. Throughout this document, the following variables
will be used to denote instances of the structures herein described:

struct ap_matrix_dev *matrix_dev;
struct ap_matrix_mdev *matrix_mdev;
struct kvm *kvm;

The Matrix Devices Lock (drivers/s390/crypto/vfio_ap_private.h)
--------------------------------------------------------------

struct ap_matrix_dev {
	...
	struct list_head mdev_list;
	struct mutex mdevs_lock;
	...
}

The Matrix Devices Lock (matrix_dev->mdevs_lock) is implemented as a global
mutex contained within the single instance of struct ap_matrix_dev. This lock
controls access to all fields contained within each matrix_mdev instance under
the control of the vfio_ap device driver (matrix_dev->mdev_list). This lock must
be held while reading from, writing to or using the data from a field contained
within a matrix_mdev instance representing one of the vfio_ap device driver's
mediated devices.

The KVM Lock (include/linux/kvm_host.h)
---------------------------------------

struct kvm {
	...
	struct mutex lock;
	...
}

The KVM Lock (kvm->lock) controls access to the state data for a KVM guest. This
lock must be held by the vfio_ap device driver while one or more AP adapters,
domains or control domains are being plugged into or unplugged from the guest.

The vfio_ap device driver registers a function to be notified when the pointer
to the kvm instance has been set. The KVM pointer is passed to the handler by
the notifier and is stored in the in the matrix_mdev instance
(matrix_mdev->kvm = kvm) containing the state of the mediated device that has
been passed through to the KVM guest.

The Guests Lock (drivers/s390/crypto/vfio_ap_private.h)
-----------------------------------------------------------

struct ap_matrix_dev {
	...
	struct list_head mdev_list;
	struct mutex guests_lock;
	...
}

The Guests Lock (matrix_dev->guests_lock) controls access to the
matrix_mdev instances (matrix_dev->mdev_list) that represent mediated devices
that hold the state for the mediated devices that have been passed through to a
KVM guest. This lock must be held:

1. To control access to the KVM pointer (matrix_mdev->kvm) while the vfio_ap
   device driver is using it to plug/unplug AP devices passed through to the KVM
   guest.

2. To add matrix_mdev instances to or remove them from matrix_dev->mdev_list.
   This is necessary to ensure the proper locking order when the list is perused
   to find an ap_matrix_mdev instance for the purpose of plugging/unplugging
   AP devices passed through to a KVM guest.

   For example, when a queue device is removed from the vfio_ap device driver,
   if the adapter is passed through to a KVM guest, it will have to be
   unplugged. In order to figure out whether the adapter is passed through,
   the matrix_mdev object to which the queue is assigned will have to be
   found. The KVM pointer (matrix_mdev->kvm) can then be used to determine if
   the mediated device is passed through (matrix_mdev->kvm != NULL) and if so,
   to unplug the adapter.

It is not necessary to take the Guests Lock to access the KVM pointer if the
pointer is not used to plug/unplug devices passed through to the KVM guest;
however, in this case, the Matrix Devices Lock (matrix_dev->mdevs_lock) must be
held in order to access the KVM pointer since it set and cleared under the
protection of the Matrix Devices Lock. A case in point is the function that
handles interception of the PQAP(AQIC) instruction sub-function. This handler
needs to access the KVM pointer only for the purposes of setting or clearing IRQ
resources, so only the matrix_dev->mdevs_lock needs to be held.

The PQAP Hook Lock (arch/s390/include/asm/kvm_host.h)
-----------------------------------------------------

typedef int (*crypto_hook)(struct kvm_vcpu *vcpu);

struct kvm_s390_crypto {
	...
	struct rw_semaphore pqap_hook_rwsem;
	crypto_hook *pqap_hook;
	...
};

The PQAP Hook Lock is a r/w semaphore that controls access to the function
pointer of the handler (*kvm->arch.crypto.pqap_hook) to invoke when the
PQAP(AQIC) instruction sub-function is intercepted by the host. The lock must be
held in write mode when pqap_hook value is set, and in read mode when the
pqap_hook function is called.

Locking Order
-------------

If the various locks are not taken in the proper order, it could potentially
result in a lockdep splat. The proper order for taking locks depends upon
the operation taking place, but in general the Guests Lock
(matrix_dev->guests_lock) must be taken outside of the KVM Lock (kvm->lock)
which in turn must be taken outside of the Matrix Devices Lock
(matrix_dev->mdevs_lock).

The following describes the various operations under which the various locks are
taken, the purpose for taking them and the order in which they must be taken.

* Operations: Setting or clearing the KVM pointer (matrix_mdev->kvm):

  1. PQAP Hook Lock (kvm->arch.crypto.pqap_hook_rwsem):

	This semaphore must be held in write mode while setting or clearing the
	reference to the function pointer (kvm->arch.crypt.pqap_hook) to call
	when the PQAP(AQIC) instruction sub-function is intercepted by the host.
	The function pointer is set when the KVM pointer is being set and
	cleared when the KVM pointer is being cleared.

  2.Guests Lock (matrix_dev->guests_lock):

	This mutex must be held while accessing the KVM pointer
	(matrix_mdev->kvm) to plug/unplug AP devices passed through to the
	KVM guest

  3. KVM Lock (kvm->lock):

	This mutex must be held while the AP devices passed through to the KVM
	guest are plugged/unplugged.

  4. Matrix Devices Lock (matrix_dev->mdevs_lock)

	This lock must be held to prevent access to the matrix_mdev state
	while writing/reading state values during the operation.

* Operations: Assign or unassign an adapter, domain or control domain of a
	      mediated device under the control of the vfio_ap device driver:

  1. Guests Lock (matrix_dev->guests_lock):

	This mutex must be held while accessing the KVM pointer
	(matrix_dev->kvm) to plug/unplug AP devices passed through to the
	KVM guest as a result of the assignment/unassignment operation.
	Assignment of an AP device may result in additional queue devices
	or control domains being plugged into the guest. Similarly, unassignment
	may result in unplugging queue devices or control domains from the
	guest

  3. KVM Lock (matrix_mdev->kvm->lock):

	This mutex must be held while the AP devices passed through to the KVM
	guest are plugged in or unplugged.

  4. Matrix Devices Lock (matrix_dev->mdevs_lock)

	This lock must be held to prevent access to the matrix_mdev state
	while writing/reading state values during the operation. For example, to
	determine which AP devices need to be plugged/unplugged, the lock
	must be held to prevent other operations from changing the data used
	to construct the guest's AP configuration.

* Operations: Probe or remove an AP queue device:

  When a queue device is bound to the vfio_ap device driver, the driver's probe
  callback is invoked. Similarly, when a queue device is unbound from the
  driver it's remove callback is invoked. The probe and remove functions will
  take locks in the following order:

  1. Guests Lock (matrix_dev->guests_lock):

	This mutex must be held for the duration of this operation.

	At the time of the operation, the vfio_ap device driver will only have
	the APQN of the queue being probed or removed, so the
	matrix_dev->mdevs_list must be perused to locate the matrix_mdev
	instance to which the queue is assigned. The Guests Lock must be held
	during this time to prevent the list from being changed while processing
	the probe/remove.

	Once the matrix_mdev is found, the operation must determine whether the
	mediated device is passed through to a guest (matrix_mdev->kvm != NULL),
	then use the KVM pointer to perform the plug/unplug operation. Here
	again, the lock must be held to prevent other operations from accessing
	the KVM pointer for the same purpose.

  2. KVM Lock (kvm->lock):

	This mutex must be held while the AP devices passed through to the KVM
	guest are plugged in or unplugged to prevent other operations from
	accessing the guest's state while it is in flux.

  3. Matrix Devices Lock (matrix_dev->mdevs_lock)

	This lock must be held to prevent access to the matrix_mdev state
	while writing/reading state values during the operation, such as the
	masks used to construct the KVM guest's AP configuration.

* Operations: Probe or remove a mediated device:

  1. Guests Lock (matrix_dev->guests_lock):

	This mutex must be held while adding the matrix_mdev to the
	matrix_dev->mdev_list during the probe operation or when removing it
	from the list during the remove operation. This is to prevent access by
	other functions that must traverse the list to find a matrix_mdev for
	the purpose of plugging/unplugging AP devices passed through to a KVM
	guest (i.e., probe/remove queue callbacks), while the list is being
	modified.

  2. Matrix Devices Lock (matrix_dev->mdevs_lock)

	This lock must be held to prevent access to the matrix_mdev state
	while writing/reading state values during the probe or remove operations
	such as initializing the hashtable of queue devices
	(matrix_mdev->qtable.queues) assigned to the matrix_mdev.

* Operation: Handle interception of the PQAP(AQIC) instruction sub-function:

  1. PQAP Hook Lock (kvm->arch.crypto.pqap_hook_rwsem)

	This semaphore must be held in read mode while retrieving the function
	pointer (kvm->arch.crypto.pqap_hook) and executing the function that
	handles the interception of the PQAP(AQIC) instruction sub-function by
	the host.

  2. Matrix Devices Lock (matrix_dev->mdevs_lock)

	This lock must be held to prevent access to the matrix_mdev state
	while writing/reading state values during the execution of the
	PQAP(AQIC) instruction sub-function interception handler. For example,
	the handler must iterate over the matrix_mdev->qtable.queues hashtable
	to find the vfio_ap_queue object representing the queue for which
	interrupts are being enabled or disabled.

  Note: It is not necessary to take the Guests Lock (matrix_dev->guests_lock)
	or the KVM Lock (matrix_mdev->kvm->lock) because the KVM pointer
	will not be accessed to plug/unplug AP devices passed through to the
	guest; it will only be used to allocate or free resources for processing
	interrupts.

* Operation: Handle AP configuration changed notification:

  The vfio_ap device driver registers a callback function to be notified when
  the AP bus detects that the host's AP configuration has changed. This can
  occur due to the addition or removal of AP adapters, domains or control
  domains via an SE or HMC connected to a DPM enabled LPAR. The objective of the
  handler is to remove the queues no longer accessible via the host in bulk
  rather than one queue at a time via the driver's queue device remove callback.
  The locks and the order in which they must be taken by this operation are:

 1. Guests Lock (matrix_dev->guests_lock):

	This mutex must be held for the duration of the operation to:

	* Iterate over the matrix_dev->mdev_list to find each matrix_mdev from
	  which a queue device to be removed is assigned and prevent other
	  operations from modifying the list while processing the affected
	  matrix_mdev instances.

	* Prevent other operations from acquiring access to the KVM pointer in
	  each affected matrix_mdev instance (matrix_mdev->kvm) for the purpose
	  of plugging/unplugging AP devices passed through to the KVM guest via
	  that instance.

2. KVM Lock (kvm->lock):

	This mutex must be held for each affected matrix_mdev instance while
	the AP devices passed through to the KVM guest are unplugged to prevent
	other operations from accessing the guest's state while it is in flux.

	Note: This lock must be re-acquired for each matrix_mdev instance.

  3. Matrix Devices Lock (matrix_dev->mdevs_lock)

	This lock must be held for each affected matrix_mdev to prevent access
	to the matrix_mdev state while writing/reading state values during the
	operation, such as the masks used to construct the KVM guest's AP
	configuration.

	Note: This lock must be re-acquired for each matrix_mdev instance.

Operation: Handle AP bus scan complete notification:

  The vfio_ap device driver registers a callback function to be notified when
  the AP bus scan completes after detecting the addition or removal of AP
  adapters, domains or control domains. The objective of the handler is t
  add the new queues accessible via the host in bulk rather than one queue
  at a time via the driver's queue device probe callback. The locks and the
  order in which they must be taken by this operation are:

  1. Guests Lock (matrix_dev->guests_lock):

	This mutex must be held for the duration of the operation to:

	* Iterate over the matrix_dev->mdev_list to find each matrix_mdev to
	  which a queue device added is assigned and prevent other operations
	  from modifying the list while processing each affected matrix_mdev
	  object.

	* Prevent other operations from acquiring access to the KVM pointer in
	  each affected matrix_mdev instance (matrix_mdev->kvm) for the purpose
	  of plugging/unplugging AP devices passed through to the KVM guest via
	  that instance.

  2. KVM Lock (kvm->lock):

	This mutex must be held for each affected matrix_mdev instance while
	the AP devices passed through to the KVM guest are plugged in to prevent
	other operations from accessing the guest's state while it is in flux.

	Note: This lock must be re-acquired for each matrix_mdev instance.

  3. Matrix Devices Lock (matrix_dev->mdevs_lock):

	This lock must be held for each affected matrix_mdev to prevent access
	to the matrix_mdev state while writing/reading state values during the
	operation, such as the masks used to construct the KVM guest's AP
	configuration.

	Note: This lock must be re-acquired for each matrix_mdev instance.

Operation: Handle resource in use query:

  The vfio_ap device driver registers a callback function with the AP bus to be
  called when changes to the bus's sysfs /sys/bus/ap/apmask or
  /sys/bus/ap/aqmask attributes would result in one or more AP queue devices
  getting unbound from the vfio_ap device driver to verify none of them are in
  use by the driver (i.e., assigned to a matrix_mdev instance). If this function
  is called while an adapter or domain is also being assigned to a mediated
  device, this could result in a deadlock; for example:

  1. A system administrator assigns an adapter to a mediated device under the
     control of the vfio_ap device driver. The driver will need to first take
     the matrix_dev->guests_lock to potentially hot plug the adapter into
     the KVM guest.
  2. At the same time, a system administrator sets a bit in the sysfs
     /sys/bus/ap/ap_mask attribute. To complete the operation, the AP bus
     must:
     a. Take the ap_perms_mutex lock to update the object storing the values
        for the /sys/bus/ap/ap_mask attribute.
     b. Call the vfio_ap device driver's in-use callback to verify that no
        queues now being reserved for the default zcrypt drivers are
        in use by the vfio_ap device driver. To do the verification, the in-use
        callback function takes the matrix_dev->guests_lock, but has to wait
        because it is already held by the operation in 1 above.
  3. The vfio_ap device driver calls an AP bus function to verify that the
     new queues resulting from the assignment of the adapter in step 1 are
     not reserved for the default zcrypt device driver. This AP bus function
     tries to take the ap_perms_mutex lock but gets stuck waiting for the
     it due to step 2a above.

    Consequently, we have the following deadlock situation:

    matrix_dev->guests_lock locked (1)
    ap_perms_mutex lock locked (2a)
    Waiting for matrix_dev->lock (2b) which is currently held (1)
    Waiting for ap_perms_mutex lock (3) which is currently held (2a)

  To prevent the deadlock scenario, the in_use operation will take the
  required locks using the mutex_trylock() function and if the lock can not be
  acquired will terminate and return -EBUSY to indicate the driver is busy
  processing another request.

  The locks required to respond to the handle resource in use query and the
  order in which they must be taken are:

  1. Guests Lock (matrix_dev->guests_lock):

  This mutex must be held for the duration of the operation to iterate over the
  matrix_dev->mdev_list to determine whether any of the queues to be unbound
  are assigned to a matrix_mdev instance.

  2. Matrix Devices Lock (matrix_dev->mdevs_lock):

  This mutex must be held for the duration of the operation to ensure that the
  AP configuration of each matrix_mdev instance does not change while verifying
  that none of the queue devices to be removed from the vfio_ap driver are
  assigned to it.
