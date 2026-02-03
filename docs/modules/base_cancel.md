# BaseCancel Module 底仓/排撤模块

> 源码: [BaseCancelModule.h](../../../result/src/modules/BaseCancelModule.h) / [BaseCancelModule.cpp](../../../result/src/modules/BaseCancelModule.cpp)

该模块承担三类职责：
1) **底仓补齐**（基于 buy-list CSV）；2) **盘前两笔涨停卖单 + 盘中排撤第二单**；3) **收盘清理不在名单中的持仓**（把 CloseSellStrategy 留下的 300 股也清掉）。

## 流程概览

```
启动/init ─── load_buy_list_symbols ───────────────┐ 读取 buy list（默认 ./data/base_cancel）
                                                   ▼
09:10:20 ─── do_pre_orders ────────────────────────┐ 盘前委托（涨停价卖 100）
09:17:00                                           │
                                                   ▼
09:24:20 ─── do_second_orders ─────────────────────┐ 第二单（涨停价卖 100，记录 order_id）
09:24:50                                           │
                                                   ▼
09:29:00 ─── do_cancel (loop) ─────────────────────┘ 盘中撤单（外部单触发 second_ready_）
14:55:00

14:54:00 ─── do_base_buy ──────────────────────────┐ 底仓补齐（把名单股补到 hold_vol）
14:55:00                                           │
                                                   ▼
14:59:50 ─── do_sell_non_list_positions ───────────┘ 清仓不在名单中的持仓（跌停价卖出）
14:59:57
```

## 输入文件（buy list）

- **目录**: `./data/base_cancel`（可通过 config `base_cancel.order_dir` 覆盖）
- **选择规则**: 选目录下**修改时间最新**的 `*.csv`，优先包含 `_list` 的文件名
- **CSV 内容**: 任意列中出现 6 位股票代码（或 `600000.SH` 形式）即可识别

常见问题：
- 程序工作目录不一致导致 `./data/base_cancel` 为空（Linux/Windows 路径差异）
- 文件命名不符合日期前缀时，旧实现可能找不到；当前实现按**修改时间**选取

## 关键状态变量

| 变量 | 类型 | 生命周期 | 说明 |
|------|------|----------|------|
| `buy_symbols_` | vector<string> | init | buy list 解析出的 symbol 集合 |
| `holding_symbols_` | vector<string> | init | 启动时持仓 symbol 集合（后续不自动刷新） |
| `second_order_ids_` | set<string> | 09:24 后 | 第二单 order_id 集合（用于区分本地单） |
| `second_order_by_symbol_` | map<symbol, order_id> | 09:24 后 | symbol → 第二单 order_id |
| `second_ready_` | set<order_id> | 盘中 | 外部单触发后待撤单的第二单 |
| `second_canceled_` | set<order_id> | 盘中 | 已撤的第二单 |
| `zt_cache_` | map<symbol, double> | 当天 | 涨停价缓存 |
| `sell_non_list_done_` | bool | 当天 | 收盘清仓只执行一次 |

## 外部订单触发（排撤）

触发条件要点（`on_order_event`）：
- 只处理 `notify_type == NOTIFY_PUSH_ORDER`（ITPDK nType=8 的委托推送）
- 只接受 **外部单**：`result.is_local == false`
- 外部单类型约束：卖出 + 限价 + 100 股 + 价格为涨停价
- 命中后：将对应 symbol 的第二单 `order_id` 放入 `second_ready_`，由 `do_cancel()` 在盘中窗口撤单

常见坑：
- `notify_type` 常被误写成 1/2（其实是 connect/disconnect），导致收不到委托/成交推送
- SDK 回调路由如果仅靠 `nStructToken`，可能丢外部单；需用 account/实例兜底（详见 SecTradingApi 修复）

## 收盘清仓（不在名单的持仓）

`do_sell_non_list_positions`：
- **窗口**: 14:59:50-14:59:57（避开 CloseSellStrategy Phase4 的 14:58:00-14:59:50 窗口）
- **对象**: `query_positions()` 返回的持仓中，**不在 buy list** 的 symbol
- **数量**: `to_lot(available, 100)`（整手）
- **价格**: 跌停价（limits.second），若拿不到则退化用 bid1

## 相关文档

- [close.md](close.md)：CloseSellStrategy Phase4 留底仓后的补充清仓由本模块完成

## 备注：底仓补齐下单价格

底仓买入（`do_base_buy`）下单使用“市价标志 + 传入价格以满足接口校验”的方式。
为避免“市价成交价高于传入委托价导致废单”，当前实现使用 **涨停价（limits.first）** 作为传入价格上限。
