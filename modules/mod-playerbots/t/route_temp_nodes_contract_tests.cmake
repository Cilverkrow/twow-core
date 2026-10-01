# #452 (heap profile 01.10.2026, 40 bots): heap under TravelNodeMap::getRoute grew from
# 98 MB to 236 MB in one hour - discarded route candidates kept their portal nodes
# (hearthstone, mage teleports). Every discarded route frees its temporary nodes.
file(READ "${PB_SOURCE_DIR}/TravelNode.cpp" node)
string(REGEX MATCHALL "route\.cleanTempNodes\(\)" cleans "${node}")
list(LENGTH cleans clean_count)
if (clean_count LESS 5)
  message(FATAL_ERROR "discarded routes must free their temp nodes (found ${clean_count} cleanTempNodes calls)")
endif()
string(FIND "${node}" "            delete botNode;" bot_node)
if (bot_node EQUAL -1)
  message(FATAL_ERROR "the hearthstone bot node must be freed")
endif()
message(STATUS "ROUTE_TEMP_NODES_CONTRACT=PASS")
