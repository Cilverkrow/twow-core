cmake_policy(VERSION 3.16)
# twow-repo#592 (PATH-02 phase 2, group A): the travel node graph under the shared m_nMapMtx.
#  no. 3: TravelNodeMap::getNodes reads m_map_nodes with find(). operator[] inserted an empty vector for a map without
#         nodes (OB-30: map 451) - a write into the hash table under the shared lock while other map threads read it.
#         Same answer (no nodes), no switch.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#592 graph: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#592 graph: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#592 graph: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#592 graph: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#592 graph: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("TravelNode.cpp" node_cpp)
read_source("TravelNode.h" node_h)

# no. 3: the read path of getRoute(position) never writes the per-map node table.
function_body("${node_cpp}" "std::vector<TravelNode*> TravelNodeMap::getNodes(WorldPosition pos, float range, uint32 transportEntry)" get_nodes)
forbid_text("${get_nodes}" "m_map_nodes[" "operator[] on m_map_nodes in the read path")
require_text("${get_nodes}" "auto const mapNodes = m_map_nodes.find(pos.getMapId());\n    if (mapNodes == m_map_nodes.end())\n        return retVec;" "unknown map: empty result without insert")
require_text("${get_nodes}" "for (auto& node : mapNodes->second)" "iterates the found nodes")
require_order("${get_nodes}" "m_map_nodes.find(" "std::sort(retVec.begin(), retVec.end()" "lookup before the unchanged sort")
# getRoute(position) reaches getNodes under the shared lock taken in getFullPath.
function_body("${node_cpp}" "TravelNodeRoute TravelNodeMap::getRoute(WorldPosition startPos, WorldPosition endPos," route)
require_text("${route}" "std::vector<TravelNode*> startNodes = getNodes(startPos, -1, transportEntry), endNodes = getNodes(endPos);" "getRoute reads through getNodes")
forbid_text("${route}" "m_map_nodes" "direct table access in getRoute")
# The table stays a plain unordered_map; writers (addNode/removeNode) are unchanged.
require_text("${node_h}" "std::unordered_map<uint32, std::vector<TravelNode*>> m_map_nodes;" "node table type")
require_text("${node_cpp}" "    m_map_nodes[pos.getMapId()].push_back(newNode);" "addNode writer unchanged")

message(STATUS "travel_node_graph source contract passed")
