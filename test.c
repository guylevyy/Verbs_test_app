#include <vl.h>
#include <ctype.h>
#include "types.h"
#include "get_clock.h"
#include <assert.h>
#include <infiniband/mlx5dv.h>

extern struct config_t config;

int force_configurations_dependencies()
{
	if(config.ring_depth < config.batch_size)
		config.ring_depth = config.batch_size;

	if (config.qp_type == IBV_QPT_UD && config.is_daemon)
		config.msg_sz += GRH_SIZE;

	if (config.qp_type == IBV_QPT_RAW_PACKET &&
	    config.msg_sz < 64) {
		VL_MISC_ERR(("Raw packet transport requires minimum 64B of message size\n"));
		return FAIL;
	}

	if(config.msg_sz % config.num_sge) {
		VL_MISC_ERR(("Test support a msg size which is a multiplication of SGEs number\n"));
		return FAIL;
	}

	/* Send new API is relevant just for sender */
	if ((config.send_method == METHOD_MIX || config.send_method == METHOD_NEW) &&
	    config.is_daemon) {
		VL_MISC_ERR(("New or MIX method is allowed just on client\n"));
		return FAIL;
	}

	/* Driver doesnt support DC in legacy send API */
	if ((config.send_method == METHOD_OLD || config.send_method == METHOD_MIX) &&
	    config.qp_type == IBV_QPT_DRIVER && !config.is_daemon) {
		VL_MISC_ERR(("OLD and MIX methods don't support DC\n"));
		return FAIL;
	}

	if (config.qp_type == IBV_QPT_RAW_PACKET &&
	    strlen(config.mac) != STR_MAC_LEN - 1) {
		VL_MISC_ERR(("Invalid local MAC address %d\n", strlen(config.mac)));
		return FAIL;
	}

	if ((config.opcode == IBV_WR_SEND_WITH_INV ||
	     config.opcode == IBV_WR_LOCAL_INV ||
	     config.opcode == IBV_WR_BIND_MW) &&
	     (config.batch_size > 1 || config.num_of_iter > 1)) {
		VL_MISC_ERR(("Test supports only batch=1 & iter=1 for MW opertions\n"));
		return FAIL;
	}

	if (config.msg_sz != 8 && !config.ext_atomic &&
	    (config.opcode == IBV_WR_ATOMIC_FETCH_AND_ADD ||
	     config.opcode == IBV_WR_ATOMIC_CMP_AND_SWP)) {
		VL_MISC_ERR(("Standard atomic operations require minimum 8B buffer\n"));
		return FAIL;
	}

	/* Spec/PRM restriction */
	if (config.num_sge > 1 &&
	    (config.opcode == IBV_WR_ATOMIC_FETCH_AND_ADD ||
	     config.opcode == IBV_WR_ATOMIC_CMP_AND_SWP)) {
		VL_MISC_ERR(("Atomic operation cant set with multiple SGEs\n"));
		return FAIL;
	}

	/* spec restriction */
	if (config.use_inl &&
	    config.opcode != IBV_WR_SEND &&
	    config.opcode != IBV_WR_SEND_WITH_IMM &&
	    config.opcode != IBV_WR_RDMA_WRITE &&
	    config.opcode != IBV_WR_RDMA_WRITE_WITH_IMM) {
		VL_MISC_ERR(("Inline isn't supported by that operation\n"));
		return FAIL;
	}

	/* spec restriction */
	if ((config.opcode == IBV_WR_RDMA_WRITE ||
	     config.opcode == IBV_WR_RDMA_WRITE_WITH_IMM ||
	     config.opcode == IBV_WR_RDMA_READ ||
	     config.opcode == IBV_WR_LOCAL_INV ||
	     config.opcode == IBV_WR_BIND_MW ||
	     config.opcode == IBV_WR_SEND_WITH_INV ||
	     config.opcode == IBV_WR_ATOMIC_FETCH_AND_ADD ||
	     config.opcode == IBV_WR_ATOMIC_CMP_AND_SWP) &&
	    (config.qp_type != IBV_QPT_DRIVER &&
	     config.qp_type != IBV_QPT_RC &&
	     config.qp_type != IBV_QPT_XRC_SEND &&
	     config.qp_type != IBV_QPT_XRC_RECV)) {
		VL_MISC_ERR(("The operation is unsupported on that transport\n"));
		return FAIL;
	}

	/* spec restrictions (TSO is just for UD underlay and raw packet)*/
	if (config.qp_type == IBV_QPT_UD &&
	    (config.opcode != IBV_WR_SEND &&
	     config.opcode != IBV_WR_SEND_WITH_IMM)) {
		VL_MISC_ERR(("The operation is unsupported on that transport\n"));
		return FAIL;
	}

	/* spec restrictions (TSO is just for UD underlay and raw packet)*/
	if (config.qp_type == IBV_QPT_RAW_PACKET &&
	    (config.opcode != IBV_WR_SEND &&
	     config.opcode != IBV_WR_SEND_WITH_IMM &&
	     config.opcode != IBV_WR_TSO)) {
		VL_MISC_ERR(("The operation is unsupported on that transport\n"));
		return FAIL;
	}

	/* Need to add support for legacy send API operations */
	if (!config.is_daemon &&
	    (config.send_method == METHOD_OLD || config.send_method == METHOD_MIX) &&
	    config.opcode != IBV_WR_SEND) {
		VL_MISC_ERR(("Test support just send opcode for legacy post\n"));
		return FAIL;
	}

	/* For performance benchmark need to optimize operations */
	if (config.opcode != IBV_WR_SEND)
		VL_MISC_ERR(("WARN: opcode isn't optimized by test!\n"));

	return 0;
}

int send_info(struct resources_t *resource, const void *buf, size_t size)
{
	void *tmp_buf;
	int rc = SUCCESS;
	int i;

	VL_SOCK_TRACE1(("Going to send info."));

	if (size % 4) {
		VL_MISC_ERR(("sync_info must get buffer size of multiples of 4B"));
		return FAIL;
	}

	tmp_buf = calloc(1, size);
	if (!tmp_buf) {
		VL_MEM_ERR((" Fail in alloc tmp_buf"));
		return FAIL;
	}

	for (i = 0; i < (int) (size / sizeof(uint32_t)); i++)
		((uint32_t*) tmp_buf)[i] = htonl((uint32_t) (((uint32_t*) buf)[i]));

	if (VL_sock_send(&resource->sock, size, tmp_buf)) {
		VL_SOCK_ERR(("Fail to send info"));
		rc =  FAIL;
		goto cleanup;
	}

	VL_SOCK_TRACE1((" Info was sent"));

cleanup:
	free(tmp_buf);

	return rc;
}

int recv_info(struct resources_t *resource, void *buf, size_t size)
{
	int i;

	VL_SOCK_TRACE1(("Going to recv info."));

	if (size % 4) {
		VL_MISC_ERR(("sync_info must get buffer size of multiples of 4"));
		return FAIL;
	}

	if (VL_sock_recv(&resource->sock, size, buf)) {
		VL_SOCK_ERR(("Fail to receive info"));
		return FAIL;
	}

	for (i = 0; i < (int) (size / sizeof(uint32_t)); i++)
		((uint32_t*) buf)[i] = ntohl((uint32_t) (((uint32_t*) buf)[i]));

	VL_SOCK_TRACE1((" Info was received"));

	return SUCCESS;
}

//TODO: This need to be optimized for singlr SGE test (save ~15[ns])
static inline void set_sge(struct resources_t *resource, struct ibv_sge *arr)
{
	size_t chunk = config.msg_sz / config.num_sge;
	void *addr = resource->mr->addr;
	int i;

	for (i = 0; i < config.num_sge; i++) {
		arr[i].addr = (uintptr_t)addr;
		arr[i].length = chunk;
		arr[i].lkey = resource->mr->ibv_mr->lkey;

		addr += chunk;
	}
}

static inline void set_data_buf(struct resources_t *resource, struct ibv_data_buf *arr)
{
	size_t chunk = config.msg_sz / config.num_sge;
	void *addr = resource->mr->addr;
	int i;

	for (i = 0; i < config.num_sge; i++) {
		arr[i].addr = addr;
		arr[i].length = chunk;

		addr += chunk;
	}
}

static inline void set_send_wr(struct resources_t *resource,
			       struct ibv_send_wr *wr, uint16_t size)
{
	int i;

	for (i = 0; i < size; i++) {
		int offset = i  * config.num_sge;

		wr[i].wr_id = WR_ID;
		wr[i].send_flags =
			IBV_SEND_SIGNALED |
			(config.use_inl ? IBV_SEND_INLINE : 0); //TODO: move it to pre-processing
		wr[i].opcode = IBV_WR_SEND;
		wr[i].next = &wr[i + 1];
		wr[i].sg_list = &resource->sge_arr[offset];
		wr[i].num_sge = config.num_sge;

		set_sge(resource, &resource->sge_arr[offset]); //TODO: optimize when IBV_SEND_INLINE
	}

	wr[size - 1].next = NULL;
}

static inline void set_recv_wr(struct resources_t *resource,
			       struct ibv_recv_wr *wr, uint16_t size)
{
	int i;

	for (i = 0; i < size; i++) {
		int offset = i * config.num_sge;

		wr[i].wr_id = WR_ID;
		wr[i].next = &wr[i + 1];
		wr[i].sg_list = &resource->sge_arr[offset];
		wr[i].num_sge = config.num_sge;

		set_sge(resource, &resource->sge_arr[offset]);
	}

	wr[size - 1].next = NULL;
}

static inline void fast_set_recv_wr(struct ibv_recv_wr *wr, uint16_t size)
{
	int i;

	wr[size - 1].next = NULL;

	for (i = 0; i < size - 1; i++)
		wr[i].next = &wr[i + 1];
}

static int prepare_receiver(struct resources_t *resource)
{
	int i;

	set_recv_wr(resource, resource->recv_wr_arr, 1);

	for (i = 0; i < (int) config.ring_depth; i++) {
		struct ibv_recv_wr *bad_wr = NULL;
		int rc;

		if (config.qp_type != IBV_QPT_DRIVER && config.qp_type != IBV_QPT_XRC_RECV)
			rc = ibv_post_recv(resource->qp, resource->recv_wr_arr, &bad_wr);
		else
			rc = ibv_post_srq_recv(resource->srq, resource->recv_wr_arr, &bad_wr);
		if (rc) {
			VL_MISC_ERR(("in ibv_post_receive (error: %s)", strerror(rc)));
			return FAIL;
		}
	}

	set_recv_wr(resource, resource->recv_wr_arr, config.batch_size);

	return SUCCESS;
}

static inline int old_post_send(struct resources_t *resource, uint16_t batch_size,
				cycles_t *t1, cycles_t *t2 )
{

	struct ibv_send_wr *bad_wr = NULL;
	int rc;

	*t1 = get_cycles();
	set_send_wr(resource, resource->send_wr_arr, batch_size);

	rc = ibv_post_send(resource->qp, resource->send_wr_arr, &bad_wr);
	*t2 = get_cycles();

	return rc;
}

static inline int _new_post_send(struct resources_t *resource, uint16_t batch_size,
				cycles_t *t1, cycles_t *t2, int inl, int list,
				enum ibv_qp_type qpt, enum ibv_wr_opcode op)
				ALWAYS_INLINE;
static inline int _new_post_send(struct resources_t *resource, uint16_t batch_size,
				cycles_t *t1, cycles_t *t2, int inl, int list,
				enum ibv_qp_type qpt, enum ibv_wr_opcode op)
{
	struct ibv_mw_bind_info bind_info = {
		.mr = resource->mr->ibv_mr,
		.addr = (uint64_t)resource->mr->addr,
		.length = config.msg_sz,
		.mw_access_flags =
			IBV_ACCESS_REMOTE_READ |
			IBV_ACCESS_REMOTE_WRITE
	};
	uint32_t new_rkey;
	int rc;
	int i;

	*t1 = get_cycles();
	ibv_wr_start(resource->eqp);
	for (i = 0; i < batch_size; i++) {
		resource->eqp->wr_id = WR_ID;
		resource->eqp->wr_flags = IBV_SEND_SIGNALED;

		switch (op) {
		case IBV_WR_SEND:
			ibv_wr_send(resource->eqp);
			break;
		case IBV_WR_SEND_WITH_IMM:
			ibv_wr_send_imm(resource->eqp, IMM_VAL);
			break;
		case IBV_WR_RDMA_WRITE:
			ibv_wr_rdma_write(resource->eqp, resource->rkey, resource->raddr);
			break;
		case IBV_WR_RDMA_WRITE_WITH_IMM:
			ibv_wr_rdma_write_imm(resource->eqp, resource->rkey, resource->raddr, IMM_VAL);
			break;
		case IBV_WR_RDMA_READ:
			ibv_wr_rdma_read(resource->eqp, resource->rkey, resource->raddr);
			break;
		case IBV_WR_ATOMIC_FETCH_AND_ADD:
			if (config.ext_atomic)
				mlx5dv_wr_atomic_fetch_add(resource->dv_qp, resource->rkey,
							   resource->raddr, (uint16_t)config.msg_sz,
							   resource->atomic_args, resource->atomic_args + config.msg_sz);
			else
				ibv_wr_atomic_fetch_add(resource->eqp, resource->rkey, resource->raddr, 0xFAFAFAFA);
			break;
		case IBV_WR_ATOMIC_CMP_AND_SWP:
			if (config.ext_atomic)
				mlx5dv_wr_atomic_comp_swap(resource->dv_qp, resource->rkey,
							   resource->raddr, (uint16_t)config.msg_sz,
							   (struct mlx5dv_comp_swap *) resource->atomic_args);
			else
				ibv_wr_atomic_cmp_swp(resource->eqp, resource->rkey, resource->raddr, 0xEEEEEEEE, 0xCCCCCCCC);
			break;
		case IBV_WR_BIND_MW:
		case IBV_WR_LOCAL_INV:
		case IBV_WR_SEND_WITH_INV:
			new_rkey = ibv_inc_rkey(resource->mw->rkey);
			ibv_wr_bind_mw(resource->eqp, resource->mw, new_rkey, &bind_info);
			rc = ibv_wr_complete(resource->eqp);
			if (rc)
				return rc;

			resource->mw->rkey = new_rkey;

			if (config.opcode == IBV_WR_BIND_MW) {
				*t2 = get_cycles();
				return rc;
			} /* End of IBV_WR_BIND_MW flow */

			ibv_wr_start(resource->eqp);

			if (config.opcode == IBV_WR_SEND_WITH_INV) {
				ibv_wr_send_inv(resource->eqp, new_rkey);
				break;
			} else {
				ibv_wr_local_inv(resource->eqp, new_rkey);
				rc = ibv_wr_complete(resource->eqp);
				*t2 = get_cycles();

				return rc;
			}

		default:
			return FAIL;
		}

		if (qpt == IBV_QPT_DRIVER)
			mlx5dv_wr_set_dc_addr(resource->dv_qp, resource->ah, resource->r_dctn ,DC_KEY);
		else if (qpt == IBV_QPT_UD)
			ibv_wr_set_ud_addr(resource->eqp, resource->ah, resource->r_dctn, QKEY);
		else if (qpt == IBV_QPT_XRC_SEND)
			ibv_wr_set_xrc_srqn(resource->eqp, resource->r_dctn);

		if (!inl && !list) {
			ibv_wr_set_sge(resource->eqp,
				       resource->mr->ibv_mr->lkey,
				       (uintptr_t) resource->mr->addr,
				       (uint32_t) config.msg_sz);
		} else if (inl && !list) {
			ibv_wr_set_inline_data(resource->eqp,
					       resource->mr->addr,
					       config.msg_sz);
		} else if (!inl && list){
			int offset = i * config.num_sge;

			set_sge(resource, &resource->sge_arr[offset]);
			ibv_wr_set_sge_list(resource->eqp,
					config.num_sge,
					&resource->sge_arr[offset]);
		} else if (inl && list) {
			int offset = i * config.num_sge;

			set_data_buf(resource, &resource->data_buf_arr[offset]);
			ibv_wr_set_inline_data_list(resource->eqp,
					config.num_sge,
					&resource->data_buf_arr[offset]);
		}
	}
	rc = ibv_wr_complete(resource->eqp);
	*t2 = get_cycles();

	return rc;
}

/* Post RC SEND WR optimized functions */

static int new_post_send_sge_rc(struct resources_t *resource, uint16_t batch_size,
				cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 0, 0, IBV_QPT_RC, IBV_WR_SEND);
}

static int new_post_send_sge_list_rc(struct resources_t *resource, uint16_t batch_size,
				     cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 0, 1, IBV_QPT_RC, IBV_WR_SEND);
}

static int new_post_send_inl_rc(struct resources_t *resource, uint16_t batch_size,
				cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 1, 0, IBV_QPT_RC, IBV_WR_SEND);
}

static int new_post_send_inl_list_rc(struct resources_t *resource,
					     uint16_t batch_size,
					     cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 1, 1, IBV_QPT_RC, IBV_WR_SEND);
}

/* Post DC SEND WR optimized functions */

static int new_post_send_sge_dc(struct resources_t *resource, uint16_t batch_size,
				cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 0, 0, IBV_QPT_DRIVER, IBV_WR_SEND);
}

static int new_post_send_sge_list_dc(struct resources_t *resource, uint16_t batch_size,
				     cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 0, 1, IBV_QPT_DRIVER, IBV_WR_SEND);
}

static int new_post_send_inl_dc(struct resources_t *resource, uint16_t batch_size,
				cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 1, 0, IBV_QPT_DRIVER, IBV_WR_SEND);
}

static int new_post_send_inl_list_dc(struct resources_t *resource,
					     uint16_t batch_size,
					     cycles_t *t1, cycles_t *t2)
{
	return _new_post_send(resource, batch_size, t1, t2, 1, 1, IBV_QPT_DRIVER, IBV_WR_SEND);
}


static int post_send_method_new(struct resources_t *resource, uint16_t batch,
				cycles_t *t1, cycles_t *t2)
{
	switch (config.qp_type) {
	case IBV_QPT_RC:
		if (0);
		else if (config.opcode != IBV_WR_SEND)
			return _new_post_send(resource, batch, t1, t2, config.use_inl,
					      config.num_sge == 1 ? 0 : 1,
					      config.qp_type, config.opcode);
		else if (config.use_inl && config.num_sge == 1)
			return new_post_send_inl_rc(resource, batch, t1, t2);
		else if (config.use_inl && config.num_sge > 1)
			return new_post_send_inl_list_rc(resource, batch, t1, t2);
		else if (!config.use_inl && config.num_sge == 1)
			return new_post_send_sge_rc(resource, batch, t1, t2);
		else if (!config.use_inl && config.num_sge > 1)
			return new_post_send_sge_list_rc(resource, batch, t1, t2);
		else {
			VL_MISC_ERR(("The post send properties are not supported on RC"));
			return FAIL;
		}
	case IBV_QPT_DRIVER:
		if (0);
		else if (config.opcode != IBV_WR_SEND)
			return _new_post_send(resource, batch, t1, t2, config.use_inl,
					      config.num_sge == 1 ? 0 : 1,
					      config.qp_type, config.opcode);
		else if (config.use_inl && config.num_sge == 1)
			return new_post_send_inl_dc(resource, batch, t1, t2);
		else if (config.use_inl && config.num_sge > 1)
			return new_post_send_inl_list_dc(resource, batch, t1, t2);
		else if (!config.use_inl && config.num_sge == 1)
			return new_post_send_sge_dc(resource, batch, t1, t2);
		else if (!config.use_inl && config.num_sge > 1)
			return new_post_send_sge_list_dc(resource, batch, t1, t2);
		else {
			VL_MISC_ERR(("The post send properties are not supported on DC"));
			return FAIL;
		}
	case IBV_QPT_UD:
	case IBV_QPT_RAW_PACKET:
	case IBV_QPT_XRC_SEND:
			return _new_post_send(resource, batch, t1, t2, config.use_inl,
					      config.num_sge == 1 ? 0 : 1,
					      config.qp_type, config.opcode);
	default:
			VL_MISC_ERR(("Unsupported transport"));
			return FAIL;
	}

}

static int post_send_method_old(struct resources_t *resource, uint16_t batch,
				cycles_t *t1, cycles_t *t2)
{
	return old_post_send(resource, batch, t1, t2);
}


static int post_send_method_mix(struct resources_t *resource, uint16_t batch,
				cycles_t *t1, cycles_t *t2)
{
	int rc;

	if (resource->method_state)
		rc = post_send_method_old(resource, batch, t1, t2);
	else
		rc = post_send_method_new(resource, batch, t1, t2);

	resource->method_state = ~resource->method_state;

	return rc;
}

static int post_send_method(struct resources_t *resource, enum send_method method,
			    uint16_t batch, cycles_t *t1, cycles_t *t2)
{
	switch (method) {
	case METHOD_OLD:
		return post_send_method_old(resource, batch, t1, t2);
	case METHOD_NEW:
		return post_send_method_new(resource, batch, t1, t2);
	case METHOD_MIX:
		return post_send_method_mix(resource, batch, t1, t2);
	default:
		VL_MISC_ERR(("Unsupported method"));
		return FAIL;
	}
}

static int do_sender(struct resources_t *resource)
{
	uint32_t tot_ccnt = 0;
	uint32_t tot_scnt = 0;
	int result = SUCCESS;

	while (tot_ccnt < config.num_of_iter) {
		uint16_t outstanding = tot_scnt - tot_ccnt;
		static bool got_bind_wc = 0;
		int rc = 0;

		if ((tot_scnt < config.num_of_iter) && (outstanding < config.ring_depth)) {
			uint32_t left = config.num_of_iter - tot_scnt;
			uint16_t batch;
			cycles_t delta, t1, t2 = 0;

			batch = (config.ring_depth - outstanding) >= config.batch_size ?
				(left >= config.batch_size ? config.batch_size : 1) : 1 ;

			rc = post_send_method(resource, config.send_method, batch, &t1, &t2);
			if (rc) {
				VL_MISC_ERR(("in post send (error: %s)", strerror(rc)));
				result = FAIL;
				goto out;
			}

			delta = t2 - t1;

			if (batch == config.batch_size) {
				if (resource->measure.min > delta)
					resource->measure.min = delta;

				if (resource->measure.max < delta)
					resource->measure.max = delta;

				resource->measure.batch_samples++;
			}

			resource->measure.tot += delta;

			tot_scnt += batch;
		}

		rc = ibv_poll_cq(resource->cq, config.batch_size, resource->wc_arr);

		if (rc > 0) {
			int i;

			for (i = 0; i < rc; i++) {
				if (resource->wc_arr[i].status != IBV_WC_SUCCESS) {
					VL_MISC_ERR(("got WC with error (%d)", resource->wc_arr[i].status));
					result = FAIL;
					goto out;
				}
			}

			tot_ccnt += rc;

			if ((config.opcode == IBV_WR_LOCAL_INV ||
			     config.opcode == IBV_WR_SEND_WITH_INV) &&
			    tot_ccnt == 1 && !got_bind_wc) {
				got_bind_wc = 1;
				tot_ccnt = 0;
			}
		} else if (rc < 0) {
			VL_MISC_ERR(("in ibv_poll_cq (%s)", strerror(rc)));
			result = FAIL;
			goto out;
		}
	}

out:
	VL_DATA_TRACE(("Sender exit with tot_scnt=%u tot_ccnt=%u", tot_scnt, tot_ccnt));

	return result;
}

static int do_receiver(struct resources_t *resource)
{
	uint32_t tot_ccnt = 0;
	uint32_t tot_rcnt = config.ring_depth; //Due to pre-preparation of the RX
	int result = SUCCESS;

	while (tot_ccnt < config.num_of_iter) {
		uint16_t outstanding;
		int rc = 0;

		rc = ibv_poll_cq(resource->cq, config.batch_size, resource->wc_arr);

		if (rc > 0) {
			int i;

			for (i = 0; i < rc; i++)
				if (resource->wc_arr[i].status != IBV_WC_SUCCESS) {
					VL_MISC_ERR(("got WC with error (%d)", resource->wc_arr[i].status));
					result = FAIL;
					goto out;
				}

			tot_ccnt += rc;
		} else if (rc < 0) {
			VL_MISC_ERR(("in ibv_poll_cq (%s)", strerror(rc)));
			result = FAIL;
			goto out;
		}

		outstanding = tot_rcnt - tot_ccnt;

		if ((tot_rcnt < config.num_of_iter) && (outstanding < config.ring_depth)) {
			struct ibv_recv_wr *bad_wr = NULL;
			uint32_t left = config.num_of_iter - tot_rcnt;
			uint16_t batch;

			batch = (config.ring_depth - outstanding) >= config.batch_size ?
				(left >= config.batch_size ? config.batch_size : 1) : 1 ;

			fast_set_recv_wr(resource->recv_wr_arr, batch);

			if (config.qp_type != IBV_QPT_DRIVER && config.qp_type != IBV_QPT_XRC_RECV)
				rc = ibv_post_recv(resource->qp, resource->recv_wr_arr, &bad_wr);
			else
				rc = ibv_post_srq_recv(resource->srq, resource->recv_wr_arr, &bad_wr);
			if (rc) {
				VL_MISC_ERR(("in ibv_post_receive", strerror(rc)));
				result = FAIL;
				goto out;
			}

			tot_rcnt += batch;
		}
	}

out:
	VL_DATA_TRACE(("Receiver exit with tot_rcnt=%u tot_ccnt=%u", tot_rcnt, tot_ccnt));

	return result;
}

int sync_configurations(struct resources_t *resource)
{
	struct sync_conf_info_t remote_info = {0};
	struct sync_conf_info_t local_info = {0};
	int rc;

	local_info.iter = config.num_of_iter;
	local_info.opcode = config.opcode;
	local_info.qp_type = config.qp_type == IBV_QPT_XRC_RECV ?
			     IBV_QPT_XRC_SEND : /* Hack the XRC QPTs sync*/
			     config.qp_type;

	if (!config.is_daemon) {
		rc = send_info(resource, &local_info, sizeof(local_info));
		if (rc)
			return FAIL;

		rc = recv_info(resource, &remote_info, sizeof(remote_info));
		if (rc)
			return FAIL;
	} else {
		rc = recv_info(resource, &remote_info, sizeof(remote_info));
		if (rc)
			return FAIL;

		rc = send_info(resource, &local_info, sizeof(local_info));
		if (rc)
			return FAIL;
	}

	if (config.num_of_iter != remote_info.iter ||
	    config.opcode != remote_info.opcode ||
	    local_info.qp_type != remote_info.qp_type) {
		VL_SOCK_ERR(("Server-client configurations are not synced"));
		return FAIL;
	}

	VL_DATA_TRACE(("Server-client configurations are synced"));

	return  SUCCESS;
}

int sync_post_connection(struct resources_t *resource)
{
	int rc;

	if (!config.is_daemon) {
		struct sync_post_connection_t remote_info = {0};

		rc = recv_info(resource, &remote_info, sizeof(remote_info));
		if (rc)
			return FAIL;

		if (config.qp_type == IBV_QPT_DRIVER || config.qp_type == IBV_QPT_XRC_SEND)
			resource->r_dctn = remote_info.dctn;

		if (config.opcode == IBV_WR_RDMA_WRITE ||
		    config.opcode == IBV_WR_RDMA_WRITE_WITH_IMM ||
		    config.opcode == IBV_WR_RDMA_READ ||
		    config.opcode == IBV_WR_ATOMIC_FETCH_AND_ADD ||
		    config.opcode == IBV_WR_ATOMIC_CMP_AND_SWP) {
			resource->rkey = remote_info.rkey;
			resource->raddr = remote_info.raddr;
		}
	} else {
		struct sync_post_connection_t local_info = {0};

		if (config.qp_type == IBV_QPT_DRIVER)
			local_info.dctn = resource->qp->qp_num;
		else if (config.qp_type == IBV_QPT_XRC_RECV)
			ibv_get_srq_num(resource->srq, &local_info.dctn);

		if (config.opcode == IBV_WR_RDMA_WRITE ||
		    config.opcode == IBV_WR_RDMA_WRITE_WITH_IMM ||
		    config.opcode == IBV_WR_RDMA_READ ||
		    config.opcode == IBV_WR_ATOMIC_FETCH_AND_ADD ||
		    config.opcode == IBV_WR_ATOMIC_CMP_AND_SWP) {
			local_info.rkey = resource->mr->ibv_mr->rkey;
			local_info.raddr = (uintptr_t)resource->mr->addr;
		}

		rc = send_info(resource, &local_info, sizeof(local_info));
		if (rc)
			return FAIL;
	}

	return  SUCCESS;
}

static int init_ah(struct resources_t *resource, uint16_t dlid)
{
	struct ibv_ah_attr ah_attr;

	memset(&ah_attr, 0, sizeof(ah_attr));
	ah_attr.dlid = dlid;
	ah_attr.port_num = IB_PORT;

	VL_HCA_TRACE1(("Going to create AH"));

	resource->ah = ibv_create_ah(resource->pd, &ah_attr);
	if (!resource->ah) {
		VL_DATA_ERR(("Fail in ibv_create_ah"));
		return FAIL;
	}

	VL_DATA_TRACE1(("Finish init AH"));

	return SUCCESS;
}

//convert mac string xx:xx:xx:xx:xx:xx to byte array
#define MAC_SEP ':'
static char *mac_string_to_byte(const char *mac_string, uint8_t *mac_bytes)
{
	int counter;
	for (counter = 0; counter < 6; ++counter) {
		unsigned int number = 0;
		char ch;

		//Convert letter into lower case.
		ch = tolower(*mac_string++);

		if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f')) {
			return NULL;
		}

		number = isdigit (ch) ? (ch - '0') : (ch - 'a' + 10);
		ch = tolower(*mac_string);

		if ((counter < 5 && ch != MAC_SEP) || (counter == 5 && ch != '\0'
				&& !isspace (ch))) {
			++mac_string;

			if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f')) {
				return NULL;
			}

			number <<= 4;
			number += isdigit (ch) ? (ch - '0') : (ch - 'a' + 10);
			ch = *mac_string;

			if (counter < 5 && ch != MAC_SEP) {
				return NULL;
			}
		}
		mac_bytes[counter] = (unsigned char) number;
		++mac_string;
	}
	return (char *) mac_bytes;
}

static int init_mcast_mac_flow(struct resources_t *resource, uint8_t *mmac)
{
	struct raw_eth_flow_attr flow_attr = {
		.attr = {
			.comp_mask      = 0,
			.type           = IBV_FLOW_ATTR_NORMAL,
			.size           = sizeof(flow_attr),
			.priority       = 0,
			.num_of_specs   = 1,
			.port           = IB_PORT,
			.flags          = 0,
		},
		.spec_eth = {
			.type   = IBV_FLOW_SPEC_ETH,
			.size   = sizeof(struct ibv_flow_spec_eth),
			.val = {
				.dst_mac = { mmac[0], mmac[1], mmac[2], mmac[3], mmac[4], mmac[5]},
				.src_mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
				.ether_type = 0,
				.vlan_tag = 0,
			},
			.mask = {
				.dst_mac = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
				.src_mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
				.ether_type = 0,
				.vlan_tag = 0,
			}
		}
	};

	VL_DATA_TRACE1(("Going to create flow rule"));

	resource->flow = ibv_create_flow(resource->qp , &flow_attr.attr);
	if (!resource->flow) {
		VL_DATA_ERR(("Fail to create flow rule (errno %d)", errno));
		return FAIL;
	}

	VL_DATA_TRACE1(("Finish to create flow rule"));

	return SUCCESS;
}

static void init_eth_header(struct resources_t *resource, uint8_t *smac, uint8_t *dmac)
{
	struct ETH_header *eth_header = resource->mr->addr;
	size_t frame_size = config.msg_sz;

	memcpy(eth_header->src_mac, smac, MAC_LEN);
	memcpy(eth_header->dst_mac, dmac, MAC_LEN);
	eth_header->eth_type = htons(frame_size - ETH_HDR_SIZE); /* Payload and CRC */
}

static int qp_to_init(const struct resources_t *resource)
{
	struct ibv_qp_attr attr = {
		.qp_state        = IBV_QPS_INIT,
		.pkey_index      = 0,
		.port_num        = IB_PORT,
	};
	int attr_mask = IBV_QP_STATE | IBV_QP_PORT;

	if (config.qp_type == IBV_QPT_RC) { //RC
		attr_mask |= IBV_QP_ACCESS_FLAGS | IBV_QP_PKEY_INDEX;
		attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
				       IBV_ACCESS_REMOTE_WRITE |
				       IBV_ACCESS_REMOTE_READ |
				       IBV_ACCESS_REMOTE_ATOMIC;
	} else if ((config.qp_type == IBV_QPT_DRIVER && config.is_daemon) ||
		   config.qp_type == IBV_QPT_XRC_RECV) { //DCT and XRC_RECV
		attr_mask |= IBV_QP_ACCESS_FLAGS | IBV_QP_PKEY_INDEX;
		attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
				       IBV_ACCESS_REMOTE_READ |
				       IBV_ACCESS_REMOTE_ATOMIC;
	} else if (config.qp_type == IBV_QPT_DRIVER && !config.is_daemon) { //DCI
		attr_mask |= IBV_QP_PKEY_INDEX;
	} else if (config.qp_type == IBV_QPT_XRC_SEND) { //XRC_SEND
		attr_mask |= IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS;
	} else if (config.qp_type == IBV_QPT_UD) { //UD
		attr_mask |= IBV_QP_QKEY | IBV_QP_PKEY_INDEX;
		attr.qkey = QKEY;
	}
	/* else Raw-Packet */

	if (ibv_modify_qp(resource->qp, &attr, attr_mask)) {
		VL_DATA_ERR(("Fail to modify QP to IBV_QPS_INIT"));
		return FAIL;
	}

	return SUCCESS;
}

static int qp_to_rtr(const struct resources_t *resource, const struct sync_qp_info_t *remote_qp)
{
	struct ibv_qp_attr attr = {
		.qp_state		= IBV_QPS_RTR,
		.ah_attr		= {
			.is_global	= 0,
			.sl		= 0,
			.src_path_bits	= 0,
			}
		};
	int attr_mask = IBV_QP_STATE;

	if (config.qp_type == IBV_QPT_RC || config.qp_type == IBV_QPT_XRC_RECV) {
		attr.dest_qp_num = remote_qp->qp_num;
		attr.ah_attr.dlid = remote_qp->lid;
		attr.max_dest_rd_atomic = 8,
		attr.min_rnr_timer = 0x10;
		attr.rq_psn = 0;
		attr.path_mtu = IBV_MTU_1024;
		attr.ah_attr.port_num = IB_PORT;

		attr_mask |= IBV_QP_AV |
			     IBV_QP_DEST_QPN |
			     IBV_QP_RQ_PSN |
			     IBV_QP_PATH_MTU |
			     IBV_QP_MAX_DEST_RD_ATOMIC |
			     IBV_QP_MIN_RNR_TIMER;
	} else if (config.qp_type == IBV_QPT_XRC_SEND) {
		attr.dest_qp_num = remote_qp->qp_num;
		attr.ah_attr.dlid = remote_qp->lid;
		attr.rq_psn = 0;
		attr.path_mtu = IBV_MTU_1024;
		attr.ah_attr.port_num = IB_PORT;

		attr_mask |= IBV_QP_AV |
			     IBV_QP_DEST_QPN |
			     IBV_QP_RQ_PSN |
			     IBV_QP_PATH_MTU;
	} else if (config.qp_type == IBV_QPT_DRIVER && config.is_daemon) { //DCT
		//attr.ah_attr.is_global = 1; //TODO: not sure if required
		attr.min_rnr_timer = 0x10;
		attr.path_mtu = IBV_MTU_1024;
		attr.ah_attr.port_num = IB_PORT;

		attr_mask |= IBV_QP_AV |
			     IBV_QP_PATH_MTU |
			     IBV_QP_MIN_RNR_TIMER;
	} else if (config.qp_type == IBV_QPT_DRIVER && !config.is_daemon) { //DCI
		/* On DCI we dont need recieve attrs */
		attr.path_mtu = IBV_MTU_1024;

		attr_mask |= IBV_QP_PATH_MTU;
	}
	/* else Raw-Packet or UD*/

	if (ibv_modify_qp(resource->qp, &attr, attr_mask)) {
		VL_DATA_ERR(("Fail to modify QP, to IBV_QPS_RTR"));
		return FAIL;
	}

	return SUCCESS;
}

static int qp_to_rts(const struct resources_t *resource)
{
	struct ibv_qp_attr attr = {
		.qp_state = IBV_QPS_RTS,
		};
	int attr_mask = IBV_QP_STATE;

	if (config.qp_type == IBV_QPT_RC || config.qp_type == IBV_QPT_DRIVER ||
	    config.qp_type == IBV_QPT_XRC_SEND) {
		attr_mask |= IBV_QP_TIMEOUT |
			     IBV_QP_RETRY_CNT |
			     IBV_QP_RNR_RETRY |
			     IBV_QP_SQ_PSN |
			     IBV_QP_MAX_QP_RD_ATOMIC;

		attr.sq_psn = 0,
		attr.timeout = 0x10;
		attr.retry_cnt = 7;
		attr.rnr_retry = 7;
		attr.max_rd_atomic = 8;
	} else if (config.qp_type != IBV_QPT_RAW_PACKET) {
		attr_mask |= IBV_QP_SQ_PSN;

		attr.sq_psn = 0;
	}

	if (ibv_modify_qp(resource->qp, &attr, attr_mask)) {
		VL_DATA_ERR(("Fail to modify QP to IBV_QPS_RTS."));
		return FAIL;
		}

	return SUCCESS;
}

int init_connection(struct resources_t *resource)
{
	struct sync_qp_info_t remote_qp_info = {0};
	struct sync_qp_info_t local_qp_info = {0};
	int rc;

	local_qp_info.qp_num = resource->qp->qp_num;

	local_qp_info.lid = resource->hca_p->port_attr.lid;
	mac_string_to_byte(config.mac, local_qp_info.mac);

	if (!config.is_daemon) {
		rc = send_info(resource, &local_qp_info, sizeof(local_qp_info));
		if (rc)
			return FAIL;

		rc = recv_info(resource, &remote_qp_info, sizeof(remote_qp_info));
		if (rc)
			return FAIL;
	} else {
		rc = recv_info(resource, &remote_qp_info, sizeof(remote_qp_info));
		if (rc)
			return FAIL;

		rc = send_info(resource, &local_qp_info, sizeof(local_qp_info));
		if (rc)
			return FAIL;
	}

	resource->r_dctn = remote_qp_info.qp_num;

	VL_DATA_TRACE1(("Going to connect QP to lid 0x%x qp_num 0x%x",
			remote_qp_info.lid,
			remote_qp_info.qp_num));

	if (qp_to_init(resource))
		return FAIL;

	if (qp_to_rtr(resource, &remote_qp_info))
		return FAIL;

	if(!config.is_daemon) {
		if (qp_to_rts(resource))
			return FAIL;
	}

	VL_DATA_TRACE1(("QP qp_num 0x%x is in ready state", resource->qp->qp_num));

	if ((config.qp_type == IBV_QPT_DRIVER || config.qp_type == IBV_QPT_UD) &&
	    !config.is_daemon) {
		rc = init_ah(resource, (uint16_t)remote_qp_info.lid);
		if (rc)
			return FAIL;
	}

	if (config.qp_type == IBV_QPT_RAW_PACKET) {
		if (config.is_daemon) {
			rc = init_mcast_mac_flow(resource, local_qp_info.mac);
			if (rc)
				return FAIL;
		} else {
			init_eth_header(resource, local_qp_info.mac, remote_qp_info.mac);
		}

	}

	VL_DATA_TRACE(("init_connection is done"));

	return  SUCCESS;
}

int do_test(struct resources_t *resource)
{
	int rc;

	if (!config.is_daemon) {
		VL_DATA_TRACE(("Run sender"));

		resource->measure.min = ~0; //initialize to max value of unsigned type

		if (VL_sock_sync_ready(&resource->sock)) {
			VL_SOCK_ERR(("Sync before traffic"));
			return FAIL;
		}

		if (do_sender(resource))
			return FAIL;

		VL_DATA_TRACE(("Wait for Receiver"));
		if (VL_sock_sync_ready(&resource->sock)) {
			VL_SOCK_ERR(("Sync after traffic"));
			return FAIL;
		}
	} else {
		rc = prepare_receiver(resource);
		if (rc)
			return FAIL;

		VL_DATA_TRACE(("Run receiver"));

		if (VL_sock_sync_ready(&resource->sock)) {
			VL_SOCK_ERR(("Sync before traffic"));
			return FAIL;
		}

		if (config.opcode != IBV_WR_RDMA_WRITE &&
		    config.opcode != IBV_WR_RDMA_READ &&
		    config.opcode != IBV_WR_ATOMIC_FETCH_AND_ADD &&
		    config.opcode != IBV_WR_ATOMIC_CMP_AND_SWP &&
		    config.opcode != IBV_WR_LOCAL_INV &&
		    config.opcode != IBV_WR_BIND_MW) {
			if (do_receiver(resource))
				return FAIL;
		}

		VL_DATA_TRACE(("Wait for Sender"));
		if (VL_sock_sync_ready(&resource->sock)) {
			VL_SOCK_ERR(("Sync after traffic"));
			return FAIL;
		}
	}

	return SUCCESS;
}

int print_results(struct resources_t *resource)
{
	double max;
	double min;
	double average;
	double freq;

	freq = get_cpu_mhz(1) / 1000; //Ghz
	if ((freq == 0)) {
		VL_MISC_ERR(("Can't produce a report"));
		return FAIL;
	}

	max = resource->measure.max / freq; //ns
	min = resource->measure.min / freq; //ns
	average = resource->measure.tot / freq / config.num_of_iter; // time per message (not per batch) [ns].

	VL_MISC_TRACE((" ---------------------- Test Results  ---------------"));
	VL_MISC_TRACE((" Batch (size: %u) was sampled %u times", config.batch_size, resource->measure.batch_samples));
	VL_MISC_TRACE((" Max batch time:                %lf[ns]", max));
	VL_MISC_TRACE((" Min batch time:                %lf[ns]", min));
	VL_MISC_TRACE((" Average time per message:      %lf[ns]", average));
	VL_MISC_TRACE((" ----------------------------------------------------"));

	return SUCCESS;

}
