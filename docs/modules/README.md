# Modules 模块文档

整合后的策略模块文档，每个文件覆盖完整策略流程。

## 文件列表

| 文件 | 策略 | 时间窗口 | 源码 |
|------|------|----------|------|
| [auction.md](auction.md) | 竞价卖出 | 09:20-09:30 | AuctionSellStrategy |
| [intraday.md](intraday.md) | 盘中卖出 | 09:30-14:49 | IntradaySellStrategy |
| [close.md](close.md) | 收盘卖出 | 14:53-15:00 | CloseSellStrategy |
| [base_cancel.md](base_cancel.md) | 底仓/排撤 | 09:10-15:00 | BaseCancelModule |

## 文档结构

每个模块文档包含：

1. **流程概览** - 时间线 + 函数调用顺序
2. **关键状态变量** - 变量、生命周期、常见误用
3. **Phase 详情** - 每个阶段的输入/输出/约束
4. **常见 Bug 模式** - 症状/根因/定位关键词
5. **相关 Incidents** - 链接到具体问题记录

## 使用方式

### Agent 检索流程

1. 用户描述 bug 症状
2. 从 `fields.json` 匹配变量名
3. 从模块文档定位相关 Phase
4. 查看 "常见 Bug 模式" 表格
5. 关联 Incidents 找历史案例

### 人工 Review

- 模块文档作为代码 Review 参照
- 字段字典确认单位和换算规则
- Incidents 积累团队知识库

