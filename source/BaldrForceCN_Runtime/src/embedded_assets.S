/*
 * embedded_assets.S
 *
 * 这个汇编文件不做任何游戏逻辑。它唯一的工作，是把 9 个已经由用户逐文件二进制
 * 对比确认“与干净原版不同”的旧汉化 loose 资源，原封不动放进 BaldrForceCN.asi。
 *
 * 为什么最终只放这 9 个：
 *   1. 旧汉化 VFS 除 BFE.PAC / Chapter.pac 外还有 311 个 loose 文件；
 *   2. 用户用二进制比较工具逐个对照了这 311 个文件；
 *   3. 只有下面 9 个与原版不同，其余 302 个逐字节完全相同；
 *   4. 因此 v1.0.0 不再像 test12 那样把整个 BMP/Hell 目录都塞进 ASI，避免保存
 *      13 份与原版完全相同、没有必要覆盖的资源；
 *   5. .incbin 只是“把文件字节复制进目标文件”，不会重新编码、不会改格式、也不会
 *      重新压缩，所以 ASI 内的数据与 recovered_vfs 中的原文件完全一致。
 *
 * C 代码会使用每个资源的 start/end 标签计算大小。游戏请求这些精确路径时，ASI 会
 * 创建一个 delete-on-close 的临时 backing file，写入对应字节，然后把真实 Windows
 * HANDLE 交回游戏。这样游戏后续仍然使用自己原来的 ReadFile/GetFileSize/SetFilePointer
 * 等流程，不需要伪造 HANDLE，也不需要我们理解 CPU/WAZ/GRP 的内部格式。
 *
 * 注意：32 位 Windows COFF 的 C 符号在汇编里带前导下划线。例如 C 中写
 * g_embedded_tooru_waz_start，链接器实际看到的是 _g_embedded_tooru_waz_start。
 */

.section .rdata,"dr"

/* ------------------------------------------------------------------------- */
/* BMP/Hell：只有这 3 个 HELLMODE 文件与原版差异巨大。                    */
/* ------------------------------------------------------------------------- */

/* HELLMODE 专用字图/图块资源。旧汉化版本体积远大于原版，是明确汉化资产。 */
.globl _g_embedded_hell_font_grp_start
_g_embedded_hell_font_grp_start:
.incbin "source/assets/BMP/Hell/Font.grp"
.globl _g_embedded_hell_font_grp_end
_g_embedded_hell_font_grp_end:

/* Trial/HELLMODE 菜单说明文字图块；test12 实机已证明它参与中文图片显示。 */
.globl _g_embedded_hell_menumsg_grp_start
_g_embedded_hell_menumsg_grp_start:
.incbin "source/assets/BMP/Hell/MenuMsg.grp"
.globl _g_embedded_hell_menumsg_grp_end
_g_embedded_hell_menumsg_grp_end:

/* HELLMODE 第三组菜单图块。二进制体积/内容与原版差异显著。 */
.globl _g_embedded_hell_menu03_grp_start
_g_embedded_hell_menu03_grp_start:
.incbin "source/assets/BMP/Hell/hell_Menu03.grp"
.globl _g_embedded_hell_menu03_grp_end
_g_embedded_hell_menu03_grp_end:

/* ------------------------------------------------------------------------- */
/* Dat/Cpu：3 个旧汉化 VFS 文件与原版只有少数字节不同。                    */
/*                                                                           */
/* 这里不猜这些字节是不是“翻译”。v1.0.0 的策略是忠实重现旧汉化真值：       */
/* 只要用户已经确认旧汉化文件与原版不同，就把旧字节按原路径提供给游戏。     */
/* ------------------------------------------------------------------------- */

.globl _g_embedded_cpu_genha_start
_g_embedded_cpu_genha_start:
.incbin "source/assets/Dat/Cpu/genha.cpu"
.globl _g_embedded_cpu_genha_end
_g_embedded_cpu_genha_end:

.globl _g_embedded_cpu_kaira_start
_g_embedded_cpu_kaira_start:
.incbin "source/assets/Dat/Cpu/kaira.cpu"
.globl _g_embedded_cpu_kaira_end
_g_embedded_cpu_kaira_end:

.globl _g_embedded_cpu_zako_s_t01_start
_g_embedded_cpu_zako_s_t01_start:
.incbin "source/assets/Dat/Cpu/zako_s_t01.cpu"
.globl _g_embedded_cpu_zako_s_t01_end
_g_embedded_cpu_zako_s_t01_end:

/* ------------------------------------------------------------------------- */
/* Dat/Waza：3 个与原版不同的 WAZ。                                          */
/* ------------------------------------------------------------------------- */

/* TOORU.WAZ 已实机确认包含“轻拳/直拳/上勾拳……”等中文招式名。 */
.globl _g_embedded_tooru_waz_start
_g_embedded_tooru_waz_start:
.incbin "source/assets/Dat/Waza/TOORU.WAZ"
.globl _g_embedded_tooru_waz_end
_g_embedded_tooru_waz_end:

/* neko05.waz 与 YAGISAWA.WAZ 的差异很小，但用户全量二进制比较确认不等于原版。 */
.globl _g_embedded_neko05_waz_start
_g_embedded_neko05_waz_start:
.incbin "source/assets/Dat/Waza/neko05.waz"
.globl _g_embedded_neko05_waz_end
_g_embedded_neko05_waz_end:

.globl _g_embedded_yagisawa_waz_start
_g_embedded_yagisawa_waz_start:
.incbin "source/assets/Dat/Waza/YAGISAWA.WAZ"
.globl _g_embedded_yagisawa_waz_end
_g_embedded_yagisawa_waz_end:
