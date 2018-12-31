#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vl.h>
#include "types.h"
#include "resources.h"
#include "test.h"

struct config_t config = {
	.hca_type = "mlx5_0",
	.ip = "127.0.0.1",
	.num_of_iter = 8,
	.is_daemon = 0,
	.wait = 0,
	.tcp = 17500,
	.qp_type = IBV_QPT_RC,
	.msg_sz = 8,
	.ring_depth = DEF_RING_DEPTH,
	.batch_size = DEF_BATCH_SIZE,
	.new_api = 0,
};

struct VL_usage_descriptor_t usage_descriptor[] = {
	{
		'h', "help", "",
		"Print this message and exit",
#define HELP_CMD_CASE				0
		HELP_CMD_CASE
	},

	{
		'i', "iteration", "ITERATION",
		"The number of iteration for this test - Default 8",
#define NUM_OF_ITER_CMD_CASE			6
		NUM_OF_ITER_CMD_CASE
	},

	{
		'd', "device", "HOST_ID",
		"HCA to use - Default mlx5_0",
#define HOST_CMD_CASE				8
		HOST_CMD_CASE
	},

	{
		'w', "wait", "",
		"Wait before exit",
#define WAIT_CMD_CASE				9
		WAIT_CMD_CASE
	},

	{
		' ', "ip", "IP_ADDR",
		"The ip of the srq_test server. Default value: current machine.",
#define IP_CMD_CASE				10
		IP_CMD_CASE
	},

	{
		' ', "daemon", "",
		"Run as a server.",
#define DAEMON_CMD_CASE				12
		DAEMON_CMD_CASE
	},

	{
		' ', "new_api", "",
		"Use new post send API",
#define NEW_API_CMD_CASE			13
		NEW_API_CMD_CASE
	},

	{
		' ', "tcp", "TCP",
		"TCP port to use",
#define TCP_CMD_CASE				14
		TCP_CMD_CASE
	},

	{
		'b', "batch", "BATCH",
		"WRs list size to post",
#define BATCH_CMD_CASE				15
		BATCH_CMD_CASE
	},

	{
		't', "qp_type", "QP_TYPE",
		"Enforce QPs type (Default: RC)",
#define QP_TYPE_CMD_CASE			16
		QP_TYPE_CMD_CASE
	}

};

/***********************************
* Function: bool_to_str.
************************************/
/* 0 = FALSE. */
const char *bool_to_str(
	IN		int var)
{
	if (var)
		return "YES";

	return "NO";
}

/***********************************
* Function: print_config.
************************************/
static void print_config(void)
{
	VL_MISC_TRACE((" ---------------------- config data  ---------------"));

	VL_MISC_TRACE((" Test side                      : %s", ((config.is_daemon) ? "Server" : "Client")));
	if (!config.is_daemon)
		VL_MISC_TRACE((" IP                             : %s", config.ip));
	VL_MISC_TRACE((" TCPort                         : %d", config.tcp));
	VL_MISC_TRACE((" HCA                            : %s", config.hca_type));
	VL_MISC_TRACE((" Number of iterations           : %d", config.num_of_iter));
	VL_MISC_TRACE((" QP Type                        : %s", (VL_ibv_qp_type_str(config.qp_type))));
	VL_MISC_TRACE((" Ring-depth                     : %u", config.ring_depth));
	VL_MISC_TRACE((" Batch size                     : %u", config.batch_size));
	VL_MISC_TRACE((" Use new post API:              : %s", config.new_api ? "Yes" : "No"));
	VL_MISC_TRACE((" Wait before exit               : %s", bool_to_str(config.wait)));

	VL_MISC_TRACE((" --------------------------------------------------"));
}

/***********************************
* Function: process_arg.
************************************/
static int process_arg(
	IN		int opt_index,
	IN		char *equ_ptr,
	IN		int arr_size,
	IN		const struct VL_usage_descriptor_t *usage_desc_arr)
{
	/* process argument */

	switch (usage_descriptor[opt_index].case_code) {
	case HELP_CMD_CASE:
		VL_usage(1, arr_size, usage_desc_arr);
		exit(1);

	case NUM_OF_ITER_CMD_CASE:
		config.num_of_iter = strtoul(equ_ptr, NULL, 0);
		break;

	case HOST_CMD_CASE:
		config.hca_type = equ_ptr;
		break;

	case WAIT_CMD_CASE:
		config.wait = 1;
		break;

	case DAEMON_CMD_CASE:
		config.is_daemon = 1;
		break;

	case IP_CMD_CASE:
		strcpy(config.ip, equ_ptr);
		break;

	case TCP_CMD_CASE:
		config.tcp = strtoul(equ_ptr, NULL, 0);
		break;

	case BATCH_CMD_CASE:
		config.batch_size = strtoul(equ_ptr, NULL, 0);
		break;

	case NEW_API_CMD_CASE:
		config.new_api = 1;
		break;

	case QP_TYPE_CMD_CASE:
		if (!strcmp("RC",equ_ptr))
			config.qp_type = IBV_QPT_RC;
		else if (!strcmp("UD",equ_ptr))
                        config.qp_type = IBV_QPT_UD;
		else {
			VL_MISC_ERR(("Unsupported QP Transport Service Type %s\n", equ_ptr));
			exit(1);
		}
                break;

	default:
		VL_MISC_ERR(("unknown parameter is the switch %s\n", equ_ptr));
		exit(4);
	}

	return 0;
}



/***********************************
* Function: parse_params.
************************************/
static int parse_params(
	IN		int argc,
	IN		char **argv)
{
	int rc;

	if (argc == 1) {
		VL_MISC_ERR((" Sorry , you must enter some data."
			     " type -h for help. "));
		exit(1);
	}

	rc = VL_parse_argv(argc, argv,
			   (sizeof(usage_descriptor)/sizeof(struct VL_usage_descriptor_t)),
			   (const struct VL_usage_descriptor_t *)(usage_descriptor),
			   (const VL_process_arg_func_t)process_arg);
	return rc;
}

/***********************************
* Function: main.
************************************/
int main(
	IN		int argc,
	IN		char *argv[])
{
	struct resources_t resource = {
		.sock = {
			.ip = "127.0.0.1",
			.port = 15000
		}
	};
	int rc = SUCCESS;

	rc = parse_params(argc, argv);
	CHECK_RC(rc, "parse_params");

	strcpy(resource.sock.ip, config.ip);

	rc = force_configurations_dependencies();
	CHECK_RC(rc, "force_configurations_dependencies");

	print_config();

	rc = resource_alloc(&resource);
	CHECK_RC(rc, "resource_alloc");

	rc = resource_init(&resource);
	CHECK_RC(rc, "resource_init");

	rc = do_test(&resource);
	CHECK_RC(rc, "do_test");

	if (!config.is_daemon) {
		rc = print_results(&resource);
		CHECK_RC(rc, "print_results");
	}

cleanup:
	if (config.wait)
		VL_keypress_wait();

	if (resource_destroy(&resource) != SUCCESS)
		rc = FAIL;

	VL_print_test_status(rc);

	return rc;
}

