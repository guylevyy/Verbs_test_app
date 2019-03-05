Test name:
    post_send_test

Author:
       Guy Levi

Short description:
        Run one way traffic using new post API and measure the SW post flow.
        This also measure the old psot send API.

Dependencies:
        verification tools (/mswg/projects/ver_tools/reg2_latest/install.sh)
        rdma-core installation
    

Supported OSes:
        Linux

Examples:
        On server: ./post_send_test -d mlx5_2 --daemon -i 8 -b 1 --num_sge=1 -o SEND -t DC
        On client: ./post_send_test -d mlx5_2 --ip=10.134.203.1 -i 8 -b 1 --num_sge=1 -m NEW -o SEND -t DC 
