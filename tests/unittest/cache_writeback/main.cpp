// Copyright © 2026
// SPDX-License-Identifier: Apache-2.0

#include "cache.h"
#include "mem_block_pool.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace vortex;

static void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << std::endl;
    std::exit(1);
  }
}

class Harness {
public:
  Harness(uint32_t sectors, Cache::ReplPolicy policy)
      : sectors_(sectors)
      , memory_(3 * sectors * VX_CFG_MEM_BLOCK_SIZE, 0)
      , writes_(3 * sectors, 0) {
    Cache::Config config{};
    config.size = 2 * sectors * VX_CFG_MEM_BLOCK_SIZE;
    config.L = log2ceil(sectors * VX_CFG_MEM_BLOCK_SIZE);
    config.S = log2ceil(VX_CFG_MEM_BLOCK_SIZE);
    config.W = 2;
    config.num_ways = 2;
    config.addr_width = 32;
    config.num_inputs = 1;
    config.mem_ports = 1;
    config.write_back = true;
    config.write_reponse = true;
    config.mshr_size = 8;
    config.latency = 2;
    config.repl_policy = policy;
    config.is_llc = true;
    config.lazy_fetch = true;
    cache_ = Cache::Create("writeback-test", config);
    SimPlatform::instance().reset();
  }

  void tick(uint32_t count = 1) {
    for (uint32_t n = 0; n < count; ++n) {
      SimPlatform::instance().tick();
      // A slow memory consumer keeps the bounded egress under backpressure.
      if (SimPlatform::instance().cycles() % 7 != 0 || cache_->mem_req_out.at(0).empty()) {
        continue;
      }
      auto req = cache_->mem_req_out.at(0).peek();
      cache_->mem_req_out.at(0).pop();
      check(req.addr + VX_CFG_MEM_BLOCK_SIZE <= memory_.size(), "memory request address");
      if (req.is_write()) {
        ++writes_.at(req.addr / VX_CFG_MEM_BLOCK_SIZE);
        for (uint32_t i = 0; i < VX_CFG_MEM_BLOCK_SIZE; ++i) {
          if ((req.byteen >> i) & 1) {
            memory_.at(req.addr + i) = req.data->at(i);
          }
        }
      } else {
        auto data = make_mem_block();
        std::copy_n(memory_.begin() + req.addr, VX_CFG_MEM_BLOCK_SIZE, data->begin());
        cache_->mem_rsp_in.at(0).send(MemRsp(req.tag, req.hart_id, req.uuid, data), 3);
      }
    }
  }

  MemRsp request(MemReq req) {
    req.tag = ++tag_;
    cache_->core_req_in.at(0).send(req);
    for (uint32_t i = 0; i < 1000; ++i) {
      tick();
      if (!cache_->core_rsp_out.at(0).empty()) {
        auto rsp = cache_->core_rsp_out.at(0).peek();
        cache_->core_rsp_out.at(0).pop();
        check(rsp.tag == req.tag, "response tag");
        return rsp;
      }
    }
    check(false, "core response timeout");
    return {};
  }

  void run(Cache::ReplPolicy policy) {
    for (uint32_t sector = 0; sector < 2 * sectors_; ++sector) {
      auto data = make_mem_block();
      data->fill(uint8_t(0x20 + sector));
      uint64_t mask = (sector & 1) ? 0x55555555u : 0xffffffffu;
      request(MemReq(MemOp::ST, sector * VX_CFG_MEM_BLOCK_SIZE, data, mask));
    }
    auto start = SimPlatform::instance().cycles();
    request(MemReq(MemOp::LD, 2 * sectors_ * VX_CFG_MEM_BLOCK_SIZE));
    // A fill may forward its read before victim replacement finishes.
    tick(128);
    for (uint32_t sector = 0; sector < sectors_; ++sector) {
      check(writes_.at(sector) == 1, "dirty victim must write each sector exactly once");
      uint64_t mask = (sector & 1) ? 0x55555555u : 0xffffffffu;
      for (uint32_t i = 0; i < VX_CFG_MEM_BLOCK_SIZE; ++i) {
        auto expected = ((mask >> i) & 1) ? uint8_t(0x20 + sector) : 0;
        check(memory_.at(sector * VX_CFG_MEM_BLOCK_SIZE + i) == expected, "writeback data and byte mask");
      }
      check(writes_.at(sectors_ + sector) == 0, "replacement victim must remain fixed");
    }
    request(MemReq(MemOp::LD, 2 * sectors_ * VX_CFG_MEM_BLOCK_SIZE));
    auto rsp = request(MemReq(MemOp::LD, 0));
    check(rsp.data && rsp.data->at(0) == 0x20, "evicted data reload");
    std::cout << "PASSED: sectors=" << sectors_ << " policy=" << unsigned(policy)
              << " cycles=" << SimPlatform::instance().cycles() - start << std::endl;
  }

private:
  uint32_t sectors_;
  Cache::Ptr cache_;
  std::vector<uint8_t> memory_;
  std::vector<uint32_t> writes_;
  uint32_t tag_ = 0;
};

int main() {
  for (uint32_t sectors : {1u, 2u, 4u}) {
    for (auto policy : {Cache::FIFO, Cache::RANDOM, Cache::PLRU}) {
      SimPlatform::instance().initialize();
      Harness harness(sectors, policy);
      harness.run(policy);
      SimPlatform::instance().finalize();
    }
  }
  return 0;
}
