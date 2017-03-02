/******************************************************************************
 * NICOS, the Networked Instrument Control System of the FRM-II
 * Copyright (c) 2009-2017 by the NICOS contributors (see AUTHORS)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Module authors:
 *   Georg Brandl <g.brandl@fz-juelich.de>
 *
 *****************************************************************************/

/*
   A filesystem/network sandbox for NICOS simulation processes, using Linux
   unshare() namespaces.

   Needs at least 4 arguments:

   - temporary chroot directory
   - numeric uid to change to
   - numeric gid to change to
   - name of binary to exec() in the sandboxed environment
   - all other arguments are passed as-is to exec()

   The helper does the following before exec()ing the new binary:

   - "unshare" the network namespace, which means that the new process cannot
     use existing network interfaces
   - "unshare" the mount namespace, so that we can remount readonly without
     influencing the whole world
   - bind-mount the whole filesystem hierarchy to a temporary directory
   - set all new mounts to readonly, except for temporaries
   - chroot into the new root
   - set user/group to desired values
   - exec the sandboxed binary

   A Linux kernel (and headers) version of 2.6.32 is required.
*/

/* to get access to unshare() */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <mntent.h>
#include <sys/mount.h>

/* compatibility for glibc < 2.12 */
#ifndef MS_PRIVATE
#include <linux/fs.h>
#endif


static void
make_mounts_readonly(const char *prefix)
{
    struct mntent *ent;

    /* Open list of mountpoints. */
    FILE *mounts = setmntent("/proc/self/mounts", "r");
    if (mounts == NULL)
        err(1, "could not get list of mountpoints");

    while ((ent = getmntent(mounts)) != NULL) {
        int flags;

        /* Skip mounts not in the new root dir. */
        if (strstr(ent->mnt_dir, prefix) != ent->mnt_dir)
            continue;

        /* Skip tmpfs. */
        if (!strcmp(ent->mnt_type, "tmpfs"))
          continue;

        flags = MS_BIND | MS_REMOUNT | MS_RDONLY;

        /* Restore existing options. */
        if (hasmntopt(ent, "nodev"))
            flags |= MS_NODEV;
        if (hasmntopt(ent, "noexec"))
            flags |= MS_NOEXEC;
        if (hasmntopt(ent, "nosuid"))
            flags |= MS_NOSUID;
        if (hasmntopt(ent, "noatime"))
            flags |= MS_NOATIME;
        if (hasmntopt(ent, "nodiratime"))
            flags |= MS_NODIRATIME;
        if (hasmntopt(ent, "relatime"))
            flags |= MS_RELATIME;

        if (mount(NULL, ent->mnt_dir, NULL, flags, NULL) < 0) {
            /* Certain errors are ok here. */
            if (errno != EACCES && errno != EINVAL && errno != ESTALE && errno != EPERM)
                err(1, "could not set mountpoint %s read-only", ent->mnt_dir);
        }
    }

    endmntent(mounts);
}

int
main(int argc, char **argv)
{
    char *ptr;
    long id;

    if (argc < 5)
        errx(1, "usage: %s rootdir uid gid binary [args...]", argv[0]);

    /* Set up the new mount and network namespaces. */
    if (unshare(CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWIPC) < 0)
        err(1, "could not create namespaces (is the binary setuid root?)");

    /* Make our copy of the rootfs mount (and all others) private, so
       our changes will not affect the parent namespace. */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
        err(1, "could not make mounts private");

    /* Mount the root filesystem (recursively) at the chroot target. */
    if (mount("/", argv[1], NULL, MS_BIND | MS_REC, NULL) < 0)
        err(1, "could not create bind mount at %s", argv[1]);

    /* Make all filesystems readonly, with exceptions. */
    make_mounts_readonly(argv[1]);

    /* Change to the chroot directory. */
    if (chdir(argv[1]) < 0)
        err(1, "could not chdir into new root");

    /* Change the root directory. */
    if (chroot(argv[1]) < 0)
        err(1, "could not create chroot jail");

    /* Set desired user and group IDs. */
    id = strtol(argv[3], &ptr, 10);
    if (*argv[3] == '\0' || *ptr != '\0')
        errx(1, "invalid numeric group id '%s' given", argv[3]);
    if (setgid(id) < 0)
        err(1, "could not set new group id %ld", id);

    id = strtol(argv[2], &ptr, 10);
    if (*argv[2] == '\0' || *ptr != '\0')
        errx(1, "invalid numeric user id '%s' given", argv[3]);
    if (setuid(id) < 0)
        err(1, "could not set new user id %ld", id);

    /* Execute desired process in the new environment. */
    if (execvp(argv[4], &argv[4]) < 0)
        err(1, "could not exec new process");

    /* unreachable */
    return 1;
}
