#ifndef GEN2_SRQ__TEST_TYPE_H
#define GEN2_SRQ__TEST_TYPE_H

#include "get_clock.h"

#define IB_PORT 1
#define DEF_NUM_SGE 1
#define DEF_BATCH_SIZE 1
#define DEF_RING_DEPTH 64
#define WR_ID 0xFE

#define ALWAYS_INLINE __attribute__((always_inline))

#define CHECK_VALUE(verb, act_val, exp_val, cmd)			\
	if ((act_val) != (exp_val)) {					\
		VL_MISC_ERR(("Error in %s, "				\
			     "expected value %d, actual value %d",	\
			     (verb), (exp_val), (act_val)));		\
			     cmd;					\
		     }

#define CHECK_RC(rc, msg)						\
	if ((rc) != SUCCESS) {						\
		VL_MISC_ERR(("TEST FAIL (%s)", (msg)));			\
		goto cleanup;						\
	}

enum {
	SUCCESS = 0,
	FAIL = -1,
};

struct config_t {
	char		*hca_type;
	char		ip[VL_IP_STR_LENGTH+1];
	int		tcp;
	int		is_daemon;
	int		new_api;
	int		wait;
	int		qp_type;
	int		use_inl;
	size_t		msg_sz;
	uint16_t	batch_size;
	uint16_t	ring_depth;
	uint16_t	num_sge;
	uint32_t	num_of_iter;
};

struct hca_data_t {
	struct ibv_device_attr	device_attr;
	struct ibv_port_attr	port_attr;
	struct ibv_device	*ib_dev;
	struct ibv_context	*context;
};

struct mr_data_t {
	struct ibv_mr		*ibv_mr;
	void			*addr;
};

struct sync_qp_info_t {
	uint32_t	qp_num;
	uint32_t	lid;
} __attribute__ ((packed));

struct measure_t {
	uint32_t batch_samples;
	cycles_t min;
	cycles_t max;
	cycles_t tot;
};

struct resources_t {
	struct VL_sock_t	sock;
	struct hca_data_t	*hca_p;
	struct ibv_pd		*pd;
	struct ibv_cq		*cq;
	struct ibv_qp		*qp;
	struct ibv_qp_ex	*eqp;
	struct mr_data_t	*mr;
	struct ibv_recv_wr	*recv_wr_arr;
	struct ibv_sge		*sge_arr;
	struct ibv_data_buf	*data_buf_arr;
	struct ibv_send_wr	*send_wr_arr;
	struct ibv_wc		*wc_arr;
	struct measure_t	measure;
};

#endif /* GEN2_SRQ__TEST_TYPE_H */

