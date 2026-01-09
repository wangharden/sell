# Intraday Strategy 盘中卖出策略

> 源码: [IntradaySellStrategy.h](../../../result/src/strategies/IntradaySellStrategy.h) / [IntradaySellStrategy.cpp](../../../result/src/strategies/IntradaySellStrategy.cpp)

## 流程概览

```
09:26:00 ─── collect_auction_data ───┐
11:28:10                             │ 收集开盘数据 (once)
                                     ▼
09:30:03 ─── execute_sell ───────────┐
11:30:00                             │ 上午交易
                                     │
13:00:00 ─── execute_sell ───────────┤
14:48:55                             │ 下午交易
                                     ▼
14:49:00 ─── cancel_orders ──────────┘
14:51:00                               撤未成交单
```

## 关键状态变量

| 变量 | 类型 | 生命周期 | 常见误用 |
|------|------|----------|----------|
| `sold_vol` | int64 (股) | StockParams 内累加 | 与 total_sell 混淆 |
| `avail_vol` | int64 (股) | 每次刷新 | 用旧值计算 keep_position |
| `hold_vol` | int64 (股) | 来自 config | — |
| `open_price` | double (元) | collect_auction_data 设置 | 未初始化时用 0 判断 |
| `jjamt` | double (元) | 集合竞价成交额 | 与 ask1_amt 混淆 |
| `before_check_` | int | 全局门控 | — |

## 核心逻辑

### collect_auction_data

- **窗口**: 09:26:00 - 11:28:10 (once)
- **输入**: auction_data API → open_price, jjamt
- **副作用**: 刷新 pre_close from snapshot

### execute_sell

- **窗口**: 09:30:03-11:30:00, 13:00:00-14:48:55
- **触发**: 每 3s

**条件分类** (determine_condition):

| 条件 | 判定 | 返回值 |
|------|------|--------|
| 连板 | second_flag = 1 | "lb" |
| 封板 | fb_flag=1, zb_flag=0 | "fb" |
| 回封 | fb_flag=1, zb_flag=1 | "hf" |
| 炸板 | fb_flag=0, zb_flag=1 | "zb" |

**时间窗口获取** (SellStrategy::get_windows):

基于 condition + jjamt + open_ratio 返回 (start, end, keep_position) 列表

**下单流程**:
1. 50% random skip
2. 检查 keep_position: `(avail_vol - hold_vol) / total_vol <= keep_position` 则跳过
3. 跳过涨停 (bid1 == zt_price)
4. price = floor((bid1 + ask1) / 2, 0.01)
5. 随机量控制: single_amt < bid1 * vol 时生效
6. 下单后 sold_vol += vol

### cancel_orders

- **窗口**: 14:49:00 - 14:51:00
- **限制**: 每日最多 3 次撤单
- **匹配**: remark 精确匹配
- **撤单条件**: SUBMITTED / PARTIAL 状态

## 常见 Bug 模式

| ID | 症状 | 根因 | 定位关键词 |
|----|------|------|------------|
| BUG-I01 | 窗口内不卖 | open_ratio 计算用 0（open_price 未初始化） | `open_price`, `open_ratio` |
| BUG-I02 | 保留仓位不准 | keep_position 用 total_vol 而非 avail_vol | `keep_position`, `avail_vol` |
| BUG-I03 | 中午不撤单 | cancel_attempts_ 跨日期未重置 | `cancel_attempt_date_` |
| BUG-I04 | 条件判断错 | fb_flag/zb_flag 组合理解错误 | `determine_condition` |

## 相关 Incidents

*(暂无)*

