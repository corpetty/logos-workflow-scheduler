cmake_minimum_required(VERSION 3.14)
project(WorkflowSchedulerPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosModule.cmake")
    include(cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found")
endif()

# Universal module: the impl class IS the API; the glue is generated from
# src/workflow_scheduler_impl.h.
logos_module(
    NAME workflow_scheduler
    SOURCES
        src/workflow_scheduler_impl.h
        src/workflow_scheduler_impl.cpp
        src/scheduler_runtime.h
        src/scheduler_runtime.cpp
        src/cron_parser.h
        src/cron_parser.cpp
        src/webhook_listener.h
        src/webhook_listener.cpp
        src/deployment_store.h
        src/deployment_store.cpp
    INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
