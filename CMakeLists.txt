cmake_minimum_required(VERSION 2.8)
project(svar_nsq)

include(PICMake)
set(CMAKE_CXX_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(src)

pi_add_target(svar_nsq SHARED src REQUIRED System DEPENDENCY event)
pi_add_target(nsq_sample BIN sample REQUIRED System DEPENDENCY event)

pi_report_target()

# Now do make install
pi_install(TARGETS svar_nsq)
