#include <iostream>       // std::cout
#include <infiniband/verbs.h>

#include "cxxopts.hpp"

const uint32_t UD_HEADER = 40;

// ParseResult class: used for the parsing of line arguments
cxxopts::ParseResult
parse(int argc, char* argv[])
{
    cxxopts::Options options(argv[0], "Sender of UD test");
    options
      .positional_help("[optional args]")
      .show_positional_help();

  try
  {
 
    options.add_options()
      ("i,ib-port", "IB port", cxxopts::value<uint8_t>()->default_value("1"), "N")
      ("ib-dev", "device", cxxopts::value<std::string>(), "DEVNAME")
      ("l,dlid", "destination lid", cxxopts::value<uint32_t>()->default_value("3"), "N")
      ("gid1", "First part of the dest. GID", cxxopts::value<uint64_t>(), "N")
      ("gid2", "Second part of the dest. GID", cxxopts::value<uint64_t>(), "N")
      ("g,gid-idx", "local port gid index for sending. note that 0-Roce1-IP6, 1-Roce2-IP6, 2-Roce1-IP4, 3-Roce2-IP4", cxxopts::value<uint32_t>()->default_value("1"), "N")
      ("help", "Print help")
     ;
 
    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
      std::cout << options.help({""}) << std::endl;
      exit(0);
    }
 
    return result;

  }
  catch (const cxxopts::OptionException& e)
  {
    std::cout << "error parsing options: " << e.what() << std::endl;
    std::cout << options.help({""}) << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]){
// parse the arguments and creates a dictionary which maps arguments to values
    auto allparams = parse(argc,argv);


    uint32_t destqpn = 0x208; 
     
// get the list of IBV devices
// https://linux.die.net/man/3/ibv_get_device_list
// returns a NULL-terminated array of RDMA devices currently available.
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
      perror("Failed to get IB devices list");
      return 1;
    }

    struct ibv_device *ib_dev = NULL;
    uint8_t ibport = allparams["ib-port"].as<uint8_t>(); // get the port number
 
// look if we find the device
    if(allparams.count("ib-dev")){
      const char *ib_devname = allparams["ib-dev"].as<std::string>().c_str();
      for (int i = 0; dev_list[i]; ++i){
        if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname)){
          ib_dev = dev_list[i];
        }
      }
      
      if (!ib_dev) {
        fprintf(stderr, "IB device %s not found\n", ib_devname);
        return 1;
      }
    } else {
      ib_dev = dev_list[0];
      if (!ib_dev) {
        fprintf(stderr, "No IB devices found\n");
        return 1;
      }
    }

    // it is the context we use almost for everyting 
// Create infiniband context
// https://linux.die.net/man/3/ibv_open_device
// struct ibv_context *ibv_open_device(struct ibv_device *device);
//     opens the device (device) and creates a context for further use. 
    struct ibv_context  *context = ibv_open_device(ib_dev);
    if (!context) {
      fprintf(stderr, "Couldn't get context for %s\n", ibv_get_device_name(ib_dev));
      return 1;
    }


    struct ibv_pd *pd = ibv_alloc_pd(context);
    if (!pd) {
      fprintf(stderr, "Couldn't allocate PD\n");
      return 1;
    }

    // now we can create the connection.
    struct ibv_qp   *qp = NULL;
    { 

     struct ibv_cq   *cq = ibv_create_cq(context, 16, NULL, NULL, 0); // 16 is the size of the queue. It is a hard bound. Overflows must be avoided.
      if (!cq) {
        fprintf(stderr, "Couldn't create CQ\n");
        return 1;
      }


      struct ibv_qp_init_attr init_attr;
      memset(&init_attr,0,sizeof init_attr);
      init_attr.send_cq = cq;  // use a single CQ for send and recv
      init_attr.recv_cq = cq;  // see above 
      init_attr.cap.max_send_wr  = 8; // the maximum number of send requests can be submitted to the QP at the same time. Polling CQ helps to remove requests from the device.
      init_attr.cap.max_recv_wr  = 0; // the maximum number of receives we can post to the QP. I use 0 to show that the connection will not receive anything.  
      init_attr.cap.max_send_sge = 1; // is not interesting for you. Must be 1 for you.
      init_attr.cap.max_recv_sge = 1; // is not interesting for you. Must be 1 for you.
      init_attr.cap.max_inline_data = 64;
      init_attr.qp_type = IBV_QPT_UD;
      
      struct ibv_qp_init_attr_ex init_attr_ex = {};

			init_attr_ex.send_cq = cq;
			init_attr_ex.recv_cq = cq;
			init_attr_ex.cap.max_send_wr = 8;
			init_attr_ex.cap.max_recv_wr = 0;
			init_attr_ex.cap.max_send_sge = 1;
			init_attr_ex.cap.max_recv_sge = 1;
			init_attr_ex.cap.max_inline_data = 64;
			init_attr_ex.qp_type = IBV_QPT_UD;
			init_attr_ex.create_flags =  IBV_QP_CREATE_SOURCE_QPN;

			init_attr_ex.comp_mask |= IBV_QP_INIT_ATTR_PD | IBV_QP_INIT_ATTR_CREATE_FLAGS;
			init_attr_ex.pd = pd;
			init_attr_ex.source_qpn = 9999; 


     // qp = ibv_create_qp(pd, &init_attr); // NOte that we use PD here! it means that QP will belong to this PD.
      qp = ibv_create_qp_ex(context, &init_attr_ex); // NOte that we use PD here! it means that QP will belong to this PD.
      if (!qp)  {
        fprintf(stderr, "Couldn't create QP\n");
        return 1;
      }
    }
    printf("We created a raw QP connection. We cannot use it yet, but we know its QPN: %u\n",qp->qp_num);

    {
      // move QP to INIT state
      struct ibv_qp_attr attr;
      memset(&attr,0,sizeof attr);
      attr.qp_state        = IBV_QPS_INIT;
      attr.pkey_index      = 0; // for you always 0
      attr.port_num        = ibport;
      attr.qkey            = 0xb1b; // can be any constant
// https://linux.die.net/man/3/ibv_modify_qp
// modifies the attributes of a QP
      if (ibv_modify_qp(qp, &attr,
            IBV_QP_STATE              |
            IBV_QP_PKEY_INDEX         |
            IBV_QP_PORT               |
            IBV_QP_QKEY)) {
        fprintf(stderr, "Failed to modify QP to INIT\n");
        return 1;
      }
    }

    printf("QP is init state. We cannot use it yet. We will allow it to receive data on the next step. \n");

// move QP to RTR state
    {
      struct ibv_qp_attr attr;
      memset(&attr,0,sizeof attr);
      attr.qp_state   = IBV_QPS_RTR;      

      if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
        fprintf(stderr, "Failed to modify QP to RTR\n");
        return 1;
      }

    }
    uint32_t my_psn = 0; // first message will have PSN (packet serial number) equal to 0; It will help to debug. Note PSN is 24 bits
    printf("QP can receive, but cannot send. \n");

// move QP to RTS state
    {
      struct ibv_qp_attr attr;
      memset(&attr,0,sizeof attr);
      attr.qp_state     = IBV_QPS_RTS;
      attr.sq_psn     = my_psn;

      if (ibv_modify_qp(qp, &attr,
            IBV_QP_STATE              |
            IBV_QP_SQ_PSN)) {
        fprintf(stderr, "Failed to modify QP to RTS\n");
        return 1;
      }
    }

    printf("Cool! QP can SEND! Let's find out where we can send. NOte that we required zero info so far.\n");

    struct ibv_ah *ah = NULL; // it is the address handler we need to send data. It will contain info about the path to destination.



    {

      struct ibv_ah_attr ah_attr;
      memset(&ah_attr,0,sizeof ah_attr);
      ah_attr.is_global = 0; // must be 1 for roce
      ah_attr.dlid = allparams["dlid"].as<uint32_t>();      // must be 0 for roce
      ah_attr.sl  = 0; // it is a service level. I am not sure what it gives for us, but can be 0.
      ah_attr.src_path_bits = 0; // I do not know. what is it. must be zero
      ah_attr.port_num      = ibport; 
      ah_attr.grh.hop_limit = 1; 

/*
// get as input the GID of the receiver
      union ibv_gid   dgid; // address of the destination.

      if(allparams.count("gid1")){
        dgid.global.subnet_prefix = allparams["gid1"].as<uint64_t>();
      } else {
        std::cout << "I need the first part of GID: \n";
        std::cin >> dgid.global.subnet_prefix;
      }

      if(allparams.count("gid2")){
        dgid.global.interface_id = allparams["gid2"].as<uint64_t>();
      } else {
        std::cout << "I need the second part of GID: \n";
        std::cin >> dgid.global.interface_id;
      }

      // address of the remote side
      ah_attr.grh.dgid = dgid;
      ah_attr.grh.sgid_index = allparams["gid-idx"].as<uint32_t>(); //  it sets ip and roce versions

// https://linux.die.net/man/3/ibv_create_ah
// struct ibv_ah *ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
//    create an address handle (AH)
//       pd: protection domain
//       attr: AH attributes
//    eturns a pointer to the created AH
*/
      ah = ibv_create_ah(pd, &ah_attr);
      if (!ah) {
        fprintf(stderr, "Failed to create AH\n");
        return 1;
      }
  };

 	
 
static const unsigned char packet[40] = {
	0x08, 0x00, 0x00, 0x00, /* ........ */
0x45, 0x00, 0x00, 0x24, 0x3e, 0x61, 0x40, 0x00, /* E..$>a@. */
0x40, 0x11, 0x78, 0xf9, 0xc0, 0xa8, 0x01, 0x0a, /* @.x..... */
0xc0, 0xa8, 0x01, 0x14, 0x82, 0x04, 0x48, 0x53, /* ......HS */
0x00, 0x10, 0x7a, 0x03, 0x66, 0x61, 0x6b, 0x65, /* ..z.fake */
0x31, 0x32, 0x33, 0x00
};

 
 
  // let's submit the request
// define the characteristics of the Work Request that we will now submit
  struct ibv_sge sge;
  sge.addr = (uintptr_t)packet; // The address of the buffer to write to (or read from)
  sge.length = sizeof(packet); // The length of the buffer in bytes, i.e. size of the data to send (or to receive)
  sge.lkey = 0; // The Local key of the Memory Region that this memory buffer was registered with
// describes the Work Request to the SEND Queue of the QP (I define an object WR of type ibv_send_wr)
  struct ibv_send_wr wr;
  wr.wr_id      = 666; // 64 bits which will be in the completion event. A 64 bits value associated with this WR. If a Work Completion will be generated when this Work Request ends, it will contain this value
  wr.sg_list    = &sge; // Scatter/Gather array. It specifies the buffers where data will be written in
  wr.num_sge    = 1;  // Size of the sg_list array. This number can be less or equal to the number of scatter/gather entries that the Queue Pair was created to support in the Receive Queue
  wr.next       = NULL; // we can batch requests. So it can point to the next send request. Pointer to the next WR in the linked list.
  wr.opcode     = IBV_WR_SEND; // The operation that this WR will perform. This value controls the way that data will be sent, the direction of the data flow and the used attributes in the WR
  wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE; // it means we want to receive an event (Work Completion will be generated when the processing of this WR will be ended). Describes the properties of the WR

  wr.wr.ud.ah   = ah; // path to destination. Address handle (AH) that describes how to send the packet. This AH must be valid until any posted Work Requests that uses it isn't considered outstanding anymore. Relevant only for UD QP 
  wr.wr.ud.remote_qpn  = destqpn; // connection ID of the dest. QP number of the destination QP.
  wr.wr.ud.remote_qkey = 0xb1b; // sanity check
 
 
  struct ibv_send_wr *bad_wr;
  int ret = ibv_post_send(qp, &wr, &bad_wr);
  if (ret) {
    fprintf(stderr, "Couldn't post request\n");
    return 1;
  }
 
  struct ibv_wc wc;
  int ne = 0; // the number of fetched events
  do{
    ne = ibv_poll_cq(qp->send_cq, 1, &wc); // 1 is the length of the array &wc
  } while(ne == 0); 

  printf("Cool! injected. status: wc.status %u; wc.wr_id %lu \n",wc.status, wc.wr_id);

}
 
