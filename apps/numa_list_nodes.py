#!/usr/bin/env python3

import numa

num_nodes=numa.get_max_node() + 1

for cur_node in range(0,num_nodes):
    cpu_to_node=list(numa.node_to_cpus(cur_node))
    print("Node " + str(cur_node) + " has " + str(len(cpu_to_node)) + " cores: " + str(cpu_to_node))
