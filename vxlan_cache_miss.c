#include <sys/types.h>
#include <sys/socket.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>

/* 
 * Small program to listen for VXLAN cache misses.
 * This has no error handling, as it is a learning excercise for myself.
 * Enjoy :-)
 */
int main() {
        // Create netlink client, fill with zeros and subscribe to the RTMGRP_NEIGH to recieve RTM_GETNEIGH events (man 7 netlink)
	struct sockaddr_nl sockaddr_nl;
	memset(&sockaddr_nl, 0, sizeof(sockaddr_nl));
	sockaddr_nl.nl_family = AF_NETLINK;   
	sockaddr_nl.nl_groups = RTMGRP_NEIGH; 

        // Standard socket programming, create socket then bind to said socket. 
        // Should this be SOCK_RAW? Does it matter for AF_NETLINK?
        // TODO test CAP_NET_RAW capability...
	int rtnetlink_socket = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	bind(rtnetlink_socket, (struct sockaddr*) &sockaddr_nl, sizeof(sockaddr_nl));

        //From libnetlink.c (iproute2)
	int status;
  	struct nlmsghdr *h; //man 7 netlink
	struct sockaddr_nl nladdr;
	struct iovec iov;

        //Generic header for use with recvmsg and sendmsg, the real data goes in iov, the io vector for storing data (man 7 netlink)
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	char   buf[8192];

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	//nladdr.nl_pid = 0;
	//nladdr.nl_groups = 0;

        //Starting address of iov is buf
	iov.iov_base = buf;
	while (1) {
		iov.iov_len = sizeof(buf);
		status = recvmsg(rtnetlink_socket, &msg, 0);
                
                //"Netlink messages consist of a byte stream with one or multiple nlmsghdr headers and associated payload." (man 7 netlink)
		for (h = (struct nlmsghdr*)buf; status >= sizeof(*h); ) {
			int len = h->nlmsg_len;
                        fprintf(stderr,"Recieved message length (including header): %d bytes\n",len);
			int l = len - sizeof(*h);
                        fprintf(stderr,"Recieved message length (excluding header): %d bytes\n",l);
                        
                        //We only care about RTM_GETNEIGH messages (h->nlmsg_type = 30). This is defined in neighbour.h.
                        if (h->nlmsg_type != RTM_GETNEIGH) {
				fprintf(stderr, "Not RTM_GETNEIGH: %08x %08x %08x\n", h->nlmsg_len, h->nlmsg_type, h->nlmsg_flags);
				status -= NLMSG_ALIGN(len);
				h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(len));
				continue;
			}
                        // (N)eighbour Discovery message. (man 7 rtnetlink)
                        // NLMSG_DATA is a macro to retrieve a pointer to the payload associated with nlmsghdr (man 3 netlink)
                        struct ndmsg *r = NLMSG_DATA(h);
                        fprintf(stderr,"Message is for interface index: %d\n",r->ndm_ifindex);
                        
                        // Table of Routing Attributes. (man 7 rtnetlink)
                        // NDA_MAX is the number of (N)eighbour (D)iscovery (A)ttributes. This is defined in neighbour.h. 
                        // We could also use __NDA_MAX here as well.
                        struct rtattr *rta_table[NDA_MAX+1];
                        memset(rta_table, 0, sizeof(struct rtattr *) * (NDA_MAX + 1)); 

                        int rta_len = len - NLMSG_LENGTH(sizeof(*r));
                        fprintf(stderr, "Length of rta: %d\n",rta_len);

		        // The routing attributes come after the ndmsg Cast r to char* then offset by ndmsg, cast back rtattr*
		        struct rtattr *rta = (struct rtattr*)((char *)r + NLMSG_ALIGN(sizeof(struct ndmsg)));
		        
                        //Verify this is a routing attribute, then add to our table of attributes
                        while (RTA_OK(rta, rta_len)) {
				if ((rta->rta_type <= NDA_MAX) && (!rta_table[rta->rta_type]))
					rta_table[rta->rta_type] = rta;
                                fprintf(stderr, "Saving RTA type: %d to routing attribute table\n", rta->rta_type);
				rta = RTA_NEXT(rta,rta_len);
			}

			if (rta_table[NDA_DST]) {
                                int buflen= 256;
                                char buf[buflen];
                                inet_ntop(r->ndm_family,RTA_DATA(rta_table[NDA_DST]),buf,buflen);
				printf("%s\n",buf);
                        }
                        
			status -= NLMSG_ALIGN(len);
			h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(len));
                 }
	}
  	return 0;
}
