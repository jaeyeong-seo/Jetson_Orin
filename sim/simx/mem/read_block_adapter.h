// Copyright © 2026
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.h"
#include "mem_block_pool.h"
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>

namespace vortex {

class ReadBlockAdapter : public SimObject<ReadBlockAdapter> {
public:
  SimChannel<MemReq> ReqIn;
  SimChannel<MemRsp> RspOut;
  SimChannel<MemReq> ReqOut;
  SimChannel<MemRsp> RspIn;

  ReadBlockAdapter(const SimContext& ctx, const char* name, uint32_t read_size,
                   uint32_t capacity)
    : SimObject<ReadBlockAdapter>(ctx, name)
    , ReqIn(this)
    , RspOut(this)
    , ReqOut(this)
    , RspIn(this)
    , read_size_(read_size)
    , parts_(read_size / VX_CFG_MEM_BLOCK_SIZE)
    , groups_(capacity) {
    if (!ispow2(read_size) || read_size < VX_CFG_MEM_BLOCK_SIZE
     || read_size > MEM_PAYLOAD_SIZE || parts_ >= 64 || capacity == 0
     || capacity > std::numeric_limits<uint32_t>::max() / parts_) {
      throw std::invalid_argument("invalid read-block adapter geometry");
    }
    this->on_reset();
  }

protected:
  void on_reset() {
    free_.clear();
    issuing_.clear();
    completed_.clear();
    fence_ = false;
    for (uint32_t i = 0; i < groups_.size(); ++i) {
      groups_.at(i) = Group{};
      free_.push_back(i);
    }
  }

  void on_tick() {
    if (!RspIn.empty()) {
      auto rsp = RspIn.peek();
      uint32_t slot = rsp.tag / parts_;
      uint32_t part = rsp.tag % parts_;
      auto& group = groups_.at(slot);
      if (!group.active || part >= group.count || (group.received & (uint64_t(1) << part))) {
        throw std::runtime_error("unexpected read-block response");
      }
      if (!group.req.flags.flush) {
        if (!rsp.data) {
          throw std::runtime_error("read-block response has no data");
        }
        std::memcpy(group.data->data() + part * VX_CFG_MEM_BLOCK_SIZE,
                    rsp.data->data(), VX_CFG_MEM_BLOCK_SIZE);
      }
      group.received |= uint64_t(1) << part;
      if (group.received == ((uint64_t(1) << group.count) - 1)) {
        completed_.push_back(slot);
      }
      RspIn.pop();
    }

    if (!completed_.empty() && !RspOut.full()) {
      uint32_t slot = completed_.front();
      auto& group = groups_.at(slot);
      MemRsp rsp{group.req.tag, group.req.hart_id, group.req.uuid, group.data};
      rsp.merged_tags = group.req.merged_tags;
      RspOut.send(rsp, 0);
      fence_ = fence_ && !group.req.flags.flush;
      group = Group{};
      completed_.pop_front();
      free_.push_back(slot);
    }

    if (!ReqIn.empty() && !free_.empty() && !fence_
     && (!ReqIn.peek().flags.flush || free_.size() == groups_.size())) {
      auto req = ReqIn.peek();
      if (!req.flags.flush && req.op != MemOp::LD) {
        throw std::runtime_error("read-block adapter received a non-read request");
      }
      uint32_t slot = free_.front();
      free_.pop_front();
      auto& group = groups_.at(slot);
      group.active = true;
      group.req = req;
      group.count = req.flags.flush ? 1 : parts_;
      group.data = req.flags.flush ? nullptr : make_mem_block();
      fence_ = req.flags.flush;
      issuing_.push_back(slot);
      ReqIn.pop();
    }

    if (!issuing_.empty() && !ReqOut.full()) {
      uint32_t slot = issuing_.front();
      auto& group = groups_.at(slot);
      MemReq req = group.req;
      if (!req.flags.flush) {
        req.addr = (req.addr & ~uint64_t(read_size_ - 1))
                 + group.sent * VX_CFG_MEM_BLOCK_SIZE;
      }
      req.tag = slot * parts_ + group.sent;
      ReqOut.send(req, 0);
      if (++group.sent == group.count) {
        issuing_.pop_front();
      }
    }

    if (free_.size() == groups_.size() && ReqIn.size() == 0 && RspIn.size() == 0) {
      this->tick_sleep();
    }
  }

private:
  struct Group {
    bool active = false;
    MemReq req;
    std::shared_ptr<mem_block_t> data;
    uint32_t sent = 0;
    uint32_t count = 0;
    uint64_t received = 0;
  };

  uint32_t read_size_;
  uint32_t parts_;
  std::vector<Group> groups_;
  std::deque<uint32_t> free_;
  std::deque<uint32_t> issuing_;
  std::deque<uint32_t> completed_;
  bool fence_ = false;

  friend class SimObject<ReadBlockAdapter>;
};

} // namespace vortex
