#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sched.h>
#include <vl.h>
#include <vl_verbs.h>
#include "resources.h"
#include <infiniband/mlx5dv.h>

extern struct config_t config;

int resource_alloc(struct resources_t *resource)
{
	size_t size;

	size = sizeof(struct mr_data_t);
	resource->mr = VL_MALLOC(size, struct mr_data_t);
	if (!resource->mr) {
		VL_MEM_ERR((" Failed to malloc mr"));
		return FAIL;
	}
	memset(resource->mr, 0, size);

	resource->mr->addr = VL_MALLOC(config.msg_sz, void);
	if (!resource->mr->addr) {
		VL_MEM_ERR(("Failed to malloc data-buffer"));
		return FAIL;
	}
	VL_MEM_TRACE1(("Data buffer address %p", resource->mr->addr));

	memset(resource->mr->addr, 0xE, config.msg_sz);

	size = config.batch_size * sizeof(struct ibv_wc);
	resource->wc_arr = VL_MALLOC(size, struct ibv_wc);
	if (!resource->wc_arr) {
		VL_MEM_ERR((" Fail in alloc wr_arr"));
		return FAIL;
	}
	memset(resource->wc_arr, 0, size);

	size = config.batch_size * sizeof(struct ibv_sge) * config.num_sge;
	resource->sge_arr = VL_MALLOC(size, struct ibv_sge);
	if (!resource->sge_arr) {
		VL_MEM_ERR((" Failed to malloc sge_arr"));
		return FAIL;
	}
	memset(resource->sge_arr, 0, size);

	size = config.batch_size * sizeof(struct ibv_data_buf) * config.num_sge;
	resource->data_buf_arr = VL_MALLOC(size, struct ibv_data_buf);
	if (!resource->data_buf_arr) {
		VL_MEM_ERR((" Failed to malloc data_buf_arr"));
		return FAIL;
	}
	memset(resource->data_buf_arr, 0, size);

	size = config.batch_size * sizeof(struct ibv_send_wr);
	resource->send_wr_arr = VL_MALLOC(size, struct ibv_send_wr);
	if (!resource->send_wr_arr) {
		VL_MEM_ERR((" Fail in alloc send_wr_arr"));
		return FAIL;
	}
	memset(resource->send_wr_arr, 0, size);

	size = config.batch_size * sizeof(struct ibv_recv_wr);
	resource->recv_wr_arr = VL_MALLOC(size, struct ibv_recv_wr);
	if (!resource->recv_wr_arr) {
		VL_MEM_ERR((" Fail in alloc recv_wr_arr"));
		return FAIL;
	}
	memset(resource->recv_wr_arr, 0, size);

	VL_MEM_TRACE((" resource alloc finish."));
	return SUCCESS;
}

static int init_socket(struct resources_t *resource)
{
	struct VL_sock_props_t	sock_prop;

	if (!config.is_daemon)
		strcpy(sock_prop.ip, config.ip);

	sock_prop.is_daemon = resource->sock.is_daemon = config.is_daemon;
	sock_prop.port = resource->sock.port = config.tcp;

	/*config.sock was init in process_arg. */
	VL_sock_init(&resource->sock);

	if (VL_sock_connect(&sock_prop, &resource->sock) != 0) {
		VL_SOCK_ERR(("Fail in VL_sock_connect."));
		return FAIL;
	}

	VL_SOCK_TRACE(("Connection was established."));
	return SUCCESS;
}

static int init_hca(struct resources_t *resource)
{
	struct ibv_device *ib_dev = NULL;
	struct ibv_device **dev_list;
	int num_devices, i, rc;

	resource->hca_p = VL_MALLOC(sizeof(struct hca_data_t), struct hca_data_t);
	if (!resource->hca_p) {
		VL_MEM_ERR(("Fail to alloc hca_data_t."));
		exit(-1);
	}

	dev_list = ibv_get_device_list(&num_devices);
	if (!dev_list) {
		VL_HCA_ERR(("ibv_get_device_list failed"));
		exit(-1);
	}

	for (i = 0; i < num_devices; i++) {
		if (!strcmp(ibv_get_device_name(dev_list[i]), config.hca_type)) {
			ib_dev = dev_list[i];
			break;
		}
	}

	if (!ib_dev) {
		VL_HCA_ERR(("HCA ID %s wasn't found in host",
				config.hca_type));
		ibv_free_device_list(dev_list);
		return FAIL;
	}

	resource->hca_p->context = ibv_open_device(ib_dev);
	if (!resource->hca_p->context) {
		VL_HCA_ERR(("ibv_open_device with HCA ID %s failed",
				config.hca_type));
		ibv_free_device_list(dev_list);
		return FAIL;
	}

	VL_HCA_TRACE1(("HCA %s was opened, context = %p",
			config.hca_type, resource->hca_p->context));

	ibv_free_device_list(dev_list);

	rc = ibv_query_device(resource->hca_p->context, &resource->hca_p->device_attr);
	if (rc) {
		VL_HCA_ERR(("ibv_query_device failed"));
		return FAIL;
	}
	VL_HCA_TRACE1(("HCA was queried"));

	rc = ibv_query_port(resource->hca_p->context, IB_PORT, &resource->hca_p->port_attr);
	if (rc) {
		VL_HCA_ERR(("ibv_query_port failed"));
		return FAIL;
	}

	/* check that the port is in active state */
	if (resource->hca_p->port_attr.state != IBV_PORT_ACTIVE) {
		VL_HCA_ERR(("IB Port is not in active state"));
		return FAIL;
	}

	return SUCCESS;
}

static int init_pd(struct resources_t *resource)
{

	VL_HCA_TRACE1(("Going to create PD"));
	resource->pd = ibv_alloc_pd(resource->hca_p->context);

	if (!resource->pd) {
		VL_HCA_ERR(("Fail to create PD"));
		return FAIL;
	}

	VL_HCA_TRACE1(("Finish init PD"));
	return SUCCESS;
}

static int init_cq(struct resources_t *resource)
{
	resource->cq = 	ibv_create_cq(resource->hca_p->context, config.ring_depth, NULL, NULL, 0);
	if (!resource->cq) {
		VL_DATA_ERR(("Fail in ibv_create_cq"));
		return FAIL;
	}

	VL_DATA_TRACE1(("Finish init CQ"));

	return SUCCESS;
}

static int init_srq(struct resources_t *resource)
{
	struct ibv_srq_init_attr attr;
	uint32_t srqn;

	if (config.qp_type != IBV_QPT_DRIVER || !config.is_daemon)
		return SUCCESS;

	memset(&attr, 0, sizeof(attr));
	attr.attr.max_wr = config.ring_depth;
	attr.attr.max_sge = config.num_sge;

	resource->srq = ibv_create_srq(resource->pd, &attr);
	if (!resource->srq) {
		VL_DATA_ERR(("Fail in ibv_create_srq"));
		return FAIL;
	}

	ibv_get_srq_num(resource->srq, &srqn);

	VL_DATA_TRACE1(("Finish init SRQ 0x%x", srqn));

	return SUCCESS;
}

static int init_qp(struct resources_t *resource)
{
	struct ibv_qp_init_attr *attr;
	struct ibv_qp_init_attr attr_leg;
	struct ibv_qp_init_attr_ex attr_ex;
	struct mlx5dv_qp_init_attr attr_dv;

	if (config.new_api || config.qp_type == IBV_QPT_DRIVER) {
		memset(&attr_dv, 0, sizeof(attr_dv));
		memset(&attr_ex, 0, sizeof(attr_ex));
		attr = (struct ibv_qp_init_attr *)&attr_ex;
	} else {
		memset(&attr_leg, 0, sizeof(attr_leg));
		attr = &attr_leg;
	}

	attr->qp_type = config.qp_type;
	attr->recv_cq = resource->cq;
	attr->send_cq = resource->cq; /* Relevant also for DCT */

	/* DCT nor DCI has receive properties */
	if (config.qp_type != IBV_QPT_DRIVER) {
		attr->cap.max_recv_sge = config.num_sge;
		attr->cap.max_recv_wr = config.ring_depth;
	}

	/* We dont want to configure send properties on DCT */
	if (!(config.qp_type == IBV_QPT_DRIVER && config.is_daemon)) {
		attr->sq_sig_all = 1;
		attr->cap.max_inline_data = config.use_inl ? config.msg_sz : 0;
		attr->cap.max_send_sge = config.num_sge;
		attr->cap.max_send_wr = config.ring_depth;
	}

	VL_DATA_TRACE1(("Going to create QP type %s, max_send_wr %d, max_send_sge %d max_inline_data %d",
			VL_ibv_qp_type_str(config.qp_type),
			attr->cap.max_send_wr,
			attr->cap.max_send_sge,
			attr->cap.max_inline_data));

	if (config.new_api) { // And correspondingly a client
		attr_ex.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

		if (config.opcode == IBV_WR_SEND)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_SEND;
		else if (config.opcode == IBV_WR_RDMA_WRITE)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_RDMA_WRITE;

		attr_ex.pd = resource->pd;

		if (config.qp_type == IBV_QPT_DRIVER) {
			attr_dv.comp_mask |= MLX5DV_QP_INIT_ATTR_MASK_DC |
					     MLX5DV_QP_INIT_ATTR_MASK_QP_CREATE_FLAGS;
			attr_dv.create_flags |= MLX5DV_QP_CREATE_DISABLE_SCATTER_TO_CQE; /*driver doesnt support scatter2cqe data-path on DCI yet*/
			attr_dv.dc_init_attr.dc_type = MLX5DV_DCTYPE_DCI;
			resource->qp = mlx5dv_create_qp(resource->hca_p->context, &attr_ex, &attr_dv);
		} else {
			resource->qp = ibv_create_qp_ex(resource->hca_p->context, &attr_ex);
		}
	} else { // A server or a client with legacy API
		if (config.qp_type == IBV_QPT_DRIVER) {
			attr_ex.comp_mask |= IBV_QP_INIT_ATTR_PD;
			attr_ex.pd = resource->pd;
			attr_ex.srq = resource->srq;
			attr_dv.comp_mask |= MLX5DV_QP_INIT_ATTR_MASK_DC;
			attr_dv.dc_init_attr.dc_type = MLX5DV_DCTYPE_DCT;
			attr_dv.dc_init_attr.dct_access_key = DC_KEY;
			resource->qp = mlx5dv_create_qp(resource->hca_p->context, &attr_ex, &attr_dv);
		} else {
			resource->qp = ibv_create_qp(resource->pd, &attr_leg);
		}
	}

	if (!resource->qp) {
		VL_DATA_ERR(("Fail to create QP"));
		return FAIL;
	}

	VL_DATA_TRACE1(("Created QP type %s, max_send_wr %d, max_send_sge %d max_inline_data %d",
			VL_ibv_qp_type_str(config.qp_type),
			attr->cap.max_send_wr,
			attr->cap.max_send_sge,
			attr->cap.max_inline_data));


	if (config.new_api) {
		resource->eqp = ibv_qp_to_qp_ex(resource->qp);
		if (config.qp_type == IBV_QPT_DRIVER)
			resource->dv_qp = mlx5dv_qp_ex_from_ibv_qp_ex(resource->eqp);
	}

	VL_DATA_TRACE1(("QP num 0x%x was created", resource->qp->qp_num));

	VL_DATA_TRACE1(("Finish init QP"));
	return SUCCESS;
}

static int init_mr(struct resources_t *resource)
{
	resource->mr->ibv_mr =
		ibv_reg_mr(resource->pd, resource->mr->addr,
			   config.msg_sz,
			   IBV_ACCESS_LOCAL_WRITE |
			   IBV_ACCESS_REMOTE_WRITE |
			   IBV_ACCESS_REMOTE_READ |
			   IBV_ACCESS_REMOTE_ATOMIC);
	if (!resource->mr->ibv_mr) {
		VL_MEM_ERR(("Fail in ibv_reg_mr"));
		return FAIL;
	}

	VL_MEM_TRACE1(("MR created, addr = %p, size = %d, lkey = 0x%x",
			resource->mr->ibv_mr->addr,
			resource->mr->ibv_mr->length,
			resource->mr->ibv_mr->lkey));

	VL_MEM_TRACE1(("Finish init MR"));

	return SUCCESS;
}

int resource_init(struct resources_t *resource)
{

	if (init_socket(resource) != SUCCESS ||
	    init_hca(resource) != SUCCESS ||
	    init_pd(resource) != SUCCESS ||
	    init_cq(resource) != SUCCESS ||
	    init_srq(resource) != SUCCESS ||
	    init_qp(resource) != SUCCESS ||
	    init_mr(resource) != SUCCESS){
			VL_MISC_ERR(("Fail to init resource"));
			return FAIL;
	}
	VL_MISC_TRACE(("Finish resource init"));
	return SUCCESS;
}

static int destroy_all_mr(struct resources_t *resource)
{
	int rc;
	int result1 = SUCCESS;

	if (resource->mr) {
		if (resource->mr->ibv_mr) {
			VL_MEM_TRACE1(("Going to destroy MR"));
			rc = ibv_dereg_mr(resource->mr->ibv_mr);
			CHECK_VALUE("ibv_reg_mr", rc, 0, result1 = FAIL);
			VL_FREE(resource->mr->addr);
		}

		VL_FREE(resource->mr);
	}

	VL_MEM_TRACE1(("Finish destroy all MR"));
	return result1;
}

static int destroy_qp(struct resources_t *resource)
{
	int rc;

	if (!resource->qp)
		return SUCCESS;

	VL_DATA_TRACE1(("Going to destroy QP"));
	rc = ibv_destroy_qp(resource->qp);
	CHECK_VALUE("ibv_destroy_qp", rc, 0, return FAIL);

	VL_DATA_TRACE1(("Finish destroy QP"));

	return SUCCESS;
}

static int destroy_ah(struct resources_t *resource)
{
	int rc;

	if (!resource->ah)
		return SUCCESS;

	VL_DATA_TRACE1(("Going to destroy AH"));
	rc = ibv_destroy_ah(resource->ah);
	CHECK_VALUE("ibv_destroy_ah", rc, 0, return FAIL);

	VL_DATA_TRACE1(("Finish destroy AH"));

	return SUCCESS;
}

static int destroy_srq(struct resources_t *resource)
{
	int rc;

	if (!resource->srq)
		return SUCCESS;

	VL_DATA_TRACE1(("Going to destroy SRQ"));
	rc = ibv_destroy_srq(resource->srq);
	CHECK_VALUE("ibv_destroy_srq", rc, 0, return FAIL);

	VL_DATA_TRACE1(("Finish destroy SRQ"));

	return SUCCESS;
}

static int destroy_cq(struct resources_t *resource)
{
	int rc;

	if (!resource->cq)
		return SUCCESS;

	VL_DATA_TRACE1(("Going to destroy CQ."));
	rc = ibv_destroy_cq(resource->cq);
	CHECK_VALUE("ibv_destroy_cq", rc, 0, return FAIL);

	VL_DATA_TRACE1(("Finish destroy CQ."));

	return SUCCESS;
}

static int destroy_pd(struct resources_t *resource)
{
	int rc;

	if (!resource->pd)
		return SUCCESS;

	VL_HCA_TRACE1(("Going to dealloc_pd"));
	rc = ibv_dealloc_pd(resource->pd);
	if (rc) {
		VL_HCA_ERR((" Fail in ibv_dealloc_pd Error %s",	strerror(rc)));
		return FAIL;

	}

	VL_HCA_TRACE1(("Finish destroy PD"));

	return SUCCESS;
}

static int destroy_hca(struct resources_t *resource)
{
	int rc;
	int result1 = SUCCESS;

	if (!resource->hca_p || !resource->hca_p->context)
		return SUCCESS;

	VL_HCA_TRACE1(("Going to close device"));
	rc = ibv_close_device(resource->hca_p->context);
	if (rc) {
		VL_HCA_ERR((" Fail in ibv_close_device"
				" Error %s",
				strerror(rc)));
		result1 = FAIL;
	}

	VL_FREE(resource->hca_p);
	VL_HCA_TRACE1(("Finish destroy HCA."));
	return result1;
}

int resource_destroy(struct resources_t *resource)
{
	int result1 = SUCCESS;

	if (resource->sock.sock_fd) {
		VL_sock_close(&resource->sock);
		VL_SOCK_TRACE((" Close the Socket."));
	}
	//destroy_recv_wr(resource);

	if (destroy_all_mr(resource) != SUCCESS	||
	    destroy_ah(resource) != SUCCESS ||
	    destroy_qp(resource) != SUCCESS ||
	    destroy_srq(resource) != SUCCESS ||
	    destroy_cq(resource) != SUCCESS ||
	    destroy_pd(resource) != SUCCESS ||
	    destroy_hca(resource) != SUCCESS)
		result1 = FAIL;

	if (resource->wc_arr)
		VL_FREE(resource->wc_arr);
	if (resource->send_wr_arr)
		VL_FREE(resource->send_wr_arr);
	if (resource->recv_wr_arr)
		VL_FREE(resource->recv_wr_arr);
	if (resource->sge_arr)
		VL_FREE(resource->sge_arr);
	if (resource->data_buf_arr)
		VL_FREE(resource->data_buf_arr);

	VL_MISC_TRACE(("*********** Destroy all resource. *************"));
	return result1;
}

