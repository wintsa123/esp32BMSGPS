# 最终验证记录

- 基线：main，22cd1e336041fcee356b67d8406824e6e8eb8ef5。
- `bash scripts/run-host-selftests.sh` 最终退出0，包括全部协议/控制器自测及三组实际C行为 harness；EM02接收分支的型号校验、同通知多页排空和辅助页心跳已直接执行。见 host-selftests-final.log。
- 五套改动协议独立 strict C + ASan/UBSan 通过；Web JS语法与diff空白检查通过，见 check-report.md。
- 两项额外历史契约失败已由审查代理用HEAD源复现，未更改旧测试或无关行为。
- S3 N16R8 ST7796 FT6336 当前硬件profile完整构建成功。镜像 output/.bms-stability-validation/idf-build/esp32_bms_gps_idf.bin，大小0x1b5450，小于0x600000 app分区。
- RFC2217烧录尝试失败：`A fatal error occurred: Could not open rfc2217://192.168.2.10:4000?ign_set_control&timeout=10, the port is busy or doesn't exist.` 内层错误 `Could not open port ...: timed out`。独立socket TCP连接同端点5秒超时，TCP未建立，RFC2217未建立，未写入设备。见 firmware-flash.log。
- 最终提交前 GitNexus detect_changes(scope=all) 原始完整结果见 graph-changes-full.json：115符号/26个已跟踪差异文件，7条受影响流程，risk=high，partial/truncated均未设置，无error。CLI固定只打印15个符号，故另通过相同LocalBackend保存完整JSON。README已独立提交到main；本次图分析仍包含已有的无关hardware spec/workspace差异。新增未跟踪测试和文档由独立审查与实际编译补充覆盖。
- 风险和限制：未完成真机启动/长时连接/实物EM02验证。未新增参数写入、校准、升级协议或隐式ANT备用通道探测。
- 用户已确认“合并到main，推送到github”。产品代码已提交 main：a556d600；验证记录随后作为第二批提交并推送 origin/main。任务保持active待硬件恢复后继续，没有归档，无关工作区改动保留。
- 交付前 main 与 origin/main 均为59106d37，此提交仅变更README，与已验证产品代码无交集；未重复执行无新增变更的构建和测试。
