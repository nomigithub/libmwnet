set(util_SRCS
	MWEventLoop.cc
	MWThread.cc
	MWThreadPool.cc
	MWSafeLock.cc
	MWAtomicInt.cc
	MWLogger.cc
	MWTimestamp.cc
	MWCountDownLatch.cc
	MWStringUtil.cc
  )

if(NOT CMAKE_BUILD_DYNAMIC_LIB)
	add_library(mwnet_mt_util ${util_SRCS})
else()
	add_library(mwnet_mt_util SHARED ${util_SRCS})
endif()

target_link_libraries(mwnet_mt_util mwnet_mt_netbase mwnet_mt_base mwnet_mt_crypto)

install(TARGETS mwnet_mt_util DESTINATION lib)

set(HEADERS
	MWEventLoop.h
	MWThread.h
	MWThreadPool.h
	MWSafeLock.h
	MWAtomicInt.h
	MWLogger.h
	MWTimestamp.h
	MWCountDownLatch.h
	MWStringUtil.h
  )
install(FILES ${HEADERS} DESTINATION include/mwnet_mt/util)

add_executable(util_test tests/util_test.cpp)
target_link_libraries(util_test mwnet_mt_util)

add_executable(logger_test tests/logger_test.cpp)
target_link_libraries(logger_test mwnet_mt_util)

add_executable(timestamp_test tests/timestamp_test.cpp)
target_link_libraries(timestamp_test mwnet_mt_util) 

add_executable(countdownlatch_test tests/countdownlatch_test.cpp)
target_link_libraries(countdownlatch_test mwnet_mt_util) 