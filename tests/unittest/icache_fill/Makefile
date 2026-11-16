ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := icache_fill
SRC_DIR := $(VORTEX_HOME)/tests/unittest/$(PROJECT)
SRCS := $(SRC_DIR)/main.cpp $(SW_COMMON_DIR)/util.cpp
CXXFLAGS += -I$(VORTEX_HOME)/sim/simx -I$(SIM_COMMON_DIR) -I$(VORTEX_HOME)/sim/simx/mem
CXXFLAGS += -DVX_CFG_XLEN=$(XLEN) -DVX_CFG_XLEN_$(XLEN)
LDFLAGS += -pthread

include ../common.mk
CXXFLAGS := $(filter-out -pedantic,$(CXXFLAGS))
