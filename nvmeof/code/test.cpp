#include "spdk/nvme.h"
#include "spdk/env.h"
#include <iostream>       // std::cout
#include <chrono>
#include "cxxopts.hpp"
#include <sys/types.h>

struct ns_entry {
	char				name[1024];
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_nvme_ns	*ns;
	struct spdk_nvme_qpair	*qpair;
};

struct ns_entry main_ns;

int pending = 0;
 

static void
op_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
 
	if (spdk_nvme_cpl_is_error(completion)) {
		spdk_nvme_qpair_print_completion(main_ns.qpair, (struct spdk_nvme_cpl *)completion);
		fprintf(stderr, "I/O error status: %s\n", spdk_nvme_cpl_get_status_string(&completion->status));
		fprintf(stderr, "I/O failed, aborting run\n");
		exit(1);
	}
	pending = 0;
}


 
static void
attach_cb(  struct spdk_nvme_ctrlr *ctrlr )
{
	
	const struct spdk_nvme_ctrlr_data *cdata = spdk_nvme_ctrlr_get_data(ctrlr);
	union spdk_nvme_cap_register cap = spdk_nvme_ctrlr_get_regs_cap(ctrlr);
	printf("Max write size %" PRIu64 "\n", (uint64_t)1 << (12 + cap.bits.mpsmin + cdata->mdts));

	snprintf(main_ns.name, sizeof(main_ns.name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);
 
 
	int num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	printf("Using controller %s with %d namespaces.\n", main_ns.name, num_ns);
        struct spdk_nvme_io_qpair_opts qpopts;
        memset(&qpopts,0,sizeof(qpopts));
	spdk_nvme_ctrlr_get_default_io_qpair_opts(ctrlr,&qpopts,sizeof(qpopts));
	printf(" will create: IO: %d, IO size %u, Requests %u, delay io %d\n",
		qpopts.qprio, qpopts.io_queue_size, qpopts.io_queue_requests, qpopts.delay_pcie_doorbell );
	      
	for (int nsid = 1; nsid <= num_ns; nsid++) {
//                printf("try nsid %d\n",nsid);
		struct spdk_nvme_ns *ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (ns == NULL) {
			continue;
		}

		if (!spdk_nvme_ns_is_active(ns)) {
			continue; //return;
		}
	 
		main_ns.ns = ns;
		printf("  Namespace ID: %d size: %juGB. block size: %u\n", spdk_nvme_ns_get_id(ns),
	        spdk_nvme_ns_get_size(ns) / 1000000000 , spdk_nvme_ns_get_sector_size(ns));
		
		main_ns.qpair = spdk_nvme_ctrlr_alloc_io_qpair(main_ns.ctrlr, NULL, 0);
		if (main_ns.qpair == NULL) {
			printf("ERROR: spdk_nvme_ctrlr_alloc_io_qpair() failed\n");
			return;
		}

		break;
	}
 
}

 
// ParseResult class: used for the parsing of line arguments
cxxopts::ParseResult
parse(int argc, char* argv[])
{
    cxxopts::Options options(argv[0], "nvme test");
    options
      .positional_help("[optional args]")
      .show_positional_help();

  try
  {
 
    options.add_options()
      ("r,nvme", "nvme device", cxxopts::value<std::string>(), "DEVNAME")
      ("help", "Print help")
     ;
 
    auto result = options.parse(argc, argv);

    if (result.count("help") || !result.count("nvme") )
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


//static char g_hostnqn[SPDK_NVMF_NQN_MAX_LEN + 1]; // is needed for host NQN

int main(int argc, char **argv)
{
	int rc;
	auto allparams = parse(argc,argv);


	struct spdk_env_opts opts;
	spdk_env_opts_init(&opts);
	opts.name = "hello_world";
	opts.shm_id = 0;
	if (spdk_env_init(&opts) < 0) {
		fprintf(stderr, "Unable to initialize SPDK env\n");
		return 1;
	}
	struct spdk_nvme_transport_id trid;
	const char *nvme_devname = allparams["nvme"].as<std::string>().c_str();
	
	rc = spdk_nvme_transport_id_parse (&trid, nvme_devname);
	
	printf("Parsed NVME type is %d\n",trid.trtype);	
	printf("Attaching to %s\n", trid.traddr);
	struct spdk_nvme_ctrlr_opts copts;

	spdk_nvme_ctrlr_get_default_ctrlr_opts(&copts, sizeof(copts));
	copts.keep_alive_timeout_ms = 5*64*1000; // 5 min delay
	printf("Note that in 5 min. the device will be disconnected, if you do not use it.\n");
  
  	main_ns.ctrlr = spdk_nvme_connect(&trid, &copts, sizeof(copts));
	if (!main_ns.ctrlr) {
		fprintf(stderr, "spdk_nvme_connect() failed\n");
		return 1;
	}
	attach_cb(main_ns.ctrlr);

	printf("Initialization complete. %p %p\n", main_ns.ns, main_ns.qpair);
	if(main_ns.ns == NULL || main_ns.qpair==NULL) {
		printf("failed to connect\n");
		exit(1);
	}

	 
	char* sendbuf = (char*)spdk_dma_zmalloc(16384, 0x1000, NULL); //16 KiB wiith 0x1000 alignment
	char* recvbuf = sendbuf + 4096; 
	

	if (sendbuf == NULL) {
		printf("ERROR: write buffer allocation failed\n");
		exit(1);
	}

	printf("INFO: host memory buffer for IO at addr %p\n",sendbuf);
	printf("INFO: receive buffer will start at addr %p\n",recvbuf);

 	memset(sendbuf,0,16384);
 	
 	while(true){
		uint64_t val = 0;
 		char op;
 		std::cout << "(q to exit, t to test) Please enter  op: r or w \n";
 		std::cin >> op;

 		if(op=='q') break;

 		if(op=='t'){
			val = *(volatile uint64_t*)sendbuf;
			if(val){
				printf("Send buffer has been modified!\n");
			}
			val = *(volatile uint64_t*)recvbuf;
			if(val){
				printf("Receive buffer has been modified!\n");
			}
 			continue;
 		}
 		
 		pending = 1;
 	 	if(op=='w'){
			rc = spdk_nvme_ns_cmd_write(main_ns.ns, main_ns.qpair, sendbuf,
					    0, /* LBA start */
					    1, /* number of LBAs */
					    op_complete, NULL, 0);
		} else {
			rc = spdk_nvme_ns_cmd_read(main_ns.ns, main_ns.qpair, recvbuf,
					    0, /* LBA start */
					    1, /* number of LBAs */
					    op_complete, NULL, 0);
		}
		
		if (rc != 0) {
			fprintf(stderr, "starting write I/O failed\n");
			exit(1);
		}
                printf("Request is issued\n");

		while (pending) {
			spdk_nvme_qpair_process_completions(main_ns.qpair, 0);
		}
		spdk_nvme_ctrlr_process_admin_completions(main_ns.ctrlr);
		printf("Finised op\n");
 
 	}
 	printf("test is done\n");
	spdk_nvme_ctrlr_free_io_qpair(main_ns.qpair);
	spdk_nvme_detach(main_ns.ctrlr);
	spdk_dma_free(sendbuf);
 
	return 0;
}
