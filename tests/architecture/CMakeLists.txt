cmake_minimum_required(VERSION 3.16)
project(AuctionBotTests LANGUAGES CXX)
enable_testing()
set(CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TRAVEL_ROOT "${CORE_ROOT}/modules/mod-playerbots/src/playerbot")
set(AHBOT_ROOT "${CORE_ROOT}/modules/mod-playerbots/src/ahbot")
function(extract_native_fragment source start end output)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
  file(READ "${source}" body)
  string(FIND "${body}" "${start}" first)
  if(first LESS 0)
    message(FATAL_ERROR "Missing fragment start: ${output}")
  endif()
  string(SUBSTRING "${body}" ${first} -1 body)
  string(FIND "${body}" "${end}" length)
  if(length LESS 0)
    message(FATAL_ERROR "Missing fragment end: ${output}")
  endif()
  string(SUBSTRING "${body}" 0 ${length} fragment)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${output}" "${fragment}")
endfunction()
extract_native_fragment("${TRAVEL_ROOT}/PlayerbotAI.cpp"
  "bool ChatHandler::HandleAhBotCommand(char* args)" "float PlayerbotAI::GetRange" NativeAhBotChatDispatch.inc)
extract_native_fragment("${AHBOT_ROOT}/AhBot.cpp"
  "bool AhBot::HandleAhBotCommand(ChatHandler* handler, char const* args)" "uint32 AhBot::auctionIds" NativeAhBotModuleDispatch.inc)
extract_native_fragment("${AHBOT_ROOT}/AhBot.h"
  "namespace ahbot" "#define auctionbot" NativeMarketDeclarations.inc)
extract_native_fragment("${AHBOT_ROOT}/AhBot.cpp"
  "bool ReadNumbers(" "bool QueryIds(" NativeMarketParser.inc)
extract_native_fragment("${AHBOT_ROOT}/AhBot.cpp"
  "bool AhBot::IsBotOwner(" "void AhBot::Status(" NativeMarketWork.inc)
extract_native_fragment("${AHBOT_ROOT}/AhBot.cpp"
  "bool AhBot::HandleCommand(" "// End market command implementation." NativeMarketCommands.inc)
extract_native_fragment("${CORE_ROOT}/src/game/AuctionHouse/AuctionHouseMgr.cpp"
  "void AuctionHouseObject::ExpireAuction(" "void AuctionHouseObject::Update()" NativeMarketExpiry.inc)
extract_native_fragment("${CORE_ROOT}/src/game/AuctionHouse/AuctionHouseMgr.cpp"
  "void AuctionHouseMgr::SendAuctionOutbiddedMail(" "void AuctionHouseObject::BuildListBidderItems(" NativeMarketRefund.inc)
extract_native_fragment("${CORE_ROOT}/src/game/AuctionHouse/AuctionHouseMgr.cpp"
  "std::vector<AuctionSnapshot> AuctionHouseObject::GetAuctionsSnapshotPage(" "AuctionHouseMgr::AuctionHouseMgr()" NativeMarketPage.inc)
foreach(test AhBotChatDispatchTest AhBotMarketTest AuctionSettlementTest)
  add_executable(${test} ${test}.cpp)
  target_compile_features(${test} PRIVATE cxx_std_17)
  target_include_directories(${test} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${AHBOT_ROOT}" "${CORE_ROOT}/src/shared")
  if(MSVC)
    target_compile_options(${test} PRIVATE /EHsc)
  endif()
  add_test(NAME ${test} COMMAND ${test})
  set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endforeach()
