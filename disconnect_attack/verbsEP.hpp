#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <infiniband/verbs.h>
#include <vector>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

 
class VerbsEP{

public:
 
  struct rdma_cm_id * id;
  struct ibv_qp * const qp;
  struct ibv_pd * const pd;
  const uint32_t max_inline_data;
 
  const uint32_t max_send_size;
  const uint32_t max_recv_size;

  uint32_t can_post = 0;
  const uint32_t recv_batch = 32;

  std::vector<struct ibv_recv_wr> recv_wr;

  VerbsEP(struct rdma_cm_id* id,struct ibv_qp *qp, uint32_t max_inline_data,uint32_t max_send_size,uint32_t max_recv_size): 
          id(id),qp(qp), pd(qp->pd), max_inline_data(0),max_send_size(max_send_size),max_recv_size(max_recv_size)
  {
      
  recv_wr.resize(recv_batch);


    struct ibv_recv_wr* wrs = recv_wr.data();

    for(uint32_t i=0; i < recv_batch; i++){
      wrs[i].num_sge = 0;
      wrs[i].sg_list = 0;
      wrs[i].wr_id = 0;
      wrs[i].next = &wrs[i+1];
    }
    wrs[recv_batch-1].next = NULL;



  }
  enum rdma_cm_event_type get_event(){
      int ret;
      struct rdma_cm_event *event;
      
      ret = rdma_get_cm_event(id->channel, &event);
      if (ret) {
          perror("rdma_get_cm_event");
          exit(ret);
      }
      enum rdma_cm_event_type out = event->event;
  printf("got event %d \n",event->event);
     /* switch (event->event){
          case RDMA_CM_EVENT_ADDR_ERROR:

          case RDMA_CM_EVENT_ROUTE_ERROR:
          case RDMA_CM_EVENT_CONNECT_ERROR:
          case RDMA_CM_EVENT_UNREACHABLE:
          case RDMA_CM_EVENT_REJECTED:
   
               text(log_fp,"[rdma_get_cm_event] Error %u \n",event->event);
              break;

          case RDMA_CM_EVENT_DISCONNECTED:
              text(log_fp,"[rdma_get_cm_event] Disconnect %u \n",event->event);
              break;

          case RDMA_CM_EVENT_DEVICE_REMOVAL:
              text(log_fp,"[rdma_get_cm_event] Removal %u \n",event->event);
              break;
          default:
              text(log_fp,"[rdma_get_cm_event] %u \n",event->event);

      }*/
      rdma_ack_cm_event(event);
      return out;
  }  
  
  void set_mem(struct ibv_sge *sge){
struct ibv_recv_wr* wrs = recv_wr.data();
    for(uint32_t i=0; i < recv_batch; i++){
      wrs[i].num_sge = 1;
      wrs[i].sg_list = sge;
    }
  }

  ~VerbsEP(){
    // empty
  }

  inline void post_empty_recvs(uint32_t post){
      struct ibv_recv_wr *bad;
      can_post+=post;
      while(can_post >= recv_batch){
          can_post-=recv_batch;
          ibv_post_recv(qp, recv_wr.data(), &bad);
      }
  }

  uint32_t get_qp_num() const{
    return qp->qp_num;
  }
 
 
  inline int poll_send_completion(struct ibv_wc* wc, int num = 1){
      return ibv_poll_cq(this->qp->send_cq, num, wc);
  }

  inline int poll_recv_completion(struct ibv_wc* wc, int num = 1){
      return ibv_poll_cq(this->qp->recv_cq, num, wc);
  }

   

  inline int post_empty_recv(uint64_t wr_id){


    struct ibv_recv_wr wr, *bad;

    wr.wr_id = wr_id;
    wr.next = NULL;
    wr.sg_list = NULL;
    wr.num_sge = 0;

    return ibv_post_recv(qp, &wr, &bad);
  }
  
  inline int post_recv(uint64_t wr_id, struct ibv_mr * mr){
      return post_recv(wr_id, (uint64_t)mr->addr, mr->lkey,  mr->length);
  }

  inline int post_recv(uint64_t wr_id, uint64_t local_addr=0ULL, uint32_t lkey=0, uint32_t length=0){
    struct ibv_sge sge;

    sge.addr = local_addr;
    sge.length = length;
    sge.lkey = lkey;

    struct ibv_recv_wr wr, *bad;

    wr.wr_id = wr_id;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    return ibv_post_recv(qp, &wr, &bad);
  }


  inline int send_signaled(uint64_t wr_id, uint64_t local_addr, uint32_t lkey, uint32_t length){
    unsigned int send_flags = IBV_SEND_SIGNALED;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }

    return two_sided( IBV_WR_SEND, send_flags, wr_id, 0,local_addr, lkey, length);
  }

  inline int send(uint64_t wr_id, uint64_t local_addr, uint32_t lkey, uint32_t length){
    unsigned int send_flags = 0;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }
    return two_sided( IBV_WR_SEND, send_flags, wr_id, 0,local_addr, lkey, length);
  }


  inline int send_with_imm_signaled(uint64_t wr_id, uint32_t imm_data, uint64_t local_addr, uint32_t lkey, uint32_t length){
    unsigned int send_flags = IBV_SEND_SIGNALED;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }

    return two_sided( IBV_WR_SEND_WITH_IMM, send_flags, wr_id, imm_data,local_addr, lkey, length);
  }

  inline int send_with_imm(uint64_t wr_id, uint32_t imm_data, uint64_t local_addr, uint32_t lkey, uint32_t length){
    unsigned int send_flags = 0;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }
    return two_sided( IBV_WR_SEND_WITH_IMM, send_flags, wr_id, imm_data,local_addr, lkey, length);
  }

  inline int write_signaled(uint64_t wr_id, uint64_t local_addr, uint32_t lkey, uint64_t remote_addr, uint32_t rkey, uint32_t length){

    unsigned int send_flags = IBV_SEND_SIGNALED;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }
    return one_sided(IBV_WR_RDMA_WRITE,send_flags,wr_id,0,local_addr,lkey,remote_addr,rkey,length);
  }


  inline int write(uint64_t wr_id, uint64_t local_addr, uint32_t lkey, uint64_t remote_addr, uint32_t rkey, uint32_t length){

    unsigned int send_flags = 0;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }
    return one_sided(IBV_WR_RDMA_WRITE,send_flags,wr_id,0,local_addr,lkey,remote_addr,rkey,length);
  }

  

  inline int write_with_imm_signaled(uint64_t wr_id, uint32_t imm_data, 
      uint64_t local_addr, uint32_t lkey, uint64_t remote_addr, uint32_t rkey, uint32_t length){

    unsigned int send_flags = IBV_SEND_SIGNALED;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }
    return one_sided(IBV_WR_RDMA_WRITE_WITH_IMM,send_flags,wr_id,imm_data,local_addr,lkey,remote_addr,rkey,length);
  }


  inline int write_with_imm(uint64_t wr_id, uint32_t imm_data, 
      uint64_t local_addr, uint32_t lkey, uint64_t remote_addr, uint32_t rkey, uint32_t length){

    unsigned int send_flags = 0;

    if(length!=0 && length<=max_inline_data){
        send_flags |= IBV_SEND_INLINE;
    }
    return one_sided(IBV_WR_RDMA_WRITE_WITH_IMM,send_flags,wr_id,imm_data,local_addr,lkey,remote_addr,rkey,length);
  }


  inline int read_signaled(uint64_t wr_id, uint64_t local_addr, uint32_t lkey, uint64_t remote_addr, 
                           uint32_t rkey, uint32_t length)
  {
    unsigned int send_flags = IBV_SEND_SIGNALED;
 
    return one_sided(IBV_WR_RDMA_READ,send_flags,wr_id,0,local_addr,lkey,remote_addr,rkey,length);
  }

  inline int read(uint64_t wr_id, uint64_t local_addr, uint32_t lkey, uint64_t remote_addr, uint32_t rkey, uint32_t length)
  {
    unsigned int send_flags = 0;

    return one_sided(IBV_WR_RDMA_READ,send_flags,wr_id,0,local_addr,lkey,remote_addr,rkey,length);
  }


  inline int one_sided(enum ibv_wr_opcode opcode, unsigned int send_flags, uint64_t wr_id, uint32_t imm_data, 
    uint64_t local_addr, uint32_t lkey, uint64_t remote_addr, uint32_t rkey, uint32_t length)
  {
      struct ibv_sge sge;

      sge.addr = local_addr;
      sge.length = length;
      sge.lkey = lkey;
      struct ibv_send_wr wr, *bad;

      wr.wr_id = wr_id;
      wr.next = NULL;
      wr.sg_list = &sge;
      wr.num_sge = 1;
      wr.opcode = opcode;

      wr.send_flags = send_flags;   
      wr.imm_data = imm_data;


      wr.wr.rdma.remote_addr = remote_addr;
      wr.wr.rdma.rkey        = rkey;

      return ibv_post_send(this->qp, &wr, &bad);    
  }


  inline int two_sided(enum ibv_wr_opcode opcode, unsigned int send_flags, uint64_t wr_id, uint32_t imm_data, 
    uint64_t local_addr, uint32_t lkey, uint32_t length)
  {
      struct ibv_sge sge;

      sge.addr = local_addr;
      sge.length = length;
      sge.lkey = lkey ;
      struct ibv_send_wr wr, *bad;

      wr.wr_id = wr_id;
      wr.next = NULL;
      wr.sg_list = &sge;
      wr.num_sge = 1;
      wr.opcode = opcode;

      wr.send_flags = send_flags;  
      wr.imm_data = imm_data; 

      return ibv_post_send(this->qp, &wr, &bad);
  }
 
};
