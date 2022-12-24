#include "verbsEP.hpp"
#include "connectRDMA.hpp"
#include "cxxopts.hpp"
#include <vector>
#include <sys/mman.h>
 
#include <thread>         // std::this_thread::sleep_for
#include <chrono>         // std::chrono::se

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


void print_data(VerbsEP *ep){

  struct ibv_qp_attr attr;
  struct ibv_qp_init_attr init_attr;
  int ret = ibv_query_qp(ep->qp, &attr, IBV_QP_STATE| IBV_QP_RQ_PSN | IBV_QP_SQ_PSN, &init_attr );
  if(ret == 0){
   if(attr.qp_state != 3){
     printf("Connection has been broken\n");
     return;
   } else {

   printf("still connected \n");
  }
  }
}


int main(int argc, char* argv[]){
	auto allparams = parse(argc,argv);

	std::string ip = allparams["address"].as<std::string>(); // "192.168.1.20"; .c_str()
 
	int port = 9999;
 
	ServerRDMA * server = new ServerRDMA(const_cast<char*>(ip.c_str()),port);
	struct ibv_qp_init_attr attr;
 	struct rdma_conn_param conn_param;
 
 
	memset(&attr, 0, sizeof(attr));
  attr.cap.max_send_wr = 1;
  attr.cap.max_recv_wr = 1;
  attr.cap.max_send_sge = 1;
  attr.cap.max_recv_sge = 1;
  attr.cap.max_inline_data = 0;
  attr.qp_type = IBV_QPT_RC;

  memset(&conn_param, 0 , sizeof(conn_param));
  conn_param.responder_resources = 0;
  conn_param.initiator_depth = 0;
  conn_param.retry_count = 3; // TODO
  conn_param.rnr_retry_count = 3; // TODO 


	struct ibv_pd *pd = server->create_pd();


VerbsEP *ep = server->acceptEP(&attr,&conn_param,pd);

uint32_t  ep_num = ep->get_qp_num();


  printf("All conntections has been connected\n");
  printf("QPN: %u \n",ep_num);
  print_data(ep);
  ep->get_event();
 print_data(ep);


  return 0;
}

