# RAG / Context 构建指引（项目内）

目标：让 Agent 用**更少上下文**定位“症状 → 文件/函数 → 根因 → 修复边界”，并降低重复 debug。

## 推荐做法（适配本仓库）

1) **分层索引 + 精准入口**
   - `docs/index/*.json`：提供可检索入口（modules / pipelines / fields / code / logs）
   - `docs/modules/*.md`：每个模块一个文档，覆盖时间线、关键状态、常见 bug 模式

2) **Chunk 可独立理解**
   - 每个 Phase 用固定结构：窗口/输入/输出/门控/常见误用
   - 在段落顶部补齐必要上下文（避免“只看一段读不懂”）

3) **字段字典（fields.json）做“概念对齐”**
   - 把容易混淆的字段写清：`available vs total`、`notify_type`、`hold_vol`
   - 给出常见误用关键词，便于检索

4) **把“事实证据”写进 worklog/incident**
   - 一个 bug 最少包含：触发条件、关键日志关键词、修复点、验证方式
   - 复现步骤优先于结论

## 索引更新规则

- 新增/修改模块时：
  - 增补 `docs/modules/*.md`
  - 更新 `docs/index/pipelines.json`（时间窗 + 函数名 + 输出字段）
  - 如新增状态字段，更新 `docs/index/fields.json`
  - 如新增关键入口文件，更新 `docs/index/code.json`

## 备注（最近一次实践）

- 2026-02-03：BaseCancel/Close 的排撤、名单加载、收盘清仓等变更已同步到 `docs/modules/base_cancel.md` 和索引文件。

