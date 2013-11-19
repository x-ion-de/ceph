/*
 * Skeleton for a sample external io engine
 *
 * Should be compiled with:
 *
 * gcc -Wall -O2 -g -shared -rdynamic -fPIC -o engine.o engine.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include <iostream>
#include "os/FileStore.h"


struct thread_data;
struct io_u;
struct fio_file;
/*
 * io_ops->queue() return values
 */
enum {
        FIO_Q_COMPLETED = 0,            /* completed sync */
        FIO_Q_QUEUED    = 1,            /* queued, will complete async */
        FIO_Q_BUSY      = 2,            /* no more room, call ->commit() */
};
#define FIO_IOOPS_VERSION 16

struct flist_head {
        struct flist_head *next, *prev;
};

struct ioengine_ops {
	struct flist_head list;
	char name[16];
	int version;
	int flags;
	int (*setup)(struct thread_data *);
	int (*init)(struct thread_data *);
	int (*prep)(struct thread_data *, struct io_u *);
	int (*queue)(struct thread_data *, struct io_u *);
	int (*commit)(struct thread_data *);
	int (*getevents)(struct thread_data *, unsigned int, unsigned int, struct timespec *);
	struct io_u *(*event)(struct thread_data *, int);
	int (*cancel)(struct thread_data *, struct io_u *);
	void (*cleanup)(struct thread_data *);
	int (*open_file)(struct thread_data *, struct fio_file *);
	int (*close_file)(struct thread_data *, struct fio_file *);
	int (*get_file_size)(struct thread_data *, struct fio_file *);
	void (*terminate)(struct thread_data *);
	int (*io_u_init)(struct thread_data *, struct io_u *);
	void (*io_u_free)(struct thread_data *, struct io_u *);
	int option_struct_size;
	struct fio_option *options;
	void *data;
	void *dlhandle;
};

/*
 * The core of the module is identical to the ones included with fio,
 * read those. You cannot use register_ioengine() and unregister_ioengine()
 * for external modules, they should be gotten through dlsym()
 */

/*
 * The ->event() hook is called to match an event number with an io_u.
 * After the core has called ->getevents() and it has returned eg 3,
 * the ->event() hook must return the 3 events that have completed for
 * subsequent calls to ->event() with [0-2]. Required.
 */
static struct io_u *fio_ceph_filestore_event(struct thread_data *td, int event)
{
	return NULL;
}

/*
 * The ->getevents() hook is used to reap completion events from an async
 * io engine. It returns the number of completed events since the last call,
 * which may then be retrieved by calling the ->event() hook with the event
 * numbers. Required.
 */
static int fio_ceph_filestore_getevents(struct thread_data *td, unsigned int min,
				  unsigned int max, struct timespec *t)
{
	return 0;
}

/*
 * The ->cancel() hook attempts to cancel the io_u. Only relevant for
 * async io engines, and need not be supported.
 */
static int fio_ceph_filestore_cancel(struct thread_data *td, struct io_u *io_u)
{
	return 0;
}

/*
 * The ->queue() hook is responsible for initiating io on the io_u
 * being passed in. If the io engine is a synchronous one, io may complete
 * before ->queue() returns. Required.
 *
 * The io engine must transfer in the direction noted by io_u->ddir
 * to the buffer pointed to by io_u->xfer_buf for as many bytes as
 * io_u->xfer_buflen. Residual data count may be set in io_u->resid
 * for a short read/write.
 */
static int fio_ceph_filestore_queue(struct thread_data *td, struct io_u *io_u)
{
	/*
	 * Could return FIO_Q_QUEUED for a queued request,
	 * FIO_Q_COMPLETED for a completed request, and FIO_Q_BUSY
	 * if we could queue no more at this point (you'd have to
	 * define ->commit() to handle that.
	 */
	return FIO_Q_COMPLETED;
}

/*
 * The ->prep() function is called for each io_u prior to being submitted
 * with ->queue(). This hook allows the io engine to perform any
 * preparatory actions on the io_u, before being submitted. Not required.
 */
static int fio_ceph_filestore_prep(struct thread_data *td, struct io_u *io_u)
{
	return 0;
}

/*
 * The init function is called once per thread/process, and should set up
 * any structures that this io engine requires to keep track of io. Not
 * required.
 */
static int fio_ceph_filestore_init(struct thread_data *td)
{
  	ObjectStore *fs = new FileStore("peng", "jpeng");

	if (fs->mkfs() < 0) {
	  cout << "mkfs failed" << std::endl;
	  return -1;
	}
	
	if (fs->mount() < 0) {
	  cout << "mount failed" << std::endl;
	  return -1;
	}


	return 0;
}

/*
 * This is paired with the ->init() function and is called when a thread is
 * done doing io. Should tear down anything setup by the ->init() function.
 * Not required.
 */
static void fio_ceph_filestore_cleanup(struct thread_data *td)
{
}

/*
 * Hook for opening the given file. Unless the engine has special
 * needs, it usually just provides generic_file_open() as the handler.
 */
static int fio_ceph_filestore_open(struct thread_data *td, struct fio_file *f)
{
	return 0;
}

/*
 * Hook for closing a file. See fio_ceph_filestore_open().
 */
static int fio_ceph_filestore_close(struct thread_data *td, struct fio_file *f)
{
	return 0;
}

static int fio_ceph_filestore_setup(struct thread_data *td)
{

	printf("YEAAH\n");
        return 0;
}

/*
 * Note that the structure is exported, so that fio can get it via
 * dlsym(..., "ioengine");
 */


extern "C" {
void get_ioengine(struct ioengine_ops **ioengine_ptr) {
	struct ioengine_ops *ioengine;
	*ioengine_ptr = (struct ioengine_ops *) malloc(sizeof(struct ioengine_ops));
	ioengine = *ioengine_ptr;

	strcpy(ioengine->name, "ceph_filestore");
	ioengine->version        = FIO_IOOPS_VERSION;
	ioengine->setup          = fio_ceph_filestore_setup;
	ioengine->init           = fio_ceph_filestore_init;
	ioengine->prep           = fio_ceph_filestore_prep;
	ioengine->queue          = fio_ceph_filestore_queue;
	ioengine->cancel         = fio_ceph_filestore_cancel;
	ioengine->getevents      = fio_ceph_filestore_getevents;
	ioengine->event          = fio_ceph_filestore_event;
	ioengine->cleanup        = fio_ceph_filestore_cleanup;
	ioengine->open_file      = fio_ceph_filestore_open;
	ioengine->close_file     = fio_ceph_filestore_close;
}
}


