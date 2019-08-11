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

	if (config.ext_atomic) {
		if (config.opcode == IBV_WR_ATOMIC_FETCH_AND_ADD) {
			resource->atomic_args = calloc(2, config.msg_sz);
			if (!resource->atomic_args) {
				VL_MEM_ERR((" Fail in alloc atomic_args"));
				return FAIL;
			}
		} else {
			struct mlx5dv_comp_swap *args;

			resource->atomic_args = calloc(1, sizeof(struct mlx5dv_comp_swap));
			if (!resource->atomic_args) {
				VL_MEM_ERR((" Fail in alloc atomic_args"));
				return FAIL;
			}
			args = resource->atomic_args;

			args->swap_val = calloc(4, config.msg_sz);
			if (!args->swap_val) {
				VL_MEM_ERR((" Fail in alloc atomic_args"));
				return FAIL;
			}
			args->compare_val = args->swap_val + config.msg_sz;
			args->swap_mask = args->swap_val + 2 * config.msg_sz;
			args->compare_mask = args->swap_val + 3 * config.msg_sz;
		}
	}

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
	struct mlx5dv_context_attr ctx_attr;
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

	memset(&ctx_attr, 0, sizeof(ctx_attr));
	ctx_attr.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
	resource->hca_p->context = mlx5dv_open_device(ib_dev, &ctx_attr);
	if (!resource->hca_p->context) {
		VL_HCA_ERR(("mlx5dv_open_device with HCA ID %s failed",
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

	if (config.ext_atomic) {
		struct mlx5dv_context dv_attr;

		memset(&dv_attr, 0, sizeof(dv_attr));

		dv_attr.comp_mask = MLX5DV_CONTEXT_MASK_ATOMICS;
		rc = mlx5dv_query_device(resource->hca_p->context, &dv_attr);
		if (rc) {
			VL_HCA_ERR(("mlx5dv_query_device failed"));
			return FAIL;
		}
		VL_HCA_TRACE1(("mlx5dv HCA was queried"));

		if (dv_attr.comp_mask & MLX5DV_CONTEXT_MASK_ATOMICS)
			VL_HCA_TRACE1(("arg_size_mask=0x%x arg_size_mask_dc=0x%x", dv_attr.atomics_caps.arg_size_mask,
					dv_attr.atomics_caps.arg_size_mask_dc));
	}

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

static int init_xrcd(struct resources_t *resource)
{

	struct ibv_xrcd_init_attr xrcd_attr;
	int fd;

	if (config.qp_type != IBV_QPT_XRC_SEND && config.qp_type != IBV_QPT_XRC_RECV)
		return SUCCESS;

	VL_HCA_TRACE1(("Going to create XRCD"));

	fd = open("/dev/null", O_WRONLY);
	if (fd < 0) {
		VL_DATA_ERR(("Fail to open a file"));
		return FAIL;
	}

	memset(&xrcd_attr, 0, sizeof(xrcd_attr));
	xrcd_attr.comp_mask = IBV_XRCD_INIT_ATTR_FD | IBV_XRCD_INIT_ATTR_OFLAGS;
        xrcd_attr.fd = fd;
        xrcd_attr.oflags = O_CREAT;

	resource->xrcd = ibv_open_xrcd(resource->hca_p->context, &xrcd_attr);

	VL_DATA_TRACE1(("Finish init XRCD"));

	return SUCCESS;
}

static int destroy_xrcd(struct resources_t *resource)
{
	int rc;
	int rc_1 = SUCCESS;

	if (resource->fd < 0)
		return SUCCESS;

	if (resource->xrcd) {
		VL_DATA_TRACE1(("Going to close XRCD"));
		rc_1 = ibv_close_xrcd(resource->xrcd);
		CHECK_VALUE("ibv_close_xrcd", rc_1, 0, );
		VL_DATA_TRACE1(("Finish closing XRCD"));
	}

	VL_DATA_TRACE1(("Going to close fd"));
	rc = close(resource->fd);
	CHECK_VALUE("close(fd)", rc, 0, return FAIL);
	VL_DATA_TRACE1(("Finish closing fd"));

	if (rc_1 != 0)
		return FAIL;

	return SUCCESS;
}

static int init_srq(struct resources_t *resource)
{
	struct ibv_srq_init_attr_ex attr;
	uint32_t srqn;

	if ((config.qp_type != IBV_QPT_DRIVER && config.qp_type != IBV_QPT_XRC_RECV) || !config.is_daemon)
		return SUCCESS;

	VL_HCA_TRACE1(("Going to create SRQ"));

	memset(&attr, 0, sizeof(attr));
        attr.comp_mask = IBV_SRQ_INIT_ATTR_TYPE | IBV_SRQ_INIT_ATTR_PD;
	attr.attr.max_wr = config.ring_depth;
	attr.attr.max_sge = config.num_sge;
	attr.pd = resource->pd;

	if (config.qp_type == IBV_QPT_DRIVER) {
		attr.srq_type = IBV_SRQT_BASIC;
	} else {
		attr.comp_mask |= IBV_SRQ_INIT_ATTR_XRCD | IBV_SRQ_INIT_ATTR_CQ;
		attr.srq_type = IBV_SRQT_XRC;
		attr.xrcd = resource->xrcd;
		attr.cq = resource->cq;
	}

	resource->srq = ibv_create_srq_ex(resource->hca_p->context, &attr);
	if (!resource->srq) {
		VL_DATA_ERR(("Fail in ibv_create_srq_ex"));
		return FAIL;
	}

	ibv_get_srq_num(resource->srq, &srqn);

	VL_DATA_TRACE1(("Finish init SRQ 0x%x", srqn));

	return SUCCESS;
}

static int init_qp(struct resources_t *resource)
{
	struct ibv_qp_init_attr *attr;
	struct ibv_qp_init_attr_ex attr_ex;
	struct mlx5dv_qp_init_attr attr_dv;

	memset(&attr_dv, 0, sizeof(attr_dv));
	memset(&attr_ex, 0, sizeof(attr_ex));
	attr = (struct ibv_qp_init_attr *)&attr_ex;

	attr->qp_type = config.qp_type;
	VL_DATA_TRACE1(("Going to create QP type %s:", VL_ibv_qp_type_str(config.qp_type)));

	if (config.qp_type != IBV_QPT_XRC_RECV || config.qp_type != IBV_QPT_XRC_SEND)
		attr->recv_cq = resource->cq;
	if (config.qp_type != IBV_QPT_XRC_RECV)
		attr->send_cq = resource->cq; /* Relevant also for DCT */

	/* DCT nor DCI nor XRC_SEND nor XRC_RECV_has receive properties */
	if (config.qp_type != IBV_QPT_DRIVER && config.qp_type != IBV_QPT_XRC_SEND &&
	    config.qp_type != IBV_QPT_XRC_RECV) {
		attr->cap.max_recv_sge = config.num_sge;
		attr->cap.max_recv_wr = config.ring_depth;

		VL_DATA_TRACE1(("max_recv_wr %d, max_recv_sge %d",
				attr->cap.max_recv_wr,
				attr->cap.max_recv_sge));
	}

	/* We dont want to configure send properties on DCT or XRC_RECV*/
	if (!(config.qp_type == IBV_QPT_DRIVER && config.is_daemon) && config.qp_type != IBV_QPT_XRC_RECV) {
		attr->sq_sig_all = 1;
		attr->cap.max_inline_data = config.use_inl ? config.msg_sz : 0;
		attr->cap.max_send_sge = config.num_sge;
		attr->cap.max_send_wr = config.ring_depth;

		VL_DATA_TRACE1(("max_send_wr %d, max_send_sge %d max_inline_data %d",
				attr->cap.max_send_wr,
				attr->cap.max_send_sge,
				attr->cap.max_inline_data));
	}

	if (!config.is_daemon) {
		attr_ex.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS | IBV_QP_INIT_ATTR_PD;

		if(0);
		else if (config.opcode == IBV_WR_SEND)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_SEND;
		else if (config.opcode == IBV_WR_SEND_WITH_IMM)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_SEND_WITH_IMM;
		else if (config.opcode == IBV_WR_RDMA_WRITE)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_RDMA_WRITE;
		else if (config.opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_RDMA_WRITE_WITH_IMM;
		else if (config.opcode == IBV_WR_RDMA_READ)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_RDMA_READ;
		else if (config.opcode == IBV_WR_ATOMIC_FETCH_AND_ADD && !config.ext_atomic)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_ATOMIC_FETCH_AND_ADD;
		else if (config.opcode == IBV_WR_ATOMIC_CMP_AND_SWP && !config.ext_atomic)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_ATOMIC_CMP_AND_SWP;
		else if (config.opcode == IBV_WR_BIND_MW)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_BIND_MW;
		else if (config.opcode == IBV_WR_LOCAL_INV)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_LOCAL_INV;
		else if (config.opcode == IBV_WR_SEND_WITH_INV)
			attr_ex.send_ops_flags |= IBV_QP_EX_WITH_SEND_WITH_INV;

		attr_ex.pd = resource->pd;

		if (config.qp_type == IBV_QPT_DRIVER) {
			attr_dv.comp_mask |= MLX5DV_QP_INIT_ATTR_MASK_DC |
					     MLX5DV_QP_INIT_ATTR_MASK_QP_CREATE_FLAGS;
			attr_dv.create_flags |= MLX5DV_QP_CREATE_DISABLE_SCATTER_TO_CQE; /*driver doesnt support scatter2cqe data-path on DCI yet*/
			attr_dv.dc_init_attr.dc_type = MLX5DV_DCTYPE_DCI;
		}

		if (config.ext_atomic) {
			attr_dv.comp_mask |= MLX5DV_QP_INIT_ATTR_MASK_SEND_OPS_FLAGS |
					     MLX5DV_QP_INIT_ATTR_MASK_QP_CREATE_FLAGS;
			attr_dv.create_flags |= MLX5DV_QP_CREATE_DISABLE_SCATTER_TO_CQE; /*driver doesnt support scatter2cqe data-path for ext atomic yet*/
			attr_dv.send_ops_flags |= MLX5DV_QP_EX_WITH_ATOMIC;
			attr_dv.comp_mask |= MLX5DV_QP_INIT_ATTR_MASK_ATOMIC_ARG;
			attr_dv.max_atomic_arg = config.msg_sz;
		}

		if (config.qp_type == IBV_QPT_DRIVER || config.ext_atomic) {
			resource->qp = mlx5dv_create_qp(resource->hca_p->context, &attr_ex, &attr_dv);
		} else {
			resource->qp = ibv_create_qp_ex(resource->hca_p->context, &attr_ex);
		}
	} else {
		attr_ex.comp_mask |= IBV_QP_INIT_ATTR_PD;
		attr_ex.pd = resource->pd;

		if (config.ext_atomic) {
			attr_dv.comp_mask |= MLX5DV_QP_INIT_ATTR_MASK_ATOMIC_ARG;
			attr_dv.max_atomic_arg = config.msg_sz;
		}

		if (config.qp_type == IBV_QPT_XRC_RECV) {
			attr_ex.comp_mask |= IBV_QP_INIT_ATTR_XRCD;
			attr_ex.xrcd = resource->xrcd;
		}

		if (config.qp_type == IBV_QPT_DRIVER) {
			attr_ex.srq = resource->srq;
			attr_dv.comp_mask |= MLX5DV_QP_INIT_ATTR_MASK_DC;
			attr_dv.dc_init_attr.dc_type = MLX5DV_DCTYPE_DCT;
			attr_dv.dc_init_attr.dct_access_key = DC_KEY;
		}

		resource->qp = mlx5dv_create_qp(resource->hca_p->context, &attr_ex, &attr_dv);
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


	if (config.send_method) {
		resource->eqp = ibv_qp_to_qp_ex(resource->qp);
		if (config.qp_type == IBV_QPT_DRIVER || config.ext_atomic)
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
			   IBV_ACCESS_MW_BIND |
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

static int init_mw(struct resources_t *resource)
{
	if (config.opcode != IBV_WR_SEND_WITH_INV &&
	    config.opcode != IBV_WR_BIND_MW &&
	    config.opcode != IBV_WR_LOCAL_INV)
		return SUCCESS;

	resource->mw =
		ibv_alloc_mw(resource->pd, IBV_MW_TYPE_2);
	if (!resource->mw) {
		VL_MEM_ERR(("Fail in ibv_alloc_mw"));
		return FAIL;
	}

	VL_MEM_TRACE1(("Finish init MW"));

	return SUCCESS;
}

int resource_init(struct resources_t *resource)
{

	if (init_socket(resource) != SUCCESS ||
	    init_hca(resource) != SUCCESS ||
	    init_pd(resource) != SUCCESS ||
	    init_xrcd(resource) != SUCCESS ||
	    init_cq(resource) != SUCCESS ||
	    init_srq(resource) != SUCCESS ||
	    init_qp(resource) != SUCCESS ||
	    init_mr(resource) != SUCCESS ||
	    init_mw(resource)) {
			VL_MISC_ERR(("Fail to init resource"));
			return FAIL;
	}
	VL_MISC_TRACE(("Finish resource init"));
	return SUCCESS;
}

static int destroy_mw(struct resources_t *resource)
{
	int rc;

	if (!resource->mw)
		return SUCCESS;

	VL_DATA_TRACE1(("Going to destroy MW"));
	rc = ibv_dealloc_mw(resource->mw);
	CHECK_VALUE("ibv_dealloc_mw", rc, 0, return FAIL);

	VL_DATA_TRACE1(("Finish destroy MW"));
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

static int destroy_flow(struct resources_t *resource)
{
	int rc;

	if (!resource->flow)
		return SUCCESS;

	VL_DATA_TRACE1(("Going to destroy flow rule"));
	rc = ibv_destroy_flow(resource->flow);
	CHECK_VALUE("ibv_destroy_flow", rc, 0, return FAIL);

	VL_DATA_TRACE1(("Finish destroy flow rule"));

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

	if (destroy_mw(resource) != SUCCESS ||
	    destroy_all_mr(resource) != SUCCESS	||
	    destroy_flow(resource) != SUCCESS ||
	    destroy_ah(resource) != SUCCESS ||
	    destroy_qp(resource) != SUCCESS ||
	    destroy_srq(resource) != SUCCESS ||
	    destroy_cq(resource) != SUCCESS ||
	    destroy_xrcd(resource) != SUCCESS ||
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
	if (resource->atomic_args) {
		if (config.ext_atomic && config.opcode == IBV_WR_ATOMIC_CMP_AND_SWP)
			VL_FREE(((struct mlx5dv_comp_swap *)resource->atomic_args)->swap_val);

		VL_FREE(resource->atomic_args);
	}
	if (resource->data_buf_arr)
		VL_FREE(resource->data_buf_arr);

	VL_MISC_TRACE(("*********** Destroy all resource. *************"));
	return result1;
}

