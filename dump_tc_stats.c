#include <netlink/netlink.h>
#include <netlink/cache.h>
#include <netlink/route/link.h>

#include <netlink/cli/utils.h>
#include <netlink/cli/tc.h>
#include <netlink/cli/qdisc.h>
#include <netlink/cli/class.h>
#include <netlink/cli/cls.h>
#include <netlink/cli/link.h>

#include <linux/pkt_sched.h>
#include <linux/netlink.h>

#include <inttypes.h>

#include <unistd.h>

#define NUM_INDENT 4

static struct nl_sock *sock;
/*static struct nl_dump_params params = {
	.dp_type = NL_DUMP_STATS,
};*/

void callback(struct nl_object * obj, void *arg) {
    //nl_object_dump(obj, &params);
    struct rtnl_tc *tc = TC_CAST(obj);
    uint64_t pa = rtnl_tc_get_stat(tc, RTNL_TC_PACKETS);
    uint64_t by = rtnl_tc_get_stat(tc, RTNL_TC_BYTES );
    uint64_t rb = rtnl_tc_get_stat(tc, RTNL_TC_RATE_BPS);
    uint64_t rp = rtnl_tc_get_stat(tc, RTNL_TC_RATE_PPS);
    uint64_t ql = rtnl_tc_get_stat(tc, RTNL_TC_QLEN);
    uint64_t bl = rtnl_tc_get_stat(tc, RTNL_TC_BACKLOG);
    uint64_t dr = rtnl_tc_get_stat(tc, RTNL_TC_DROPS);
    uint64_t rq = rtnl_tc_get_stat(tc, RTNL_TC_REQUEUES);
    uint64_t ov = rtnl_tc_get_stat(tc, RTNL_TC_OVERLIMITS);
    printf("{");
    printf( "\"PACKETS\":%"PRIu64, pa);
    printf(",\"BYTES\":%"PRIu64, by);
    printf(",\"RATE_BPS\":%"PRIu64, rb);
    printf(",\"RATE_PPS\":%"PRIu64, rp);
    printf(",\"QLEN\":%"PRIu64, ql);
    printf(",\"BACKLOG\":%"PRIu64, bl);
    printf(",\"DROPS\":%"PRIu64, dr);
    printf(",\"REQUEUES\":%"PRIu64, rq);
    printf(",\"OVERLIMITS\":%"PRIu64, ov);
    printf("}\n");
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    
    if (argc != 2) {
        printf("Usage: dump_tc_stats <iface>\n");
        printf("Dump interface statistics periodically. Don't forget to `tc qdisc add <iface> root ...` something\n");
        return 2;
    }
    
	struct rtnl_qdisc *qdisc;
	struct rtnl_tc *tc;
	struct nl_cache *link_cache, *qdisc_cache;

	sock = nl_cli_alloc_socket();
	nl_cli_connect(sock, NETLINK_ROUTE);
	link_cache = nl_cli_link_alloc_cache(sock);
	qdisc = nl_cli_qdisc_alloc();
	tc = (struct rtnl_tc *) qdisc;
	
	nl_cli_tc_parse_dev(tc, link_cache, argv[1]);
	
	//params.dp_fd = stdout;
	
	for(;;) {
	   qdisc_cache = nl_cli_qdisc_alloc_cache(sock);
	   nl_cache_foreach_filter(qdisc_cache, OBJ_CAST(qdisc), &callback, NULL);
	   nl_cache_free(qdisc_cache);
	   usleep(1000000);
	}
}
