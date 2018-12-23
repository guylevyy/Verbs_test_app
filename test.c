#include <vl.h>
#include "types.h"

extern struct config_t config;

int force_configurations_dependencies()
{
	if(config.ring_depth < config.batch_size)
		config.ring_depth = config.batch_size;

	return 0;
}

int send_info(struct resources_t *resource, const void *buf, size_t size)
{
	void *tmp_buf;
	int rc = SUCCESS;
	int i;

	VL_SOCK_TRACE1(("Going to send info."));

	if (size % 4) {
		VL_MISC_ERR(("sync_info must get buffer size of multiples of 4"));
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

static int connect_rc_qp(const struct resources_t *resource,
			 const struct sync_qp_info_t *remote_qp)
{
	struct ibv_qp *qp = resource->qp;

	VL_DATA_TRACE1(("Going to RC QP to lid 0x%x qp_num 0x%x",
			remote_qp->lid,
			remote_qp->qp_num));

	{//INIT
	struct ibv_qp_attr attr = {
		.qp_state        = IBV_QPS_INIT,
		.pkey_index      = 0,
		.port_num        = IB_PORT,
		.qp_access_flags = 0
		};

		if (ibv_modify_qp(qp, &attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
			VL_DATA_ERR(("Fail to modify RC QP to IBV_QP_INIT"));
			return FAIL;
		}

	}

	{//RTR
	struct ibv_qp_attr attr = {
		.qp_state		= IBV_QPS_RTR,
		.path_mtu		= IBV_MTU_1024,
		.dest_qp_num		= remote_qp->qp_num,
		.rq_psn			= 0,
		.max_dest_rd_atomic	= 0,
		.min_rnr_timer		= 0x10,
		.ah_attr		= {
			.is_global	= 0,
			.dlid		= remote_qp->lid,
			.sl		= 0,
			.src_path_bits	= 0,
			.port_num	= IB_PORT,
			}
		};

		if (ibv_modify_qp(qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_AV                 |
			  IBV_QP_PATH_MTU           |
			  IBV_QP_DEST_QPN           |
			  IBV_QP_RQ_PSN             |
			  IBV_QP_MAX_DEST_RD_ATOMIC |
			  IBV_QP_MIN_RNR_TIMER)) {
			VL_DATA_ERR(("Fail to modify RC QP, to IBV_QPS_RTR"));
			return FAIL;
			}
	}

	{//RTS
		struct ibv_qp_attr attr = {
		.qp_state		= IBV_QPS_RTS,
		.timeout		= 0x10,
		.retry_cnt		= 7,
		.rnr_retry		= 7,
		.sq_psn			= 0,
		.max_rd_atomic		= 0
		};

		if (ibv_modify_qp(resource->qp, &attr,
			  IBV_QP_STATE              |
			  IBV_QP_TIMEOUT            |
			  IBV_QP_RETRY_CNT          |
			  IBV_QP_RNR_RETRY          |
			  IBV_QP_SQ_PSN             |
			  IBV_QP_MAX_QP_RD_ATOMIC)) {
			VL_DATA_ERR(("Fail to modify RC QP to IBV_QPS_RTS."));
			return FAIL;
			}
	}

	VL_DATA_TRACE1(("RC QP qp_num 0x%x now at RTS.", resource->qp->qp_num));

	return SUCCESS;
}

static inline void set_send_wr(struct resources_t *resource,
			       struct ibv_send_wr *wr, uint16_t size)
{
	int i;

	for (i = 0; i < size; i++) {
		wr[i].wr_id = WR_ID;
		wr[i].opcode = IBV_WR_SEND;
		wr[i].next = &wr[i + 1];
		wr[i].sg_list = &resource->sge_arr[i];
		wr[i].num_sge = DEF_NUM_SGE;

		resource->sge_arr[i].addr = (uintptr_t) resource->mr->addr;
		resource->sge_arr[i].length = config.msg_sz;
		resource->sge_arr[i].lkey = resource->mr->ibv_mr->lkey;
	}

	wr[size - 1].next = NULL;
}

static inline void set_recv_wr(struct resources_t *resource,
			       struct ibv_recv_wr *wr, uint16_t size)
{
	int i;

	for (i = 0; i < size; i++) {
		wr[i].wr_id = WR_ID;
		wr[i].next = &wr[i + 1];
		wr[i].sg_list = &resource->sge_arr[i];
		wr[i].num_sge = DEF_NUM_SGE;

		resource->sge_arr[i].addr = (uintptr_t) resource->mr->addr;
		resource->sge_arr[i].length = config.msg_sz;
		resource->sge_arr[i].lkey = resource->mr->ibv_mr->lkey;
	}

	wr[size - 1].next = NULL;
}

int prepare_receiver(struct resources_t *resource)
{
	int i;

	set_recv_wr(resource, resource->recv_wr_arr, 1);

	for (i = 0; i < (int) config.ring_depth; i++) {
		struct ibv_recv_wr *bad_wr = NULL;
		int rc;

		rc = ibv_post_recv(resource->qp, resource->recv_wr_arr, &bad_wr);
		if (rc) {
			VL_MISC_ERR(("in ibv_post_receive", strerror(rc)));
			return FAIL;
		}
	}

	return SUCCESS;
}

static int do_sender(struct resources_t *resource)
{
	uint32_t tot_ccnt = 0;
	uint32_t tot_scnt = 0;
	int result = SUCCESS;

	while (tot_ccnt < config.num_of_iter) {
		int rc = 0;

		if ((tot_scnt < config.num_of_iter) && (tot_scnt - tot_ccnt < config.ring_depth)) {
			struct ibv_send_wr *bad_wr = NULL;

			set_send_wr(resource, resource->send_wr_arr, config.batch_size);

			rc = ibv_post_send(resource->qp, resource->send_wr_arr, &bad_wr);
			if (rc) {
				VL_MISC_ERR(("in ibv_post_send (%s)", strerror(rc)));
				result = FAIL;
				goto out;
			}

			tot_scnt += config.batch_size;
		}

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

		if ((tot_rcnt < config.num_of_iter) && (tot_rcnt - tot_ccnt < config.ring_depth)) {
			struct ibv_recv_wr *bad_wr = NULL;

			rc = ibv_post_recv(resource->qp, resource->recv_wr_arr, &bad_wr);
			if (rc) {
				VL_MISC_ERR(("in ibv_post_receive", strerror(rc)));
				result = FAIL;
				goto out;
			}

			tot_rcnt += config.batch_size;
		}
	}

out:
	VL_DATA_TRACE(("Receiver exit with tot_rcnt=%u tot_ccnt=%u", tot_rcnt, tot_ccnt));

	return result;
}

int do_test(struct resources_t *resource)
{
	struct sync_qp_info_t remote_qp_info = {0};
	struct sync_qp_info_t local_qp_info = {0};
	int rc;


	local_qp_info.qp_num = resource->qp->qp_num;
	local_qp_info.lid = resource->hca_p->port_attr.lid;

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

	rc = connect_rc_qp(resource, &remote_qp_info);
	if (rc)
		return FAIL;

	if (!config.is_daemon) {
		VL_DATA_TRACE(("Run sender"));

		if (VL_sock_sync_ready(&resource->sock)) {
			VL_SOCK_ERR(("Sync before traffic"));
			return FAIL;
		}

		if (do_sender(resource))
			return FAIL;
	} else {
		rc = prepare_receiver(resource);
		if (rc)
			return FAIL;

		VL_DATA_TRACE(("Run receiver"));

		if (VL_sock_sync_ready(&resource->sock)) {
			VL_SOCK_ERR(("Sync before traffic"));
			return FAIL;
		}

		if (do_receiver(resource))
			return FAIL;
	}

	return SUCCESS;
}

