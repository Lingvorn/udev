/*
 * udevmonitor.c
 *
 * Copyright (C) 2004-2005 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 * 
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 * 
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <linux/types.h>
#include <linux/netlink.h>

#include "udev.h"
#include "udevd.h"
#include "udev_libc_wrapper.h"

static int uevent_netlink_sock;
static int udev_monitor_sock;

static int init_udev_monitor_socket(void)
{
	struct sockaddr_un saddr;
	socklen_t addrlen;
	const int feature_on = 1;
	int retval;

	memset(&saddr, 0x00, sizeof(saddr));
	saddr.sun_family = AF_LOCAL;
	/* use abstract namespace for socket path */
	strcpy(&saddr.sun_path[1], "/org/kernel/udev/monitor");
	addrlen = offsetof(struct sockaddr_un, sun_path) + strlen(saddr.sun_path+1) + 1;

	udev_monitor_sock = socket(AF_LOCAL, SOCK_DGRAM, 0);
	if (udev_monitor_sock == -1) {
		fprintf(stderr, "error getting socket, %s\n", strerror(errno));
		return -1;
	}

	/* the bind takes care of ensuring only one copy running */
	retval = bind(udev_monitor_sock, (struct sockaddr *) &saddr, addrlen);
	if (retval < 0) {
		fprintf(stderr, "bind failed, %s\n", strerror(errno));
		close(udev_monitor_sock);
		return -1;
	}

	/* enable receiving of the sender credentials */
	setsockopt(udev_monitor_sock, SOL_SOCKET, SO_PASSCRED, &feature_on, sizeof(feature_on));

	return 0;
}

static int init_uevent_netlink_sock(void)
{
	struct sockaddr_nl snl;
	int retval;

	memset(&snl, 0x00, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 0xffffffff;

	uevent_netlink_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (uevent_netlink_sock == -1) {
		fprintf(stderr, "error getting socket, %s\n", strerror(errno));
		return -1;
	}

	retval = bind(uevent_netlink_sock, (struct sockaddr *) &snl,
		      sizeof(struct sockaddr_nl));
	if (retval < 0) {
		fprintf(stderr, "bind failed, %s\n", strerror(errno));
		close(uevent_netlink_sock);
		uevent_netlink_sock = -1;
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int env = 0;
	int maxsockplus;
	fd_set readfds;
	int retval;

	if (getuid() != 0) {
		printf("need to be root, exit\n");
		exit(1);
	}

	if (argc == 2 && strstr(argv[1], "--env"))
		env = 1;

	init_uevent_netlink_sock();
	init_udev_monitor_socket();

	FD_ZERO(&readfds);
	FD_SET(uevent_netlink_sock, &readfds);
	FD_SET(udev_monitor_sock, &readfds);
	maxsockplus = udev_monitor_sock+1;

	while (1) {
		static char buf[UEVENT_BUFFER_SIZE*2];
		ssize_t buflen;
		fd_set workreadfds;

		buflen = 0;
		workreadfds = readfds;

		retval = select(maxsockplus, &workreadfds, NULL, NULL, NULL);
		if (retval < 0) {
			if (errno != EINTR)
				fprintf(stderr, "error receiving uevent message\n");
			continue;
		}

		if (FD_ISSET(uevent_netlink_sock, &workreadfds)) {
			buflen = recv(uevent_netlink_sock, &buf, sizeof(buf), 0);
			if (buflen <=  0) {
				fprintf(stderr, "error receiving uevent message\n");
				continue;
			}
			printf("UEVENT[%i] %s\n", time(NULL), buf);
		}

		if (FD_ISSET(udev_monitor_sock, &workreadfds)) {
			buflen = recv(udev_monitor_sock, &buf, sizeof(buf), 0);
			if (buflen <=  0) {
				fprintf(stderr, "error receiving udev message\n");
				continue;
			}
			printf("UDEV  [%i] %s\n", time(NULL), buf);
		}

		if (buflen == 0)
			continue;

		/* print environment */
		if (env) {
			size_t bufpos;

			/* start of payload */
			bufpos = strlen(buf) + 1;

			while (bufpos < (size_t)buflen) {
				int keylen;
				char *key;

				key = &buf[bufpos];
				keylen = strlen(key);
				if (keylen == 0)
					break;
				printf("%s\n", key);
				bufpos += keylen + 1;
			}
			printf("\n");
		}
	}

	close(uevent_netlink_sock);
	close(udev_monitor_sock);
	exit(1);
}