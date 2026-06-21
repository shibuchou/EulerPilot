# docs

作用：项目方案、计划、实验结论、交付报告和答辩材料的主目录。

## 当前核心入口

- `contest_briefing_reference.md`：比赛宣讲整理和最终完成度参考。
- `next_phase_plan_v2_1.md`：当前下一阶段执行口径。
- `skills_yaml_plan.md`：Skill/YAML 控制面规划。
- `progress_status.md`：当前阶段进度状态看板。
- `public_control_plane_design.md`：TargetResolver、AuditBus、ActionJournal、CapabilityDetector 公共控制面设计。
- `network_policy_skill.md`：NetworkPolicySkill 阶段 B 设计、YAML、hook 作用域、审计与回滚口径。
- `network_pod_veth_target.md`：Network Pod/veth target 解析预备能力、reason code 和安全边界。
- `security_policy_skill.md`：SecurityPolicySkill 正式化设计、复用边界、最小验证入口和下一步清单。
- `design_proposal.md`：总体设计方案。
- `final_report_submission.md`：已有阶段最终报告草稿。

## 维护规则

- 阶段计划变更必须明确标注历史版本和当前执行版本。
- 每个阶段完成后必须更新 `progress_status.md`。
- 新增正式 Skill 时必须补独立设计文档，例如：
  - `network_policy_skill.md`
  - `security_policy_skill.md`
  - `resource_control_skill.md`
  - `cross_agent_policy.md`
- 文档不得把未完成能力写成已完成；必须区分规划、进行中、已验证。
