#include "verbsEP.hpp"
#include "connectRDMA.hpp"
#include <chrono>
#include "cxxopts.hpp"
#include <vector>
#include <sys/mman.h>
#include <iostream>
#include <fstream>
#include <random>
#include <thread>         // std::this_thread::sleep_for


cxxopts::ParseResult
parse(int argc, char* argv[])
{
    cxxopts::Options options(argv[0], "Server for the microbenchmark");
    options
      .positional_help("[optional args]")
      .show_positional_help();
 
  try
  {
 
    options.add_options()
      ("address", "IP address", cxxopts::value<std::string>(), "IP")
      ("help", "Print help")
     ;
 
    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
      std::cout << options.help({""}) << std::endl;
      exit(0);
    }

    if (result.count("address") == 0)
    {
      std::cout << options.help({""}) << std::endl;
      exit(0);
    }

    return result;

  } catch (const cxxopts::OptionException& e)
  {
    std::cout << "error parsing options: " << e.what() << std::endl;
    std::cout << options.help({""}) << std::endl;
    exit(1);
  }
}


int main(int argc, char* argv[]){
 
	auto allparams = parse(argc,argv);

	std::string ip = allparams["address"].as<std::string>(); // "192.168.1.20"; .c_str()

	//char *ip = "192.168.1.1";
	int port = 9999;

	ClientRDMA * client = new ClientRDMA(const_cast<char*>(ip.c_str()),port);
	struct ibv_qp_init_attr attr;
 	struct rdma_conn_param conn_param;
 
	memset(&attr, 0, sizeof(attr));
  attr.cap.max_send_wr = 2;
  attr.cap.max_recv_wr = 2;
  attr.cap.max_send_sge = 1;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;
  attr.qp_type = IBV_QPT_RC;

  memset(&conn_param, 0 , sizeof(conn_param));
  conn_param.responder_resources = 0;
  conn_param.initiator_depth = 0;
  conn_param.retry_count = 3; // TODO
  conn_param.rnr_retry_count = 3; // TODO 

VerbsEP *ep = client->connectEP(&attr,&conn_param);
	printf("Connection is established\n");
  

  printf("Connection is established %p\n",ep);

  std::this_thread::sleep_for(std::chrono::seconds(500));

 return 0;
}

