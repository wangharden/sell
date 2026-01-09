# Close Strategy 收盘卖出策略

> 源码: [CloseSellStrategy.h](../../../result/src/strategies/CloseSellStrategy.h) / [CloseSellStrategy.cpp](../../../result/src/strategies/CloseSellStrategy.cpp)

## 流程概览

```
14:53:00 ─── phase1_random_sell ───┐
14:56:45                           │ 随机卖 70%
                                   ▼
14:56:45 ─── phase2_cancel_orders ─┐
14:57:00                           │ 撤单
                                   ▼
14:57:20 ─── phase3_test_sell ─────┐
14:57:50                           │ 测试卖 100 股
                                   ▼
14:58:00 ─── phase4_bulk_sell ─────┘
14:59:50                             清仓
```

## 关键状态变量

| 变量 | 类型 | 生命周期 | 常见误用 |
|------|------|----------|----------|
| `sold_volumes_` | map<symbol, int64> | 策略级 | 与 StockParams.sold_vol 独立，易混淆 |
| `total_volumes_` | map<symbol, int64> | 策略级 | 初始化时快照，后续不更新 |
| `order_ids_` | map<symbol, vector<string>> | 策略级 | Phase2 撤单依赖 |
| `callbacks_` | map<symbol, int> | 策略级 | Phase2 门控 |
| `phase2_cancel_done_` | int | 全局 | 保证只跑一次 |
| `phase3_test_sell_done_` | int | 全局 | — |
| `phase4_bulk_sell_done_` | int | 全局 | — |

## Phase 详情

### Phase 1: phase1_random_sell

- **窗口**: 14:53:00 - 14:56:45
- **触发**: 每 3s，15% 概率
- **70% 限制**: skip if sold_vol > total_vol * 0.7
- **价格**: floor((bid1 + ask1) / 2, 0.01)
- **跳过条件**: 涨停 (bid1 == zt_price)

### Phase 2: phase2_cancel_orders

- **窗口**: 14:56:45 - 14:57:00
- **策略**: 先按 order_id 撤，再按 remark 撤
- **门控**: phase2_cancel_done_ + callbacks_ per-symbol

### Phase 3: phase3_test_sell

- **窗口**: 14:57:20 - 14:57:50
- **逻辑**: 固定卖 100 股 @ 跌停价
- **前提**: available >= 100 && remaining >= 100
- **目的**: 探测流动性

### Phase 4: phase4_bulk_sell

- **窗口**: 14:58:00 - 14:59:50
- **逻辑**: 全部可售 @ 跌停价
- **公式**: `sellable = min(available, total) - hold_vol`
- **注意**: 无 100 股取整（可能需修正）

## 常见 Bug 模式

| ID | 症状 | 根因 | 定位关键词 |
|----|------|------|------------|
| BUG-C01 | 重复撤单 | callbacks_ 未正确更新 | `callbacks_`, `phase2` |
| BUG-C02 | 70% 限制失效 | sold_volumes_ 与持仓不同步 | `sold_volumes_`, `0.7` |
| BUG-C03 | Phase4 非整手 | sellable 未 round down | `phase4`, `/100*100` |
| BUG-C04 | 测试单重复 | phase3_test_sell_done_ 门控失效 | `phase3_test_sell_done_` |

## 相关 Incidents

*(暂无)*

