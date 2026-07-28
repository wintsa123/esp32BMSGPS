# 卡片黑场转场设计

## Boundary

只修改 `components/esp_bms_lvgl_ui/esp_bms_lvgl_ui.c` 的既有黑场占位页及其
事件收尾。SDL 模拟器继续编译生产 UI，`simulator/main.c` 不实现重复的转场逻辑。

## Object Model

每张既有 `page_transition_*` 占位页保持满屏、同槽位、可吸附且不可滚动。它新增一个
手工定位的子卡片：黑底、深灰 1 px 边框、8 px 圆角，居中标题位于卡片内。

外层占位页始终维持 `s_ui.width x s_ui.height`，所以真实页隐藏后，`s_ui.pages` 的
可滚动范围和吸附坐标不变。子卡片由 `x/y/width/height` 动画控制；不使用图层透明度、
缩放变换、阴影或缓存。

## Event Contract

| Event / path | Required state |
| --- | --- |
| `LV_EVENT_SCROLL_BEGIN` | 显示黑场占位页；所有卡片重置为满屏，启动缩卡动画。 |
| `LV_EVENT_SCROLL` | 保持现有位移采样；占位页自然跟手，卡片随父对象移动。 |
| `LV_EVENT_SCROLL_THROW_BEGIN` | 保持现有释放位移冻结和切页阈值计算。 |
| 最终 `LV_EVENT_SCROLL_END` | 锁定目标页，令目标卡片放大；动画完成回调恢复真实页并刷新延迟快照。 |
| `finish_page_scroll_state()` / 重建 | 取消所有卡片动画，立即恢复真实页和稳定滚动位置。 |
| `move_to_page(..., false)` | 同步程序化滚动跳过黑卡事件路径，保持既有稳定数据与快照语义。 |

当滚动吸附尚未结束时，黑卡维持可见；最终结束只放大当前稳定槽位中的一张卡片。扩展
动画期间保持 `SETTLING`，令快照继续延迟，直到真实页恢复为止。LVGL 对卡片的
`x/y/width/height` 先更新样式再更新实际几何，因此开始放大前必须刷新卡片布局；快速
松手若仍停在满屏起点，先同步设置为收卡几何再执行放大动画。

## Compatibility And Rollback

- 页面枚举、目标映射、滚动阈值、原生手势和 API 签名不变。
- 所有新增对象是黑场占位页的子对象，随根页面删除；显式停止动画避免旧对象回调。
- 回滚此任务提交即可还原平面黑场标题转场，不涉及 NVS、配置迁移或设备烧录。
