// Copyright © 2026
// SPDX-License-Identifier: Apache-2.0

#include "dram/impl/LPDDR5.cpp"

namespace Ramulator {

// The pinned device tests row zero instead of the requested row in RD/WR prerequisites.
class LPDDR5BankRows : public LPDDR5 {
public:
  LPDDR5BankRows(const YAML::Node& config, Implementation* parent)
    : LPDDR5(config, parent) {
    for (auto op : {std::pair{"RD16", "CASRD"}, std::pair{"WR16", "CASWR"}}) {
      int sync = m_commands(op.second);
      m_preqs[m_levels["bank"]][m_commands(op.first)] =
        [sync](Node* node, int cmd, const AddrVec_t& addr, Clk_t clk) {
          switch (node->m_state) {
          case m_states["Closed"]:
            return m_commands["ACT-1"];
          case m_states["Pre-Opened"]:
            return m_commands["ACT-2"];
          case m_states["Opened"]: {
            if (node->m_row_state.find(addr[m_levels["row"]]) == node->m_row_state.end()) {
              return m_commands["PRE"];
            }
            auto* rank = node->m_parent_node->m_parent_node;
            return rank->m_final_synced_cycle < clk ? sync : cmd;
          }
          default:
            throw std::runtime_error("invalid LPDDR5 bank state");
          }
        };
      // ACT-1 has not completed activation, so neither query can report an open row.
      m_rowhits[m_levels["bank"]][m_commands(op.first)] =
        [](Node* node, int cmd, int target_id, Clk_t clk) {
          return node->m_state != m_states["Pre-Opened"]
              && Lambdas::RowHit::Bank::RDWR<LPDDR5>(node, cmd, target_id, clk);
        };
      m_rowopens[m_levels["bank"]][m_commands(op.first)] =
        [](Node* node, int cmd, int target_id, Clk_t clk) {
          return node->m_state != m_states["Pre-Opened"]
              && Lambdas::RowOpen::Bank::RDWR<LPDDR5>(node, cmd, target_id, clk);
        };
    }
  }
};

static bool registered_lpddr5_bank_rows = Factory::register_implementation(
  IDRAM::get_name(), "LPDDR5BankRows", "LPDDR5 with requested-row prerequisites",
  [](const YAML::Node& config, Implementation* parent) -> Implementation* {
    return new LPDDR5BankRows(config, parent);
  });

} // namespace Ramulator
