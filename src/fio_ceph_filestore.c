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
#include "global/global_init.h"

#define AIO_EVENT_LOCKING 
#ifdef AIO_EVENT_LOCKING
#include <pthread.h>
#endif

#include "../../fio/fio.h"

#if 0
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
#endif


////////////////////////////

struct rbd_data {
        struct io_u **aio_events;
        unsigned int queued;
        uint32_t events;
	char *osd_path;
	char *journal_path;
	ObjectStore *fs;
#ifdef AIO_EVENT_LOCKING
        pthread_mutex_t aio_event_lock;
        pthread_cond_t aio_event_cond;
#endif

};


/////////////////////////////


struct OnCommitted : public Context {
  struct io_u *io_u;
  OnCommitted(struct io_u* io_u) : io_u(io_u) {}
  void finish(int r) {
  }
};
struct OnApplied : public Context {
  struct io_u *io_u;
  ObjectStore::Transaction *t;
  OnApplied(struct io_u* io_u, ObjectStore::Transaction *t) : io_u(io_u), t(t) {}
  void finish(int r) {
    struct rbd_data *rbd_data = (struct rbd_data *) io_u->engine_data;

#ifdef AIO_EVENT_LOCKING
        pthread_mutex_lock(&(rbd_data->aio_event_lock));
#endif
        dprint(FD_IO, "%s:  aio_events[%d] = %p r:%d\n", __func__, rbd_data->events, io_u, r);
	//cout << "aio_events(" << io_u << "): " << r << std::endl;
        rbd_data->aio_events[rbd_data->events] = io_u;
        rbd_data->events++;

#ifdef AIO_EVENT_LOCKING
        pthread_cond_signal(&(rbd_data->aio_event_cond));
        pthread_mutex_unlock(&(rbd_data->aio_event_lock));
#endif

	delete t;
  }
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
        struct rbd_data *rbd_data = (struct rbd_data*) td->io_ops->data;
        dprint(FD_IO, "%s: event:%d\n", __func__, event);
        
        return rbd_data->aio_events[event];
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
        struct rbd_data *rbd_data = (struct rbd_data*) td->io_ops->data;
        int events = 0;

        dprint(FD_IO, "%s\n", __func__);

#ifdef AIO_EVENT_LOCKING
        pthread_mutex_lock(&(rbd_data->aio_event_lock));
#endif

        while (rbd_data->events < min) {
#ifdef AIO_EVENT_LOCKING
                pthread_cond_wait(&(rbd_data->aio_event_cond), &(rbd_data->aio_event_lock));
#else
                usleep(10000);
#endif
        }



        events = rbd_data->events;
        rbd_data->events -= events;

#ifdef AIO_EVENT_LOCKING
        pthread_mutex_unlock(&(rbd_data->aio_event_lock));
#endif


	return events;
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


	struct rbd_data *rbd_data = (struct rbd_data *) td->io_ops->data;
	char buf[32];
	snprintf(buf, sizeof(buf), "XXX_%lu_%lu", io_u->start_time.tv_usec, io_u->start_time.tv_sec);
	sobject_t poid(object_t(buf), 0);
	ObjectStore *fs = rbd_data->fs;

#if 0
	int bytes = 42;
	int pos = io_u->offset;
	buffer::ptr bp(bytes);
	bp.zero();
	bufferlist bl;
	bl.push_back(bp);
#else
	uint64_t len = io_u->xfer_buflen;
	uint64_t off = io_u->offset;
	bufferlist data;
	data.append((char *)io_u->xfer_buf, io_u->xfer_buflen);


#endif


	ObjectStore::Transaction *t = new ObjectStore::Transaction;
	if (!t) {

		cout << "ObjectStore Transcation allocation failed." << std::endl;
		goto failed;
	}


#if 0
	t->write(coll_t(), hobject_t(poid), pos, bytes, bl);
	fs->queue_transaction(NULL, t, new OnApplied(pos), new OnCommitted(pos));
#else
        io_u->engine_data = rbd_data;
        if (io_u->ddir == DDIR_WRITE) {
		t->write(coll_t(), hobject_t(poid), off, len, data);
		//cout << "QUEUING transaction " << io_u << std::endl;
		fs->queue_transaction(NULL, t, new OnApplied(io_u, t), new OnCommitted(io_u));
	} else {
		cout << "WARNING: No DDIR beside DDIR_WRITE supported!" << std::endl;
		return FIO_Q_COMPLETED;
	}

#endif

        rbd_data->queued++;
        return FIO_Q_QUEUED;

failed:

        io_u->error = -1;
        td_verror(td, io_u->error, "xfer");
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
        struct rbd_data *rbd_data;
	vector<const char*> args;

        rbd_data = (struct rbd_data *) malloc(sizeof(struct rbd_data));
        memset(rbd_data, 0, sizeof(struct rbd_data));
        rbd_data->aio_events = (struct io_u**)  malloc(td->o.iodepth * sizeof(struct io_u *));
        memset(rbd_data->aio_events, 0, td->o.iodepth * sizeof(struct io_u *));
        td->io_ops->data = rbd_data;

	
	global_init(NULL, args, CEPH_ENTITY_TYPE_OSD, CODE_ENVIRONMENT_UTILITY, 0);
	//g_conf->journal_dio = false;
	common_init_finish(g_ceph_context);
	//g_ceph_context->_conf->set_val("debug_filestore", "20");
	//g_ceph_context->_conf->set_val("debug_throttle", "20");
	g_ceph_context->_conf->apply_changes(NULL);


#ifdef AIO_EVENT_LOCKING
        pthread_mutex_init(&(rbd_data->aio_event_lock), NULL);
        pthread_cond_init(&(rbd_data->aio_event_cond), NULL);
#endif  


	rbd_data->osd_path = strdup("/mnt/fio_ceph_filestore.XXXXXXX");
	rbd_data->journal_path = strdup("/var/lib/ceph/osd/journal-ram/fio_ceph_filestore.XXXXXXX");

	mkdtemp(rbd_data->osd_path);
	//mktemp(rbd_data->journal_path); // NOSPC issue

  	ObjectStore *fs = new FileStore(rbd_data->osd_path, rbd_data->journal_path);
	rbd_data->fs = fs;

	if (fs->mkfs() < 0) {
	  cout << "mkfs failed" << std::endl;
	  return -1;
	}
	
	if (fs->mount() < 0) {
	  cout << "mount failed" << std::endl;
	  return -1;
	}

	ObjectStore::Transaction ft;
	ft.create_collection(coll_t());
	fs->apply_transaction(ft);


	return 0;
}

/*
 * This is paired with the ->init() function and is called when a thread is
 * done doing io. Should tear down anything setup by the ->init() function.
 * Not required.
 */
static void fio_ceph_filestore_cleanup(struct thread_data *td)
{
        struct rbd_data *rbd_data = (struct rbd_data*) td->io_ops->data;
	
	/* TODO: cleanup test jorunal/osd path */
	free(rbd_data->osd_path);
	free(rbd_data->journal_path);
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

	struct fio_file *f;
        if (!td->files_index) {
                add_file(td, td->o.filename ?: "rbd");
                td->o.nr_files = td->o.nr_files ?: 1;
        }
	f = td->files[0];
	f->real_file_size = 1024 * 1024;
	// THIS avoid that files get layedout
	f->filetype = FIO_TYPE_CHAR;

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

