# 实现计划

1. 在三个品牌目录添加纯 C 请求构造、帧校验和遥测解析器，以及主机 selftest。
2. 扩展 `esp_bms_bms_ble` 的品牌协议描述、UUID 发现、轮询及通知分派。
3. 将新增源文件登记到组件 CMake，并编译运行 selftest。
4. 运行 ESP-IDF 构建、GitNexus change detection 和 LAN RFC2217 flash 尝试。

风险点：JBD 认证密码不可记录或硬编码；只实现只读链路。Daly A5 无可验证帧，不纳入支持承诺。彦阳工作区改动不修改也不回滚。
