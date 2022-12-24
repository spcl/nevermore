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
  

static void pp_client(const char *servername, int port )
{
	 struct sockaddr_in servaddr;
	char msg[sizeof "wait123"];
	int n;
	int sockfd = -1, connfd;
    
 
  
// Creating socket file descriptor
  if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
      perror("socket creation failed");
      exit(EXIT_FAILURE);
  }

  memset(&servaddr, 0, sizeof(servaddr));
 
    
  // Filling server information
  servaddr.sin_family    = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = inet_addr(servername);
  servaddr.sin_port = htons(port);
 

	sendto(sockfd, "real123", sizeof msg,
        MSG_DONTWAIT, (const struct sockaddr *) &servaddr,  sizeof(servaddr));

	std::this_thread::sleep_for(std::chrono::seconds(5000));

out:
	close(sockfd); 
}

static void pp_server( int port )
{
 
	struct sockaddr_in servaddr, cliaddr;
	char msg[sizeof "wait123"];
 
	int sockfd = -1, connfd;
   
 
// Creating socket file descriptor
  if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
      perror("socket creation failed");
      exit(EXIT_FAILURE);
  }

  memset(&servaddr, 0, sizeof(servaddr));
  memset(&cliaddr, 0, sizeof(cliaddr));
    
  // Filling server information
  servaddr.sin_family    = AF_INET; // IPv4
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(port);
    
  // Bind the socket with the server address
  if ( bind(sockfd, (const struct sockaddr *)&servaddr, 
          sizeof(servaddr)) < 0 )
  {
      perror("bind failed");
      exit(EXIT_FAILURE);
  }

  socklen_t len = 0;

  int n = recvfrom(sockfd, (char *)msg, sizeof msg, 
                MSG_WAITALL, ( struct sockaddr *) &cliaddr,
                &len);

	printf("Get message: %s \n",msg);	


	recvfrom(sockfd, (char *)msg, sizeof msg, 
                MSG_WAITALL, ( struct sockaddr *) &cliaddr,
                &len);


	printf("Get message: %s \n",msg);	

out:
	close(connfd); 
}


static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -p, --port=<port>      listen on/connect to port <port> (default 18515)\n");

}

int main(int argc, char *argv[])
{
	char                    *servername = NULL;
	unsigned int             port = 18515;
	int			 gidx = -1;
	char			 gid[33];


	while (1) {
		int c;

		static struct option long_options[] = {
			{  "port",  required_argument, 0 , 'p' },
			{ 0,0,0,0 }
		};

		c = getopt_long(argc, argv, "p:t",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'p':
			port = strtoul(optarg, NULL, 0);
			if (port > 65535) {
				usage(argv[0]);
				return 1;
			}
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


	if (servername)
		pp_client(servername, port);
	else
		pp_server(port);
	


	return 0;
}

