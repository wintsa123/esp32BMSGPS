# App、Web 前端与设备固件更新分发

本文记录本轮可实施协议及后续发布流程。没有执行域名、R2、GitHub Release 或线上更新包部署；正式域名已由用户指定为 `fuckBms.wintsa123.top`（DNS/URL 规范化为 `fuckbms.wintsa123.top`）；指定域名不代表已经解析或部署。设备固件与 APK 的现有 Vercel 路径继续兼容，不能由本地清单推断线上已更新。

## 更新卡片与生效时机

设置页使用一张“更新”卡片，App、Web 前端、固件各一行，右侧“查看更新”打开底部弹窗并查询。版本、查询进度、结果、重试及安装/重启操作只在对应弹窗内显示。

| 类型 | 当前兼容来源 / 配置 | 下载及生效 |
| --- | --- | --- |
| App | 现有 `/apk/latest.json`、`/apk/两轮智控.apk` | 用户下载并由 Android 安装；安装签名必须与已安装 App 一致 |
| Web 前端 | 浏览器所在站点的构建及 SW；原生壳使用 APK 内固定的 `assets/dashboard-update.json` | 优先打开已缓存资源，每次启动后台检查并下载。完整验证后待激活，不自动刷新；下次打开或手动重启生效 |
| 设备固件 | 现有 `/firmware/firmware.json`、`/firmware/<profile>.bin` | 用户选择兼容硬件 profile，按设备 OTA 流程上传；不能与 Web 缓存更新混用 |

Web 的版本使用构建内容版本 `web-version.json.version`，不能拿工程 `package.json.version` 或“网络正常”当作最新判断。原生壳另外以单调递增 `sequence` 决定是否接受更新。未配置可信 Web 更新入口时显示“未配置更新源”，检查失败显示失败，禁止显示“已是最新”。前端的 `VITE_UPDATE_BASE_URL` 仅供后续切换 App/固件分发根地址；它不能替代 APK 内固定的 Web 签名公钥。

浏览器 SW 完整预缓存 `index.html`、构建资源和图片，排除 APK、固件与信任配置；不自动 `skipWaiting`。所有旧页面关闭后自然激活，手动重启才发送激活指令并重新加载。原生 WebView 存在 `BmsNative` 时不注册该 SW，避免两套缓存叠加；原生缓存切换不覆盖当前打开页面正在使用的资源目录。

浏览器站点固定 URL 的 `sw.js`、`web-version.json` 与入口 HTML 必须 `no-cache` 或短缓存，只有 hash 资源及带版本的更新目录适合 immutable。SW 作用域默认是脚本目录；站点部署在子目录时须对齐注册 scope，扩大作用域才配置相应的 `Service-Worker-Allowed`。不得把离线目录页或 HTML 错误页当下载包。

显式重启激活 SW 时，其他标签页仍可能使用旧 hash 资源；本轮保留旧缓存，直到下次无页面占用后的自然激活再清理，避免多标签旧页面出现 chunk 404。

## 前端迁移准备

已提供 `vercel/public/_headers`，构建自动复制到 dist：内容 hash 的 `/assets/*` 缓存一年，入口 HTML、`sw.js`、`web-version.json` 每次校验，APK/固件目录清单禁止缓存。现有两份 Vercel 配置采用相同策略；域名迁移前即可获益。浏览器缓存优先、后台下载与延后激活已实现，页面不依赖外部字体或第三方 CDN 脚本。

Cloudflare Pages 接入前端仓库 `wintsa123/espBmsSetting`，生产分支 `main`，根目录留空，安装 `npm ci`，构建命令 `npm run build`，产物 `dist`；在 Custom domains 绑定 `fuckbms.wintsa123.top` 并验证 DNS/TLS。无需改成服务器渲染，也无需发布新 tag。当前宿主没有可用 Cloudflare 登录，故这里只完成可审查的迁移配置，没有创建线上项目或修改 DNS。Web 原生更新需要下文的 R2 路由、签名源及新版 APK，单独部署网页不会自动启用。

更新源尚未就绪前保留原生 `UpdateApi.BASE_URL` 的现有地址。新源上线且真实清单与所有包无代理直连验证通过后，再把原生更新根地址与可信 Web 清单配置一起切换，并重新构建 APK；网页端默认同源查询，另可通过构建变量 `VITE_UPDATE_BASE_URL` 指定已验证的根地址。

## 原生 Web 更新协议 v1

可信配置只随 APK 打包，不允许从网络 Web 文件里替换：

```json
{
  "manifestUrl": "https://fuckbms.wintsa123.top/updates/stable/manifest.json",
  "publicKey": "RSA SPKI DER 的标准 base64",
  "bundledVersion": "构建内容版本",
  "bundledSequence": 0
}
```

当前原生默认未配置可信更新源。配置缺少真实 HTTPS URL 或 RSA 公钥即禁用在线更新，仍可运行 APK 内置页面。将工具生成且经核实的模板复制到 `android-cast/app/src/main/assets/dashboard-update.json`（外层 assets，不是 `dashboard/` 资源目录）后，重新构建并安装 APK，不能从远程下载模板或生成假公钥冒充启用。旧 App 必须先升级到包含新原生桥和缓存实现的安装包，后续才能独立升级离线 Web 资源。启动显示版本优先读取内置 `dashboard/web-version.json`，配置的 `bundledVersion` 仅为 fallback；`bundledSequence` 为内置版本序号，允许 0，不应误填待下载版本序号。

清单是签名信封：

```json
{"payload":"原始 JSON 字符串","signature":"RSA-SHA256 标准 base64 签名"}
```

签名覆盖 `payload` 原始 UTF-8 字节，接收者不能先解析再重新序列化。payload 内容：

```json
{
  "schema": 1,
  "version": "构建内容版本",
  "sequence": 42,
  "minAppVersionCode": 3,
  "files": [
    {"path":"index.html","url":"https://fuckbms.wintsa123.top/updates/stable/web-42/index.html","size":123,"sha256":"64位小写hex"}
  ]
}
```

RSA 公钥至少 2048 bit；展示版本非空且最多 100 字符。`minAppVersionCode >= 1`，`sequence >= 1`，两者最多为 JS 安全整数 9007199254740991；更新 sequence 必须严格大于内置、活跃和待激活版本序号的最大值。文件入口固定 `index.html`，不能改变原生桥或 Android 权限。路径最多 240 字符，仅允许 ASCII 字母、数字、`._/-`，每段非空且不能以 `.` 开头或为 `dashboard-update.json`，禁止反斜杠、百分号编码、路径越界及文件/目录前缀冲突。URL 必须是 HTTPS，无 userinfo、fragment，且与 manifest URL 同源（包含端口），不接受重定向（包括 manifest 请求本身）。限制：清单 256 KiB、文件最多 512 个、每个文件至少 1 字节且最多 8 MiB、文件总大小最多 32 MiB。

下载先到临时版本目录，逐文件核对长度及 SHA-256；签名无效、版本不兼容、路径错误、下载失败或资源不完整均不改变活跃目录。完整写盘后才标记待激活。下次打开或手动重启采用完整版本，保留内置兜底；需要回滚时发布“更高 sequence、旧版内容”的签名清单，避免放宽防降级门槛。已知故障版本可发布撤回说明并停止推荐，但不能悄悄覆盖其不可变文件。

## 本地生成工具

`scripts/package-web-update.py` 仅生成本地产物，不联网、不打 tag、不上传。依赖 Python 3 标准库与 OpenSSL。先正常构建前端，再使用离线保管的发布 RSA 私钥：

```sh
npm --prefix vercel run build
python3 scripts/package-web-update.py \
  --dist vercel/dist \
  --output /vol1/releases/web-42 \
  --base-url https://fuckbms.wintsa123.top/updates/stable \
  --key /secure/release-web-rsa.pem \
  --sequence 42 \
  --min-app-version-code 3 \
  --bundled-sequence 0
```

默认展示版本来自 dist 的 `web-version.json`，也可显式 `--version`。输出目录必须不存在，且在 dist 外。生成 `manifest.json`、`web-42/` 实际文件、`web-42.zip` 归档、`SHA256SUMS` 和独立的信任配置模板。ZIP 仅供 GitHub Releases 归档与人工审计；App 不下载或解压 ZIP。工具包含 `sw.js`、`web-version.json`；与 Android 内置资源任务一致排除顶层 `apk/`、`firmware/`、宿主配置 `_headers`/`_redirects`，遇到信任配置、符号链接和不合规路径直接失败。私钥不会复制到输出。

测试：`python3 -m unittest discover -s tests -p test_web_update_package.py -v`。测试验证签名及篡改拒绝、逐文件 hash/size、重复构建一致、私钥与信任配置隔离、危险路径与无效输入拒绝，不证明正式发布或真机更新成功。

## 推荐存储与 CDN 布局（待配置）

确定对外域名为 `fuckbms.wintsa123.top`，前端 `/`，签名清单 `/updates/stable/manifest.json`，全部更新资源保持同源。建议使用 Cloudflare Workers Static Assets（或 Pages）承载 Vite dist，再将 `/updates/*`、`/releases/*` 通过固定 Worker 路由读取 R2。R2 绑定和前端静态资源由同一个 Worker 分路径响应，是最直接的同源实现；另一方案为 Pages 提供前端、Worker 接管上述资源路径。不能同时把同一 hostname 的整个根域直接绑定 Pages 与 R2 两种 origin；R2 独立 bucket 域名可供 Worker 内部读取，但客户端收到的文件 URL 始终为上述正式域名。代理只读取固定 bucket/key 映射，不接受任意远程 URL，不能成为开放代理。当前未创建上述资源或 DNS 记录。

若未来独立更新域名，则清单及其全部文件仍必须在该更新域名同源，由新 APK 信任配置显式绑定；不能让原生直接从页面提供的任意地址下载。前端静态 hosting 与更新内容可以分别维护，但需要统一 TLS、CORS、缓存和错误状态，缺失资源应返回真实 404，而不是 SPA 的 index.html。

GitHub Releases 保存 APK、Web ZIP、各硬件 profile 的固件、签名清单、SHA256SUMS 和构建来源。建议启用 GitHub release immutability；官方说明启用后新发布 release 的标签及附件受保护。先建 draft、完成附件后再发布，旧附件不覆盖。可额外保存 release attestation；它不能替代 App 的签名验签。

Cloudflare R2 配置自定义域名作为主分发入口；R2 官方将 `r2.dev` 定位为开发用途且有限流，不作为生产主源。建议目录：

```text
updates/stable/manifest.json
updates/stable/web-42/index.html
updates/stable/web-42/assets/<contenthash>.js
releases/app/<versionCode>/<sha256>.apk
releases/firmware/<profile>/<version>/<sha256>.bin
```

manifest 建议 `Cache-Control: public, max-age=60, must-revalidate` 或更严格 `no-cache`，发布切换时主动清理该对象缓存；版本文件建议 `public, max-age=31536000, immutable`。R2 自定义域名可使用 Cloudflare Cache，但默认缓存规则不保证 JSON/APK 等类型全部缓存，应明确设置匹配版本目录的规则，避免把 manifest 覆盖为长期缓存。对象上传时同时设正确 Content-Type/Content-Length；Web 请求和离线缓存不能被 CDN HTML challenge 页面冒充成功响应。

原子发布顺序：完成签名构建 → GitHub draft 附件/审计 → R2 上传全体不可变版本文件 → 从真实 CDN 下载核对 SHA-256、状态码与 CORS → 最后上传/切换 manifest → 再从冷/热缓存读取验签 → 发布 release。不得先推 manifest 再补文件。回收旧版本必须留足离线客户端及回滚保留期。

## Fallback、CORS 与安全边界

当前同源文件清单协议不允许将某个 file URL 临时改成 GitHub 跨域链接，也不跟随跳转。第一层 fallback 是离线活跃缓存，第二层是 APK 内置页面；更新失败不影响已缓存页面。GitHub 是归档和人工恢复来源。将来需要自动镜像源时，应升级协议和受信任配置，使用另一个同源、自成完整版本树的镜像清单，并保持同一签名/版本/长度/hash校验，不能以无校验下载兜底。CDN/API 不可达时界面显示失败并允许重试，不清空有效缓存。

GitHub Releases 的 `browser_download_url` 会跳转到附件域名，当前禁重定向且同源校验下不作为 App 直接更新源或自动 fallback。若发布初期只有 GitHub，可另外评估专用公开分发仓库的 `raw.githubusercontent.com` 固定 commit 文件树，实测清单和全部文件无重定向且同源后才写入可信 APK 配置；不能拿稳定分支覆盖 URL 当不可变包。切换 CDN 主机需要重新发布带新信任配置的 APK；不能通过待更新的网页自行改变受信任源。

R2 CORS 允许实际前端来源与 WebView 资源来源、GET/HEAD/必要 OPTIONS，暴露需要读取的 ETag/Content-Length；只开放用到的请求头，不需要凭据。带 Origin 的真实跨域请求必须检查，而不能只测 curl 默认请求；R2 官方说明修改 CORS 后旧缓存可能保留旧头，需要 purge。生产允许来源应按实际配置固定，禁止因为测试方便给管理接口开放通配。原生 HTTP 客户端不受浏览器 CORS 限制，仍必须执行 HTTPS/同源/签名验证。

现有 `scripts/publish-update-server.py` 只生成 Vercel APK/固件数据，APK 版本取 Gradle，固件四位 code 是 CRC32 modulo 10000；该 code 用于现有设备校验，不是来源签名。旧 manifest 没有签名和完整 hash，不宣称已获得同等安全性。后续兼容新增 SHA-256、固件 profile/chip/flash容量/分区布局/最低 bootloader 与版本门槛后，仍要分别确认 App 读取器和设备 OTA 接收器实际执行。APK 必须 Android 正式签名并验证签名证书；固件正式渠道需结合设备支持的 secure boot / signed OTA，不能只校验文件名、四位码或 manifest 文本。

本轮实现边界：Web 有独立签名文件清单、后台完整下载校验与待激活缓存；APK、固件卡仍兼容原目录检查。为后两者补签名清单、SHA-256 强制校验和硬件门槛属于后续分发升级规划，不因 Web 已验签而自动获得。

私钥离线或 CI secret 管理，禁止放 Git、Web dist、APK 或公开 R2；仅公钥随 APK。公钥轮换通过可信 APK 更新或另行设计的交叉签名协议完成。Web 运行在带原生桥的上下文中，签名源属于可信代码发布权限；下载校验不代表可执行不受控第三方页面。

## 官方资料（2026-11-11 核对）

- GitHub：Preventing changes to your releases，`https://docs.github.com/en/code-security/how-tos/secure-your-supply-chain/establish-provenance-and-integrity/prevent-release-changes`
- Cloudflare R2：Public buckets，`https://developers.cloudflare.com/r2/buckets/public-buckets/`
- Cloudflare R2：Configure CORS，`https://developers.cloudflare.com/r2/buckets/cors/`
- Cloudflare Workers：Custom Domains，`https://developers.cloudflare.com/workers/configuration/routing/custom-domains/`
- Cloudflare Pages：Custom domains，`https://developers.cloudflare.com/pages/configuration/custom-domains/`
- Cloudflare China Network：Overview，`https://developers.cloudflare.com/china-network/`

上述资料用于确认平台能力；缓存时长、目录、发布顺序与回滚方法是本项目建议，尚未代表平台已配置。

## 大陆直连实测（2026-11-11 01:31，单宿主样本）

从当前宿主清除 `HTTP_PROXY`、`HTTPS_PROXY`、`ALL_PROXY` 及小写同名变量，每次 `curl --noproxy '*'`，不使用 DoH、代理、`--resolve` 或绕行。系统 DNS 为 223.5.5.5 / 119.29.29.29；另以标准 UDP DNS 分别查询两者。这里只能证明当前宿主出口的观测，不代表全国各运营商、各时段，也没有移动网络真机测速。

| 目标 | 条件 | HTTP / curl 结果 | DNS / TCP / TLS / 首字节 / 总时长（秒） |
| --- | --- | --- | --- |
| `fuckbms.wintsa123.top/` | 默认地址族，2 次 | DNS 失败，curl 6；两 DNS 均 NXDOMAIN | 未建立连接，总 0.111 / 0.061 |
| `fuckbms.wintsa123.top/updates/stable/manifest.json` | 计划正式清单路径，1 次 | DNS 失败，curl 6，HTTP 000 | 未建立连接 |
| `esp-bms-setting.vercel.app/` | 默认地址族，2 次 | curl 28，HTTP 000，TCP 超时 | DNS 0.168 / 0.021；其余未建立，总 7.002 / 7.002 |
| `esp-bms-setting.vercel.app/apk/latest.json` | 默认地址族，2 次 | curl 28，HTTP 000，TCP 超时 | DNS 0.020 / 0.047；其余未建立，总 7.002 / 7.001 |
| `esp-bms-setting.vercel.app/firmware/firmware.json` | IPv4 / IPv6 各 1 次 | 均 curl 28，HTTP 000 | DNS 0.007 / 0.017；其余未建立，总 6.001 / 6.001 |
| `octocat.github.io/` | 强制 IPv4，3980 bytes | 200 | 0.064 / 0.280 / 0.506 / 0.727 / 0.727 |
| `octocat.github.io/` | 强制 IPv6，3980 bytes | 200 | 0.061 / 0.126 / 0.194 / 0.429 / 0.429 |
| `developers.cloudflare.com/` | 强制 IPv4，201086 bytes | 200 | 0.009 / 0.166 / 0.334 / 0.548 / 1.096 |
| `developers.cloudflare.com/` | 强制 IPv6，201086 bytes | 200 | 0.007 / 0.171 / 0.346 / 0.551 / 1.084 |

耗时来自 curl 累计时间字段，不能把 TLS 列当作独立握手耗时。CF 官方页另两次默认地址族请求为 200，总 1.121 / 1.509 秒，GitHub Pages 官方页 `pages.github.com/` 为 200，总 0.409 / 0.214 秒。对照页体积和业务文件不同，速度不可直接横比。Vercel 的标准 DNS 在两解析器返回不同地址，本轮只记录解析和连接失败，不据此断言具体封锁或污染原因。

本轮结论：现有 Vercel 入口在该宿主直连失败，指定新域名未解析；CF、GitHub 对照能打开，但这不是已部署业务的下载测速，不能宣称 Cloudflare 必然优于 Vercel。Cloudflare 官方说明中国大陆网络是 Enterprise 的独立订阅能力，普通全球 CF 静态托管/R2 自定义域名不能承诺大陆节点或普遍直连质量。部署前后需用指定域名真实 HTML、JS、图片及 APK/固件，在电信/联通/移动 IPv4/IPv6、冷/热缓存重复测试成功率、TTFB、完整下载速率和 SHA-256；未通过前保留缓存兜底与现有兼容入口。

可复现实测命令（每个目标替换 URL，禁止去掉 `--noproxy`）：

```sh
env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY -u http_proxy -u https_proxy -u all_proxy \
  curl --noproxy '*' -4 -sS -o /dev/null --connect-timeout 7 --max-time 15 \
  -w '%{json}' https://fuckbms.wintsa123.top/
dig @223.5.5.5 fuckbms.wintsa123.top A
dig @119.29.29.29 fuckbms.wintsa123.top A
```

本轮只检查凭据字段名：当前进程没有 CF/Cloudflare/R2/Wrangler 对应键，在可读的用户配置和项目 `.env*` 候选中未发现对应凭据字段；没有访问或使用真实密钥，不能据此声称宿主所有位置均无凭据（例如 `/root` 不可读）。部署仍需明确账号、zone、权限、R2/Worker 资源和发布密钥。
