# Auction Strategy 竞价卖出策略

> 源码: [AuctionSellStrategy.h](../../../result/src/strategies/AuctionSellStrategy.h) / [AuctionSellStrategy.cpp](../../../result/src/strategies/AuctionSellStrategy.cpp)

## 流程概览

```
09:20:05 ─── check_market_data ───┐
09:23:00                          │ 行情校验
                                  ▼
09:23:30 ─── phase1_return1_sell ─┐
09:25:00                          │ 无条件卖10%
                                  ▼
09:23:40 ─── phase2_conditional ──┐
09:24:45                          │ 条件卖出
                                  ▼
09:24:50 ─── phase3_final_sell ───┐
09:25:00                          │ 最后冲刺
                                  ▼
09:25:13 ─── cancel_auction_orders┐
09:25:23                          │ 撤未成交单
                                  ▼
09:26:00 ─── collect_auction_data ┐
09:28:10                          │ 收集开盘数据
                                  ▼
09:29:55 ─── after_open_sell ─────┘
09:30:40                            开盘后继续
```

## 关键状态变量

| 变量 | 类型 | 生命周期 | 常见误用 |
|------|------|----------|----------|
| `total_sell` | int64 (股) | 跨 phase 累加 | Phase3 重置前未清零；与 sold_vol 混淆 |
| `avail_vol` | int64 (股) | 每次刷新 | 用了旧快照值 |
| `hold_vol` | int64 (股) | 来自 config | 和 available 概念混淆 |
| `sell_flag` | int | per-symbol | collect_auction_data 重置后未注意 |
| `return1_sell` | int | per-symbol | Phase1 只执行一次的门控 |
| `limit_sell` | int | per-symbol | Phase3 半仓卖出只执行一次 |

## Phase 详情

### Phase 0: check_market_data

- **窗口**: 09:20:05 - 09:23:00 (once)
- **输入**: Snapshot
- **逻辑**: 更新 pre_close / zt_price / dt_price
- **门控**: hangqin_check_ 保证只跑一次

### Phase 1: phase1_return1_sell

- **窗口**: 09:23:30 - 09:25:00
- **逻辑**: 卖出 10% 可售量，挂跌停价
- **公式**: `sellable = min(available - hold_vol, total - hold_vol)`
- **跳过条件**: 涨停封板 (bid1 == zt_price && ask2_vol <= 0)
- **输出**: total_sell += vol; return1_sell = 1

### Phase 2: phase2_conditional_sell

- **窗口**: 09:23:40 - 09:24:45
- **触发**: 每 0.5s，12.5% 概率
- **条件卖出逻辑**:

| 条件 | 判定 | 挂单价 |
|------|------|--------|
| 连板 (second_flag) | bid1 >= pre_close * 1.07 | pre_close * 1.07 |
| 封板 (fb_flag only) | ask1_amt < 1500万 && bid1 >= pre_close * 1.015 | pre_close * 1.015 |
| 炸板 (zb_flag only) | ask1_amt < 300万 && bid1 >= pre_close * 1.01 | pre_close * 1.01 |

- **比例限制**: skip if total_sell >= ask1_vol * sell_to_mkt_ratio
- **跳过条件**: 涨停封板

### Phase 3: phase3_final_sell

- **窗口**: 09:24:50 - 09:25:00
- **特殊分支**: 涨停未封板时，卖一半 @ zt_price - 0.01，标记 limit_sell
- **比例限制**: 既做 skip 也做 cap (与 Phase2 不同)
- **公式**: `cap_vol = (ask1_vol * ratio - total_sell) / 100 * 100`

### cancel_auction_orders

- **窗口**: 09:25:13 - 09:25:23
- **匹配**: remark 前缀匹配 或 userOrderId 精确匹配
- **撤单条件**: 非 FILLED 状态

### collect_auction_data

- **窗口**: 09:26:00 - 09:28:10 (once)
- **输出**: open_price, jjamt 更新; sell_flag 重置为 0

### after_open_sell

- **窗口**: 09:29:55 - 09:30:40
- **触发**: 每 3s
- **条件**: fb_flag / zb_flag 分别检查 open_price 和 jjamt
- **注意**: 不更新 sell_flag，可重复下单

## 常见 Bug 模式

| ID | 症状 | 根因 | 定位关键词 |
|----|------|------|------------|
| BUG-A01 | 卖出超限 | total_sell 与 ask1_vol 单位不一致 (股 vs 手) | `total_sell`, `ask_volume` |
| BUG-A02 | 非整手委托 | Phase3 cap 未 round down 到 100 股 | `phase3`, `/100*100` |
| BUG-A03 | ST 股触发异常 | pre_close = zt_price / 1.1 硬编码 | `1.1`, `pre_close` |
| BUG-A04 | 重复下单 | sell_flag 在 collect_auction_data 被重置 | `sell_flag = 0` |

## 相关 Incidents

- [INC-20260106-01](../incidents/INC-20260106-01.md): ratio cap 单位混用

