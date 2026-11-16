// Copyright © 2026
// SPDX-License-Identifier: Apache-2.0

#include "read_block_adapter.h"
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

static void tick(uint32_t cycles = 1) {
  for (uint32_t i = 0; i < cycles; ++i) {
    SimPlatform::instance().tick();
  }
}

static MemReq request(ReadBlockAdapter& adapter) {
  for (uint32_t i = 0; i < 100; ++i) {
    tick();
    if (!adapter.ReqOut.empty()) {
      MemReq req = adapter.ReqOut.peek();
      adapter.ReqOut.pop();
      return req;
    }
  }
  check(false, "request timeout");
  return {};
}

static MemRsp response(ReadBlockAdapter& adapter) {
  for (uint32_t i = 0; i < 100; ++i) {
    tick();
    if (!adapter.RspOut.empty()) {
      MemRsp rsp = adapter.RspOut.peek();
      adapter.RspOut.pop();
      return rsp;
    }
  }
  check(false, "response timeout");
  return {};
}

static void reply(ReadBlockAdapter& adapter, const MemReq& req, uint8_t value,
                  uint32_t delay) {
  auto data = make_mem_block();
  std::fill(data->begin(), data->end(), value);
  adapter.RspIn.send(MemRsp(req.tag, req.hart_id, req.uuid, data), delay);
}

int main() {
  SimPlatform::instance().initialize();
  constexpr uint32_t read_size = MEM_PAYLOAD_SIZE;
  constexpr uint32_t parts = read_size / VX_CFG_MEM_BLOCK_SIZE;
  auto adapter = ReadBlockAdapter::Create("ifill-test", read_size, 2);
  SimPlatform::instance().reset();

  // Two outstanding fills reuse the same lower-address offsets but own tags.
  adapter->ReqIn.send(MemReq(MemOp::LD, 4 * read_size, nullptr, 0, 91, 3, 7));
  adapter->ReqIn.send(MemReq(MemOp::LD, 5 * read_size, nullptr, 0, 17, 4, 8));
  std::vector<MemReq> a, b;
  for (uint32_t i = 0; i < parts; ++i) {
    a.push_back(request(*adapter));
    check(a.back().addr == 4 * read_size + i * VX_CFG_MEM_BLOCK_SIZE, "first fill address");
  }
  for (uint32_t i = 0; i < parts; ++i) {
    b.push_back(request(*adapter));
    check(b.back().addr == 5 * read_size + i * VX_CFG_MEM_BLOCK_SIZE, "second fill address");
  }
  check(a.front().tag != b.front().tag, "outstanding fills must have distinct tags");

  // Delayed responses must retain the fill until every part arrives.
  tick(8);
  for (uint32_t i = parts; i != 0; --i) {
    reply(*adapter, b.at(i - 1), uint8_t(0x80 + i - 1), 6 + parts - i);
  }
  auto brsp = response(*adapter);
  check(brsp.tag == 17 && brsp.hart_id == 4 && brsp.uuid == 8, "out-of-order fill identity");
  for (uint32_t i = 0; i < read_size; ++i) {
    check(brsp.data->at(i) == uint8_t(0x80 + i / VX_CFG_MEM_BLOCK_SIZE), "second fill data");
  }
  check(adapter->RspOut.empty(), "incomplete first fill must not respond");

  // A fence waits until older reads complete.
  MemReq fence;
  fence.tag = 123;
  fence.flags.flush = true;
  adapter->ReqIn.send(fence);
  for (uint32_t i = parts; i != 0; --i) {
    reply(*adapter, a.at(i - 1), uint8_t(0x20 + i - 1), 5 + parts - i);
  }
  auto arsp = response(*adapter);
  check(arsp.tag == 91 && arsp.hart_id == 3 && arsp.uuid == 7, "first fill identity");
  for (uint32_t i = 0; i < read_size; ++i) {
    check(arsp.data->at(i) == uint8_t(0x20 + i / VX_CFG_MEM_BLOCK_SIZE), "first fill data");
  }
  auto token = request(*adapter);
  check(token.flags.flush, "fence must follow older reads");
  adapter->RspIn.send(MemRsp(token.tag), 5);
  check(response(*adapter).tag == 123, "fence response identity");

  SimPlatform::instance().reset();
  adapter->ReqIn.send(MemReq(MemOp::LD, 9 * read_size, nullptr, 0, 77));
  for (uint32_t i = 0; i < parts; ++i) {
    auto req = request(*adapter);
    reply(*adapter, req, 0x55, 3);
  }
  check(response(*adapter).tag == 77, "slot reuse after reset");

  adapter->ReqIn.send(MemReq(MemOp::LD, 10 * read_size, nullptr, 0, 88));
  fence.tag = 124;
  adapter->ReqIn.send(fence);
  for (uint32_t i = 0; i < parts; ++i) {
    auto req = request(*adapter);
    check(!req.flags.flush, "queued fence must not stall unsent read fragments");
    reply(*adapter, req, 0x66, 6);
  }
  check(response(*adapter).tag == 88, "read ahead of queued fence");
  token = request(*adapter);
  check(token.flags.flush, "queued fence forwarded once");
  adapter->RspIn.send(MemRsp(token.tag), 4);
  check(response(*adapter).tag == 124, "queued fence completion");
  std::cout << "PASSED: instruction fills (" << parts << " transfers/fill)" << std::endl;
  SimPlatform::instance().finalize();
  return 0;
}
