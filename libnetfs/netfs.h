/* 

   Copyright (C) 1994, 1995, 1996, 1997, 1999, 2000 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef _HURD_NETFS_H_
#define _HURD_NETFS_H_

#include <hurd/ports.h>
#include <hurd/fshelp.h>
#include <hurd/iohelp.h>
#include <assert.h>

/* This library supports client-side network file system
   implementations.  It is analogous to the diskfs library provided for 
   disk-based filesystems.  */

struct argp;

struct protid
{
  struct port_info pi;
  
  /* User identification */
  struct iouser *user;
  
  /* Object this refers to */
  struct peropen *po;
  
  /* Shared memory I/O information. */
  memory_object_t shared_object;
  struct shared_io *mapped;
};

/* One of these is created for each open */
struct peropen
{
  off_t filepointer;
  int lock_status;
  int refcnt;
  int openstat;

  struct node *np;

  /* The parent of the translator's root node.  */
  mach_port_t root_parent;

  /* If this node is in a shadow tree, the parent of its root.  */
  mach_port_t shadow_root_parent;
  /* If in a shadow tree, its root node in this translator.  */
  struct node *shadow_root;
};

/* A unique one of these exists for each node currently in use. */
struct node
{
  struct node *next, **prevp;
  
  /* Protocol specific stuff; defined by user.  */
  struct netnode *nn;

  /* The stat information for this particular node.  */
  struct stat nn_stat;
  
  struct mutex lock;
  
  /* The number of references to this node.  */
  int references;
  
  mach_port_t sockaddr;

  int owner;
  
  struct transbox transbox;

  struct lock_box userlock;

  struct conch conch;

  struct dirmod *dirmod_reqs;
};

/* The user must define this function.  Make sure that NP->nn_stat is
   filled with the most current information.  CRED identifies the user
   responsible for the operation. NP is locked.  */
error_t netfs_validate_stat (struct node *np, struct iouser *cred);

/* The user must define this function.  This should attempt a chmod
   call for the user specified by CRED on locked node NP, to change
   the owner to UID and the group to GID.  */
error_t netfs_attempt_chown (struct iouser *cred, struct node *np,
			     uid_t uid, uid_t gid);

/* The user must define this function.  This should attempt a chauthor
   call for the user specified by CRED on locked node NP, thereby
   changing the author to AUTHOR.  */
error_t netfs_attempt_chauthor (struct iouser *cred, struct node *np,
				uid_t author);

/* The user must define this function.  This should attempt a chmod
   call for the user specified by CRED on locked node NODE, to change
   the mode to MODE.  Unlike the normal Unix and Hurd meaning of
   chmod, this function is also used to attempt to change files into
   other types.  If such a transition is attempted which is
   impossible, then return EOPNOTSUPP.  */
error_t netfs_attempt_chmod (struct iouser *cred, struct node *np,
			     mode_t mode);

/* The user must define this function.  Attempt to turn locked node NP
   (user CRED) into a symlink with target NAME.  */
error_t netfs_attempt_mksymlink (struct iouser *cred, struct node *np,
				 char *name);

/* The user must define this function.  Attempt to turn NODE (user
   CRED) into a device.  TYPE is either S_IFBLK or S_IFCHR.  NP is
   locked.  */
error_t netfs_attempt_mkdev (struct iouser *cred, struct node *np,
			     mode_t type, dev_t indexes);

/* The user may define this function.  Attempt to set the passive
   translator record for FILE to ARGZ (of length ARGZLEN) for user
   CRED. NP is locked.  */
error_t netfs_set_translator (struct iouser *cred, struct node *np,
			      char *argz, size_t argzlen);

/* The user may define this function (but should define it together
   with netfs_set_translator).  For locked node NODE with S_IPTRANS
   set in its mode, look up the name of its translator.  Store the
   name into newly malloced storage, and return it in *ARGZ; set
   *ARGZ_LEN to the total length.  */
error_t netfs_get_translator (struct node *node, char **argz,
			      size_t *argz_len);

/* The user must define this function.  This should attempt a chflags
   call for the user specified by CRED on locked node NP, to change
   the flags to FLAGS.  */
error_t netfs_attempt_chflags (struct iouser *cred, struct node *np,
			       int flags);

/* The user must define this function.  This should attempt a utimes
   call for the user specified by CRED on locked node NP, to change
   the atime to ATIME and the mtime to MTIME.  If ATIME or MTIME is
   null, then set to the current time.  */
error_t netfs_attempt_utimes (struct iouser *cred, struct node *np,
			      struct timespec *atime, struct timespec *mtime);

/* The user must define this function.  This should attempt to set the
   size of the locked file NP (for user CRED) to SIZE bytes long.  */
error_t netfs_attempt_set_size (struct iouser *cred, struct node *np,
				off_t size);

/* The user must define this function.  This should attempt to fetch
   filesystem status information for the remote filesystem, for the
   user CRED. NP is locked.  */
error_t netfs_attempt_statfs (struct iouser *cred, struct node *np,
			      struct statfs *st);

/* The user must define this function.  This should sync the locked
   file NP completely to disk, for the user CRED.  If WAIT is set,
   return only after the sync is completely finished.  */
error_t netfs_attempt_sync (struct iouser *cred, struct node *np,
			    int wait);

/* The user must define this function.  This should sync the entire
   remote filesystem.  If WAIT is set, return only after the sync is
   completely finished.  */
error_t netfs_attempt_syncfs (struct iouser *cred, int wait);

/* The user must define this function.  Lookup NAME in DIR (which is
   locked) for USER; set *NP to the found name upon return.  If the
   name was not found, then return ENOENT.  On any error, clear *NP.
   (*NP, if found, should be locked and a reference to it generated.
   This call should unlock DIR no matter what.)  */
error_t netfs_attempt_lookup (struct iouser *user, struct node *dir, 
			      char *name, struct node **np);

/* The user must define this function.  Delete NAME in DIR (which is
   locked) for USER.  */
error_t netfs_attempt_unlink (struct iouser *user, struct node *dir,
			      char *name);

/* The user must define this function.  Attempt to rename the
   directory FROMDIR to TODIR. Note that neither of the specific nodes
   are locked.  */
error_t netfs_attempt_rename (struct iouser *user, struct node *fromdir,
			      char *fromname, struct node *todir, 
			      char *toname, int excl);

/* The user must define this function.  Attempt to create a new
   directory named NAME in DIR (which is locked) for USER with mode
   MODE. */
error_t netfs_attempt_mkdir (struct iouser *user, struct node *dir,
			     char *name, mode_t mode);

/* The user must define this function.  Attempt to remove directory
   named NAME in DIR (which is locked) for USER.  */
error_t netfs_attempt_rmdir (struct iouser *user, 
			     struct node *dir, char *name);


/* The user must define this function.  Create a link in DIR with name
   NAME to FILE for USER. Note that neither DIR nor FILE are
   locked. If EXCL is set, do not delete the target.  Return EEXIST if
   NAME is already found in DIR.  */
error_t netfs_attempt_link (struct iouser *user, struct node *dir,
			    struct node *file, char *name, int excl);

/* The user must define this function.  Attempt to create an anonymous
   file related to DIR (which is locked) for USER with MODE.  Set *NP
   to the returned file upon success. No matter what, unlock DIR.  */
error_t netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			      mode_t mode, struct node **np);

/* The user must define this function.  Attempt to create a file named
   NAME in DIR (which is locked) for USER with MODE.  Set *NP to the
   new node upon return.  On any error, clear *NP.  *NP should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t netfs_attempt_create_file (struct iouser *user, struct node *dir,
				   char *name, mode_t mode, struct node **np);

/* The user must define this function.  Read the contents of locked
   node NP (a symlink), for USER, into BUF.  */
error_t netfs_attempt_readlink (struct iouser *user, struct node *np,
				char *buf);

/* The user must define this function. Locked node NP is being opened
   by USER, with FLAGS.  NEWNODE is nonzero if we just created this
   node.  Return an error if we should not permit the open to complete
   because of a permission restriction.  */
error_t netfs_check_open_permissions (struct iouser *user, struct node *np,
				      int flags, int newnode);

/* The user must define this function.  Read from the locked file NP
   for user CRED starting at OFFSET and continuing for up to *LEN
   bytes.  Put the data at DATA.  Set *LEN to the amount successfully
   read upon return.  */
error_t netfs_attempt_read (struct iouser *cred, struct node *np,
			    off_t offset, size_t *len, void *data);

/* The user must define this function.  Write to the locked file NP
   for user CRED starting at OFSET and continuing for up to *LEN bytes
   from DATA.  Set *LEN to the amount successfully written upon
   return.  */
error_t netfs_attempt_write (struct iouser *cred, struct node *np,
			     off_t offset, size_t *len, void *data);

/* The user must define this function.  Return the valid access
   types (bitwise OR of O_READ, O_WRITE, and O_EXEC) in *TYPES for
   locked file NP and user CRED.  */
error_t netfs_report_access (struct iouser *cred, struct node *np,
			     int *types);

/* The user must define this function.  Create a new user from the
   specified UID and GID arrays. */
struct iouser *netfs_make_user (uid_t *uids, int nuids,
				       uid_t *gids, int ngids);

/* The user must define this function.  Node NP has no more references;
   free all its associated storage. */
void netfs_node_norefs (struct node *np);

/* The user must define this function.  Fill the array *DATA of size
   BUFSIZE with up to NENTRIES dirents from DIR (which is locked)
   starting with entry ENTRY for user CRED.  The number of entries in
   the array is stored in *AMT and the number of bytes in *DATACNT.
   If the supplied buffer is not large enough to hold the data, it
   should be grown.  */
error_t netfs_get_dirents (struct iouser *cred, struct node *dir,
                           int entry, int nentries, char **data,
			   mach_msg_type_number_t *datacnt,
			   vm_size_t bufsize, int *amt);

/* Option parsing */

/* Parse and execute the runtime options in ARGZ & ARGZ_LEN.  EINVAL is
   returned if some option is unrecognized.  The default definition of this
   routine will parse them using NETFS_RUNTIME_ARGP. */
error_t netfs_set_options (char *argz, size_t argz_len);

/* Append to the malloced string *ARGZ of length *ARGZ_LEN a NUL-separated
   list of the arguments to this translator.  The default definition of this
   routine simply calls netfs_append_std_options.  */
error_t netfs_append_args (char **argz, unsigned *argz_len);

/* If this is defined or set to a pointer to an argp structure, it will be
   used by the default netfs_set_options to handle runtime option parsing.
   The default definition is initialized to a pointer to
   NETFS_STD_RUNTIME_ARGP.  Setting this variable is the usual way to add
   option parsing to a program using libnetfs.  */
extern struct argp *netfs_runtime_argp;

/* An argp for the standard netfs runtime options.  The default definition
   of NETFS_RUNTIME_ARGP points to this, although if the user redefines
   that, he may chain this onto his argp as well.  */
extern const struct argp netfs_std_runtime_argp;

/* An argp structure for the standard netfs command line arguments.  The
   user may call argp_parse on this to parse the command line, chain it onto
   the end of his own argp structure, or ignore it completely.  */
extern const struct argp netfs_std_startup_argp;

/* *Appends* to ARGZ & ARGZ_LEN '\0'-separated options describing the standard
   netfs option state (note that unlike netfs_get_options, ARGZ & ARGZ_LEN
   must already have a sane value).  */
error_t netfs_append_std_options (char **argz, size_t *argz_len);

/* Definitions provided by user */

/* Maximum number of symlinks to follow before returning ELOOP. */
extern int netfs_maxsymlinks;

/* Definitions provided by netfs. */

/* Given a netnode created by the user program, wraps it in a node
   structure.  The new node is not locked and has a single reference.
   If an error occurs, NULL is returned.  */
struct node *netfs_make_node (struct netnode *);

/* Whenever node->references is to be touched, this lock must be
   held.  Cf. netfs_nrele, netfs_nput, netfs_nref and netfs_drop_node.  */
extern spin_lock_t netfs_node_refcnt_lock;

/* Normally called in main.  This function sets up some of the netfs
   server's internal state.  */
void netfs_init (void);

/* Starts the netfs server.  Called after netfs_init. BOOTSTRAP is
   the bootstrap port.  FLAGS indicate how to open the underlying node
   (Cf. hurd/fsys.defs).  */
mach_port_t netfs_startup (mach_port_t bootstrap, int flags);

/* Normally called as the last function in main.  The netfs server now
   begins answering requests. This function never returns.  */
void netfs_server_loop (void);

/* Creates a new credential from USER which can be NULL on the peropen
   PO.  Returns NULL and sets errno on error.  */
struct protid *netfs_make_protid (struct peropen *po, struct iouser *user);

/* Create and return a new peropen structure on node NP with open
   flags FLAGS.  The initial values for the root_parent, shadow_root,
   and shadow_root_parent fields are copied from CONTEXT if it's
   non-zero, otherwise zeroed.  */
struct peropen *netfs_make_peropen (struct node *, int,
				    struct peropen *context);

/* Add a reference to node NP, which must be locked by the caller.  */
void netfs_nref (struct node *np);

/* Releases a node.  Drops a reference to node NP, which must not be
   locked by the caller.  If this was the last reference, drops the
   node.  The node cannot be used again without first obtaining a
   reference to it.  */
void netfs_nrele (struct node *np);

/* Puts a node back.  Drops a reference to the node NP, which must be
   locked by the caller (this lock will be released by netfs_nput).
   If this was the last reference, drops the node.  The node cannot be
   used again without first obtaining a reference to it.  */
void netfs_nput (struct node *np);

/* Called internally when no more references to node NP exist. */
void netfs_drop_node (struct node *np);

/* Called internally when no more references to a protid exit. */
void netfs_release_protid (void *);

/* Called internally when no more references to a protid exit. */
void netfs_release_peropen (struct peropen *);

/* The netfs message demuxer. */
int netfs_demuxer (mach_msg_header_t *, mach_msg_header_t *);

/* Called to ask the filesystem to shutdown.  If it returns, an error
   occurred.  FLAGS are passed to fsys_goaway. */
error_t netfs_shutdown (int flags);

extern struct port_class *netfs_protid_class;
extern struct port_class *netfs_control_class;
extern struct port_bucket *netfs_port_bucket;
extern struct node *netfs_root_node;
extern mach_port_t netfs_fsys_identity;
extern auth_t netfs_auth_server_port;


/* Mig gook. */
typedef struct protid *protid_t;


#endif /* _HURD_NETFS_H_ */
