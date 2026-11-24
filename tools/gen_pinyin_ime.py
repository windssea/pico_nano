"""生成离线全拼输入法查表头文件（components/pn_widgets/pn_pinyin_ime.generated.h），不手改产物。
Generate the offline full-pinyin IME table header (components/pn_widgets/pn_pinyin_ime.generated.h); never hand-edit output.

基于GB2312-80一级汉字（3755字，按全拼顺序排列）构建约400个标准音节表，并结合常用字频与高频多音字前置排序，
全表仅约15 KiB只读Flash常量，无运行期堆分配。
Builds ~400 standard Mandarin syllables from GB2312-80 level-1 hanzi (3755 chars sorted by full pinyin),
promoting high-frequency everyday/literary characters and common polyphones; ~15 KiB const Flash, zero heap.
"""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "components/pn_widgets/pn_pinyin_ime.generated.h"

# GB2312一级汉字（0xB0A1–0xD7F9）全部396个全拼音节首字（严格按GB2312码位升序）
# All 396 full-pinyin syllable starting characters in GB2312 level-1 (0xB0A1–0xD7F9), strictly monotonic.
SYLLABLE_STARTS = [
    ("啊", "a"), ("埃", "ai"), ("鞍", "an"), ("肮", "ang"), ("凹", "ao"),
    ("芭", "ba"), ("白", "bai"), ("斑", "ban"), ("邦", "bang"), ("苞", "bao"),
    ("杯", "bei"), ("奔", "ben"), ("崩", "beng"), ("逼", "bi"), ("鞭", "bian"),
    ("标", "biao"), ("鳖", "bie"), ("彬", "bin"), ("兵", "bing"), ("玻", "bo"), ("捕", "bu"),
    ("擦", "ca"), ("猜", "cai"), ("餐", "can"), ("苍", "cang"), ("操", "cao"), ("厕", "ce"), ("层", "ceng"),
    ("插", "cha"), ("拆", "chai"), ("搀", "chan"), ("昌", "chang"), ("超", "chao"), ("车", "che"),
    ("郴", "chen"), ("撑", "cheng"), ("吃", "chi"), ("充", "chong"), ("抽", "chou"), ("初", "chu"),
    ("揣", "chuai"), ("川", "chuan"), ("疮", "chuang"), ("吹", "chui"), ("春", "chun"), ("戳", "chuo"),
    ("疵", "ci"), ("聪", "cong"), ("凑", "cou"), ("粗", "cu"), ("蹿", "cuan"), ("摧", "cui"), ("村", "cun"), ("磋", "cuo"),
    ("搭", "da"), ("呆", "dai"), ("耽", "dan"), ("当", "dang"), ("刀", "dao"), ("德", "de"), ("蹬", "deng"),
    ("堤", "di"), ("颠", "dian"), ("碉", "diao"), ("跌", "die"), ("丁", "ding"), ("丢", "diu"),
    ("东", "dong"), ("兜", "dou"), ("都", "du"), ("端", "duan"), ("堆", "dui"), ("墩", "dun"), ("掇", "duo"),
    ("蛾", "e"), ("恩", "en"), ("而", "er"),
    ("发", "fa"), ("藩", "fan"), ("坊", "fang"), ("菲", "fei"), ("芬", "fen"), ("丰", "feng"), ("佛", "fo"), ("否", "fou"), ("夫", "fu"),
    ("噶", "ga"), ("该", "gai"), ("干", "gan"), ("冈", "gang"), ("篙", "gao"), ("哥", "ge"), ("给", "gei"),
    ("根", "gen"), ("耕", "geng"), ("工", "gong"), ("钩", "gou"), ("辜", "gu"), ("刮", "gua"), ("乖", "guai"),
    ("棺", "guan"), ("光", "guang"), ("瑰", "gui"), ("辊", "gun"), ("锅", "guo"),
    ("哈", "ha"), ("骸", "hai"), ("酣", "han"), ("夯", "hang"), ("壕", "hao"), ("呵", "he"), ("嘿", "hei"),
    ("痕", "hen"), ("哼", "heng"), ("轰", "hong"), ("喉", "hou"), ("呼", "hu"), ("花", "hua"), ("槐", "huai"),
    ("欢", "huan"), ("荒", "huang"), ("灰", "hui"), ("荤", "hun"), ("豁", "huo"),
    ("击", "ji"), ("嘉", "jia"), ("歼", "jian"), ("僵", "jiang"), ("蕉", "jiao"), ("揭", "jie"),
    ("巾", "jin"), ("荆", "jing"), ("炯", "jiong"), ("揪", "jiu"), ("鞠", "ju"), ("捐", "juan"), ("撅", "jue"), ("均", "jun"),
    ("喀", "ka"), ("开", "kai"), ("刊", "kan"), ("康", "kang"), ("考", "kao"), ("坷", "ke"), ("肯", "ken"),
    ("坑", "keng"), ("空", "kong"), ("抠", "kou"), ("枯", "ku"), ("夸", "kua"), ("块", "kuai"), ("宽", "kuan"),
    ("匡", "kuang"), ("亏", "kui"), ("坤", "kun"), ("括", "kuo"),
    ("垃", "la"), ("莱", "lai"), ("蓝", "lan"), ("琅", "lang"), ("捞", "lao"), ("勒", "le"), ("雷", "lei"),
    ("棱", "leng"), ("厘", "li"), ("俩", "lia"), ("联", "lian"), ("粮", "liang"), ("撩", "liao"), ("列", "lie"),
    ("琳", "lin"), ("拎", "ling"), ("溜", "liu"), ("龙", "long"), ("楼", "lou"), ("芦", "lu"), ("驴", "lv"),
    ("峦", "luan"), ("掠", "lve"), ("抡", "lun"), ("萝", "luo"),
    ("妈", "ma"), ("埋", "mai"), ("瞒", "man"), ("芒", "mang"), ("猫", "mao"), ("么", "me"), ("玫", "mei"),
    ("门", "men"), ("萌", "meng"), ("眯", "mi"), ("棉", "mian"), ("苗", "miao"), ("蔑", "mie"), ("民", "min"),
    ("明", "ming"), ("谬", "miu"), ("摸", "mo"), ("谋", "mou"), ("拇", "mu"),
    ("拿", "na"), ("氖", "nai"), ("南", "nan"), ("囊", "nang"), ("挠", "nao"), ("呢", "ne"), ("馁", "nei"),
    ("嫩", "nen"), ("能", "neng"), ("妮", "ni"), ("蔫", "nian"), ("娘", "niang"), ("鸟", "niao"), ("捏", "nie"),
    ("您", "nin"), ("柠", "ning"), ("牛", "niu"), ("脓", "nong"), ("奴", "nu"), ("女", "nv"), ("暖", "nuan"),
    ("虐", "nve"), ("挪", "nuo"),
    ("哦", "o"), ("欧", "ou"),
    ("啪", "pa"), ("拍", "pai"), ("攀", "pan"), ("乓", "pang"), ("抛", "pao"), ("呸", "pei"), ("喷", "pen"),
    ("砰", "peng"), ("坯", "pi"), ("篇", "pian"), ("飘", "piao"), ("撇", "pie"), ("拼", "pin"), ("乒", "ping"),
    ("坡", "po"), ("剖", "pou"), ("扑", "pu"),
    ("期", "qi"), ("掐", "qia"), ("牵", "qian"), ("枪", "qiang"), ("橇", "qiao"), ("切", "qie"), ("钦", "qin"),
    ("青", "qing"), ("琼", "qiong"), ("秋", "qiu"), ("趋", "qu"), ("圈", "quan"), ("缺", "que"), ("裙", "qun"),
    ("然", "ran"), ("瓤", "rang"), ("饶", "rao"), ("惹", "re"), ("壬", "ren"), ("扔", "reng"), ("日", "ri"),
    ("戎", "rong"), ("揉", "rou"), ("茹", "ru"), ("软", "ruan"), ("蕊", "rui"), ("闰", "run"), ("若", "ruo"),
    ("撒", "sa"), ("腮", "sai"), ("三", "san"), ("桑", "sang"), ("搔", "sao"), ("瑟", "se"), ("森", "sen"),
    ("僧", "seng"), ("莎", "sha"), ("筛", "shai"), ("珊", "shan"), ("墒", "shang"), ("梢", "shao"), ("奢", "she"),
    ("砷", "shen"), ("声", "sheng"), ("师", "shi"), ("收", "shou"), ("蔬", "shu"), ("刷", "shua"), ("摔", "shuai"),
    ("栓", "shuan"), ("霜", "shuang"), ("谁", "shui"), ("吮", "shun"), ("说", "shuo"), ("斯", "si"), ("松", "song"),
    ("搜", "sou"), ("苏", "su"), ("酸", "suan"), ("虽", "sui"), ("孙", "sun"), ("蓑", "suo"),
    ("塌", "ta"), ("胎", "tai"), ("坍", "tan"), ("汤", "tang"), ("掏", "tao"), ("特", "te"), ("藤", "teng"),
    ("梯", "ti"), ("天", "tian"), ("挑", "tiao"), ("贴", "tie"), ("厅", "ting"), ("通", "tong"), ("偷", "tou"),
    ("凸", "tu"), ("湍", "tuan"), ("推", "tui"), ("吞", "tun"), ("拖", "tuo"),
    ("挖", "wa"), ("歪", "wai"), ("豌", "wan"), ("汪", "wang"), ("威", "wei"), ("瘟", "wen"), ("嗡", "weng"),
    ("挝", "wo"), ("巫", "wu"),
    ("昔", "xi"), ("瞎", "xia"), ("掀", "xian"), ("相", "xiang"), ("萧", "xiao"), ("楔", "xie"), ("薪", "xin"),
    ("星", "xing"), ("兄", "xiong"), ("休", "xiu"), ("墟", "xu"), ("轩", "xuan"), ("靴", "xue"), ("勋", "xun"),
    ("压", "ya"), ("焉", "yan"), ("殃", "yang"), ("邀", "yao"), ("椰", "ye"), ("一", "yi"), ("茵", "yin"),
    ("英", "ying"), ("哟", "yo"), ("拥", "yong"), ("幽", "you"), ("迂", "yu"), ("鸳", "yuan"), ("曰", "yue"), ("耘", "yun"),
    ("匝", "za"), ("栽", "zai"), ("咱", "zan"), ("赃", "zang"), ("遭", "zao"), ("责", "ze"), ("贼", "zei"),
    ("怎", "zen"), ("增", "zeng"), ("扎", "zha"), ("摘", "zhai"), ("瞻", "zhan"), ("樟", "zhang"), ("招", "zhao"),
    ("遮", "zhe"), ("珍", "zhen"), ("蒸", "zheng"), ("芝", "zhi"), ("中", "zhong"), ("舟", "zhou"), ("珠", "zhu"),
    ("抓", "zhua"), ("拽", "zhuai"), ("专", "zhuan"), ("桩", "zhuang"), ("椎", "zhui"), ("谆", "zhun"), ("捉", "zhuo"),
    ("兹", "zi"), ("鬃", "zong"), ("邹", "zou"), ("租", "zu"), ("钻", "zuan"), ("嘴", "zui"), ("尊", "zun"), ("昨", "zuo"),
]

# 常用字频与高频多音字前置（确保最常用字及常见阅读/名言用字始终出现在候选第1页）
# High-frequency everyday/literary chars & common polyphones promoted to the front of each syllable.
PROMOTE = {
    "a": "啊阿",
    "ai": "爱哀埃矮碍艾",
    "an": "安按暗案岸俺",
    "ang": "昂盎",
    "ao": "傲奥熬澳袄",
    "ba": "把吧八巴爸拔霸罢",
    "bai": "白百拜摆败柏",
    "ban": "半办班般版板伴搬",
    "bang": "帮榜棒绑傍",
    "bao": "保报包宝抱薄暴饱",
    "bei": "被北备背倍杯悲辈贝",
    "ben": "本奔笨",
    "beng": "崩蹦",
    "bi": "比必笔彼闭壁避碧毕鼻",
    "bian": "边变便编遍辨辩",
    "biao": "表标",
    "bie": "别",
    "bin": "宾滨彬",
    "bing": "并冰兵病柄饼",
    "bo": "波博播薄泊伯勃拨剥",
    "bu": "不部步布补捕",
    "ca": "擦",
    "cai": "才采彩菜财猜裁材",
    "can": "参残餐灿惨",
    "cang": "藏苍仓沧",
    "cao": "草操曹槽",
    "ce": "策测册侧厕",
    "ceng": "曾层",
    "cha": "查差茶察插叉",
    "chai": "拆柴",
    "chan": "产禅缠蝉",
    "chang": "长常场唱畅尝肠厂",
    "chao": "朝超潮抄炒巢",
    "che": "车彻撤扯",
    "chen": "晨尘陈沉臣趁衬",
    "cheng": "成城程承诚称乘澄橙",
    "chi": "吃持池迟尺赤齿耻",
    "chong": "重充冲虫崇宠",
    "chou": "愁抽筹丑臭",
    "chu": "出处初除楚触储厨",
    "chuan": "传穿船川串",
    "chuang": "窗创床闯",
    "chui": "吹垂锤",
    "chun": "春纯醇唇",
    "chuo": "戳",
    "ci": "此词次辞慈刺瓷磁",
    "cong": "从聪丛匆葱",
    "cou": "凑",
    "cu": "粗促醋簇",
    "cui": "翠催脆摧崔",
    "cun": "存村寸",
    "cuo": "错措挫",
    "da": "大打达答搭",
    "dai": "代带待戴袋呆",
    "dan": "但单淡担旦胆丹弹诞",
    "dang": "当党档挡荡",
    "dao": "到道导倒岛刀稻盗",
    "de": "的得地德",
    "deng": "等灯登凳邓",
    "di": "地第底低帝弟敌递抵滴",
    "dian": "点电典店殿典垫颠",
    "diao": "调掉雕钓吊",
    "die": "叠蝶跌爹",
    "ding": "定顶丁订盯钉鼎",
    "diu": "丢",
    "dong": "动东冬懂洞栋",
    "dou": "都斗豆抖兜",
    "du": "读度独都杜堵渡肚毒",
    "duan": "段短断端锻",
    "dui": "对队堆兑",
    "dun": "顿盾蹲墩",
    "duo": "多朵夺躲舵",
    "e": "恶额鹅俄饿",
    "en": "恩",
    "er": "而二儿耳尔",
    "fa": "发法罚乏阀",
    "fan": "反凡繁饭翻范烦帆返",
    "fang": "方放房防访仿芳坊",
    "fei": "非飞费肥废肺菲",
    "fen": "分份粉奋纷芬愤坟",
    "feng": "风丰封峰逢奉凤枫蜂",
    "fo": "佛",
    "fou": "否",
    "fu": "复服福富父夫负副付附府腹浮扶符抚赋",
    "ga": "嘎",
    "gai": "改该概盖钙",
    "gan": "感干敢甘赶杆肝",
    "gang": "刚钢岗港纲",
    "gao": "高告搞稿糕",
    "ge": "个各歌格哥割隔革阁",
    "gei": "给",
    "gen": "根跟",
    "geng": "更耕耿梗",
    "gong": "工公功共供攻宫恭贡",
    "gou": "够构购沟狗勾",
    "gu": "古故固顾骨谷孤鼓估股",
    "gua": "挂瓜刮寡",
    "guai": "怪乖拐",
    "guan": "关观管官馆惯冠贯",
    "guang": "光广逛",
    "gui": "归规贵鬼桂轨柜龟",
    "gun": "滚棍",
    "guo": "国过果郭锅裹",
    "ha": "哈",
    "hai": "还海孩害骇",
    "han": "汉寒含韩喊汗涵翰旱",
    "hang": "行航杭",
    "hao": "好号豪毫浩耗",
    "he": "和何合河核荷贺喝赫鹤",
    "hei": "黑嘿",
    "hen": "很恨痕狠",
    "heng": "横衡恒哼",
    "hong": "红洪宏鸿虹轰哄",
    "hou": "后候厚猴喉侯",
    "hu": "户护互湖呼胡虎狐糊壶",
    "hua": "华花话画化划滑",
    "huai": "怀坏淮槐",
    "huan": "还换欢环缓幻唤患",
    "huang": "黄荒皇慌晃煌谎",
    "hui": "会回慧辉挥灰汇毁绘悔惠",
    "hun": "魂婚混昏浑",
    "huo": "或活火获货伙豁惑霍",
    "ji": "及机几己既记际极集计基激积继纪季寄急技击寂迹",
    "jia": "家加价假架佳甲驾嫁夹",
    "jian": "见间件建简坚剑渐检减兼健鉴践尖肩箭",
    "jiang": "将江讲奖降疆姜匠酱",
    "jiao": "教交叫觉较脚角焦娇骄胶",
    "jie": "结解界接节姐借街皆阶戒洁杰截捷",
    "jin": "进今金近尽紧仅禁锦巾筋浸",
    "jing": "经精京静境敬景镜净警惊晶竞径",
    "jiong": "炯窘",
    "jiu": "就九久究酒旧救纠",
    "ju": "具局居举句剧聚据巨拒距菊俱",
    "juan": "卷捐倦绢鹃",
    "jue": "觉决绝掘爵",
    "jun": "军均君俊菌",
    "ka": "卡咖",
    "kai": "开凯楷慨",
    "kan": "看刊堪砍勘",
    "kang": "康抗慷",
    "kao": "考靠烤",
    "ke": "可科克客刻课颗渴壳棵",
    "ken": "肯恳啃",
    "keng": "坑",
    "kong": "空控孔恐",
    "kou": "口扣",
    "ku": "苦哭库酷枯裤",
    "kua": "跨夸垮",
    "kuai": "快块筷会",
    "kuan": "宽款",
    "kuang": "况狂矿框旷",
    "kui": "亏愧葵溃",
    "kun": "困昆坤捆",
    "kuo": "扩括阔",
    "la": "拉啦落辣蜡",
    "lai": "来赖莱",
    "lan": "蓝兰览懒栏拦烂澜",
    "lang": "浪朗狼廊郎",
    "lao": "老劳牢落捞",
    "le": "了乐勒",
    "lei": "类雷累泪垒",
    "leng": "冷棱",
    "li": "里理力立利历离礼丽李例励粒璃黎",
    "lia": "俩",
    "lian": "连联练脸恋莲炼廉链",
    "liang": "两量良亮凉梁粮谅",
    "liao": "了料聊疗辽僚",
    "lie": "列烈裂猎劣",
    "lin": "林临邻淋琳",
    "ling": "领令灵零另龄岭凌铃",
    "liu": "流留六刘柳溜",
    "long": "龙隆笼拢",
    "lou": "楼露漏陋",
    "lu": "路录陆露鲁炉鹿卢芦",
    "lv": "绿旅律虑履率吕驴",
    "luan": "乱卵峦",
    "lve": "略掠",
    "lue": "略掠",
    "lun": "论轮伦",
    "luo": "落罗逻洛骆锣螺",
    "ma": "吗马妈码麻骂玛",
    "mai": "买卖迈脉埋麦",
    "man": "满慢漫蛮曼瞒",
    "mang": "忙茫盲芒",
    "mao": "毛猫貌贸冒帽矛茅",
    "me": "么",
    "mei": "没每美眉梅妹媒煤枚",
    "men": "们门闷",
    "meng": "梦猛蒙萌盟",
    "mi": "密迷米秘弥蜜觅谜",
    "mian": "面免眠棉绵勉缅",
    "miao": "妙描苗秒庙渺",
    "mie": "灭蔑",
    "min": "民敏闽抿",
    "ming": "明名命鸣铭",
    "miu": "谬",
    "mo": "模墨默莫磨末魔抹漠陌摸",
    "mou": "某谋",
    "mu": "目母木幕墓慕牧拇",
    "na": "那拿哪纳娜",
    "nai": "乃耐奶奈",
    "nan": "南难男",
    "nang": "囊",
    "nao": "脑闹恼挠",
    "ne": "呢哪",
    "nei": "内",
    "nen": "嫩",
    "neng": "能",
    "ni": "你尼泥拟逆腻",
    "nian": "年念粘碾",
    "niang": "娘酿",
    "niao": "鸟",
    "nie": "捏",
    "nin": "您",
    "ning": "宁凝柠",
    "niu": "牛扭纽",
    "nong": "农浓弄",
    "nu": "努怒奴",
    "nv": "女",
    "nuan": "暖",
    "nve": "虐疟",
    "nue": "虐疟",
    "nuo": "诺挪糯",
    "o": "哦",
    "ou": "欧偶藕",
    "pa": "怕爬帕拍趴",
    "pai": "排派拍牌",
    "pan": "盘判盼攀畔叛",
    "pang": "旁胖庞",
    "pao": "跑泡抛炮袍",
    "pei": "配培陪佩赔",
    "pen": "喷盆",
    "peng": "朋碰捧彭蓬棚",
    "pi": "批皮疲匹辟披脾",
    "pian": "片篇偏骗便",
    "piao": "票漂飘瓢",
    "pie": "撇瞥",
    "pin": "品拼频贫聘",
    "ping": "平评凭屏瓶苹坪",
    "po": "破迫坡婆颇魄泼",
    "pou": "剖",
    "pu": "普铺朴谱扑蒲葡",
    "qi": "其起气期七奇齐器启企妻弃泣棋骑旗岂契",
    "qia": "恰洽掐",
    "qian": "前千浅钱签潜牵迁欠谦遣",
    "qiang": "强墙抢枪腔",
    "qiao": "桥巧悄敲瞧乔侨翘",
    "qie": "且切窃怯",
    "qin": "亲勤琴侵秦禽寝",
    "qing": "情清青请轻晴庆倾卿",
    "qiong": "穷琼",
    "qiu": "求秋球丘",
    "qu": "去取区曲趣趋驱渠屈",
    "quan": "全权泉圈劝券拳",
    "que": "却确缺雀鹊",
    "qun": "群裙",
    "ran": "然燃染冉",
    "rang": "让壤",
    "rao": "绕饶扰",
    "re": "热惹",
    "ren": "人任认忍仁刃",
    "reng": "仍扔",
    "ri": "日",
    "rong": "容荣融溶绒茸",
    "rou": "柔肉揉",
    "ru": "如入乳儒辱",
    "ruan": "软阮",
    "rui": "瑞锐蕊",
    "run": "润闰",
    "ruo": "若弱",
    "sa": "撒洒萨",
    "sai": "赛塞腮",
    "san": "三散伞",
    "sang": "桑丧嗓",
    "sao": "扫骚嫂",
    "se": "色涩瑟",
    "sen": "森",
    "seng": "僧",
    "sha": "沙杀傻纱刹砂",
    "shai": "晒筛",
    "shan": "山善闪衫删扇陕珊",
    "shang": "上商伤赏尚裳",
    "shao": "少烧稍哨韶梢",
    "she": "设社舍摄涉射蛇舌",
    "shen": "身深神什甚申审伸慎沈",
    "sheng": "生声省胜升盛剩圣绳",
    "shi": "是时事世市实使士诗师十石始式试视识示室适释势食史失",
    "shou": "手受收首守寿授售瘦兽",
    "shu": "书数树属术述熟舒疏叔输暑曙署鼠",
    "shua": "刷耍",
    "shuai": "率衰帅摔甩",
    "shuan": "栓",
    "shuang": "双爽霜",
    "shui": "水睡谁税",
    "shun": "顺瞬舜",
    "shuo": "说硕烁",
    "si": "思四死斯司丝私似寺肆伺",
    "song": "送松宋诵颂耸",
    "sou": "搜艘",
    "su": "素速苏诉俗宿塑肃",
    "suan": "算酸蒜",
    "sui": "岁随虽碎穗遂",
    "sun": "损孙笋",
    "suo": "所锁索缩梭",
    "ta": "他她它塔踏塌",
    "tai": "太台态抬泰胎苔",
    "tan": "谈探叹炭坦坛弹潭摊贪",
    "tang": "堂唐糖汤躺塘趟",
    "tao": "讨套逃陶桃涛掏",
    "te": "特",
    "teng": "腾疼藤",
    "ti": "体提题替梯踢啼",
    "tian": "天田甜添填",
    "tiao": "条调跳挑眺",
    "tie": "铁贴帖",
    "ting": "听停庭厅挺亭",
    "tong": "同通统痛童铜桐筒",
    "tou": "头投透偷",
    "tu": "图土突途涂徒兔吐屠",
    "tuan": "团湍",
    "tui": "推退腿颓",
    "tun": "吞屯",
    "tuo": "脱托拖妥拓驼",
    "wa": "瓦挖娃蛙洼",
    "wai": "外歪",
    "wan": "万完晚玩湾弯碗挽顽宛",
    "wang": "望王往网忘亡旺汪",
    "wei": "为位未微围维委伟味威卫唯谓尾慰伪",
    "wen": "文问闻温稳纹吻蚊",
    "weng": "翁嗡",
    "wo": "我握卧窝沃蜗",
    "wu": "无物五务武午舞误屋吴乌悟雾污伍勿",
    "xi": "西系息希细习喜洗戏惜夕吸稀析溪袭席悉膝",
    "xia": "下夏侠霞狭峡吓虾瞎",
    "xian": "现先线限显险献县闲鲜仙贤弦衔嫌",
    "xiang": "想向相象像响项香乡详享箱翔",
    "xiao": "小笑消效校晓萧肖宵削",
    "xie": "些写谢协斜鞋携歇泄械",
    "xin": "心新信辛欣薪芯",
    "xing": "行性形星兴醒幸姓刑型",
    "xiong": "雄兄胸凶",
    "xiu": "修休秀袖绣羞嗅",
    "xu": "需许续序虚须徐叙蓄",
    "xuan": "选宣悬旋轩喧绚",
    "xue": "学雪血穴削靴",
    "xun": "寻讯训巡迅循旬勋",
    "ya": "压亚雅牙呀押鸭芽哑",
    "yan": "言眼研演严验沿烟颜盐延燕厌宴岩艳",
    "yang": "样阳养扬洋央杨羊氧仰",
    "yao": "要药遥摇腰邀咬耀妖",
    "ye": "也业夜叶野爷页液椰",
    "yi": "一以已意亦义易艺益议移依医亿遗译仪异宜衣疑椅忆翼",
    "yin": "因音引银印阴饮隐吟荫殷",
    "ying": "应影英营迎硬映盈樱鹰",
    "yo": "哟",
    "yong": "用永拥勇泳咏庸涌",
    "you": "有又由游友优右油幽悠邮犹诱幼",
    "yu": "于与语玉雨余遇育预宇域欲羽鱼愈娱御誉寓浴榆渔",
    "yuan": "元原远院员愿园源圆援缘渊",
    "yue": "月阅越约乐岳悦曰",
    "yun": "云运允韵蕴匀晕",
    "za": "杂砸匝",
    "zai": "在再载灾栽宰",
    "zan": "赞暂咱攒",
    "zang": "藏脏葬赃",
    "zao": "早造遭燥枣噪澡灶",
    "ze": "则责择泽",
    "zei": "贼",
    "zen": "怎",
    "zeng": "增曾赠憎",
    "zha": "扎炸查闸眨榨",
    "zhai": "摘债宅窄斋寨",
    "zhan": "展战站占斩瞻盏绽",
    "zhang": "长张章掌涨账丈障彰",
    "zhao": "照找着招赵兆召朝沼",
    "zhe": "这者着折哲遮浙",
    "zhen": "真阵镇珍针震振诊贞枕",
    "zheng": "正政整争证征郑症蒸挣",
    "zhi": "之只知至制直指治志职质值支纸止置址智枝织执植",
    "zhong": "中重种众终钟忠肿仲",
    "zhou": "周州洲舟昼骤皱轴",
    "zhu": "主住注助著筑珠逐竹祝柱朱猪煮驻",
    "zhua": "抓爪",
    "zhuai": "拽",
    "zhuan": "转专传砖赚撰",
    "zhuang": "装状壮庄桩撞",
    "zhui": "追坠缀椎",
    "zhun": "准谆",
    "zhuo": "着桌捉卓琢拙",
    "zi": "自字子资紫姿咨滋仔",
    "zong": "总宗综纵踪棕",
    "zou": "走奏邹",
    "zu": "组族足祖阻租卒诅",
    "zuan": "钻纂",
    "zui": "最嘴罪醉",
    "zun": "尊遵",
    "zuo": "作做坐座左昨佐",
}

END = 0xD7F9


def gb2312_code(ch: str) -> int:
    b = ch.encode("gb2312")
    assert len(b) == 2
    return (b[0] << 8) | b[1]


def build_table():
    bounds = []
    for ch, syl in SYLLABLE_STARTS:
        code = gb2312_code(ch)
        if bounds and code <= bounds[-1][0]:
            raise SystemExit(f"Non-monotonic syllable start: {ch} ({syl}) 0x{code:04X} <= 0x{bounds[-1][0]:04X}")
        bounds.append((code, syl))

    buckets: dict[str, list[str]] = {syl: [] for _, syl in bounds}
    # Add alias syllables (lue/nue) so users typing either lve/lue or nve/nue match
    buckets.setdefault("lue", [])
    buckets.setdefault("nue", [])

    idx = 0
    total_chars = 0
    for lead in range(0xB0, 0xD8):
        for trail in range(0xA1, 0xFF):
            code = (lead << 8) | trail
            if code > END:
                break
            while idx + 1 < len(bounds) and code >= bounds[idx + 1][0]:
                idx += 1
            ch = bytes([lead, trail]).decode("gb2312", errors="strict")
            buckets[bounds[idx][1]].append(ch)
            total_chars += 1

    if total_chars != 3755:
        raise SystemExit(f"Expected 3755 GB2312 level-1 chars, got {total_chars}")

    # Copy lve -> lue, nve -> nue
    buckets["lue"] = list(buckets["lve"])
    buckets["nue"] = list(buckets["nve"])

    # Apply PROMOTE (deduplicated, preserving order, keeping all GB2312 chars + polyphones)
    final_entries = []
    for syl in sorted(buckets.keys()):
        seen = set()
        ordered = []
        for ch in PROMOTE.get(syl, ""):
            if ch not in seen:
                seen.add(ch)
                ordered.append(ch)
        for ch in buckets[syl]:
            if ch not in seen:
                seen.add(ch)
                ordered.append(ch)
        if ordered:
            final_entries.append((syl, "".join(ordered)))

    return final_entries


def c_escape_utf8(s: str) -> str:
    raw = s.encode("utf-8")
    return "".join(f"\\x{b:02x}" for b in raw)


def generate() -> str:
    entries = build_table()
    # Pack all UTF-8 strings into one contiguous byte pool for tiny ROM footprint
    pool = bytearray()
    meta = []
    for syl, chars in entries:
        off = len(pool)
        raw = chars.encode("utf-8")
        pool.extend(raw)
        meta.append((syl, off, len(chars)))

    lines = [
        "/*",
        " * SPDX-FileCopyrightText: 2026 pico_nano contributors",
        " * SPDX-License-Identifier: Apache-2.0",
        " * 自动生成：tools/gen_pinyin_ime.py由GB2312一级汉字（3755字）及常用字频/多音字前置生成，勿手改。",
        " * Generated by tools/gen_pinyin_ime.py from GB2312 level-1 hanzi (3755 chars) with frequency/polyphone promotion; do not hand-edit.",
        " */",
        "#pragma once",
        "#include <stdint.h>",
        f"#define PN_IME_SYLLABLE_COUNT {len(meta)}",
        f"#define PN_IME_POOL_BYTES {len(pool)}",
        "typedef struct {",
        "    char py[7];",
        "    uint16_t offset;",
        "    uint16_t count;",
        "} pn_ime_entry_t;",
        "static const pn_ime_entry_t pn_ime_entries[PN_IME_SYLLABLE_COUNT] = {",
    ]
    for syl, off, cnt in meta:
        lines.append(f'    {{"{syl}", {off}u, {cnt}u}},')
    lines.append("};")
    lines.append("static const uint8_t pn_ime_pool[PN_IME_POOL_BYTES] = {")
    for i in range(0, len(pool), 24):
        chunk = ",".join(f"0x{b:02x}" for b in pool[i:i + 24])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    text = generate()
    if args.verify:
        entries = build_table()
        d = dict(entries)
        print(f"Syllables: {len(entries)}, Total chars: {sum(len(v) for _, v in entries)}, Pool bytes: {sum(len(v.encode('utf-8')) for _, v in entries)}")
        # Spot-check mottos & common phrases
        for py, expected in [
            ("fu", "腹"), ("you", "有"), ("shi", "诗"), ("shu", "书"), ("qi", "气"), ("zi", "自"), ("hua", "华"),
            ("du", "读"), ("man", "慢"), ("jing", "静"), ("xin", "心"), ("meng", "梦"), ("ni", "你"), ("hao", "好"),
            ("wo", "我"), ("de", "的"), ("le", "了"), ("shi", "是"), ("zhong", "中"), ("lv", "绿"), ("nv", "女"),
            ("lue", "略"), ("lve", "略"), ("yue", "阅"), ("chang", "长"), ("he", "和"),
        ]:
            assert py in d and expected in d[py], f"Missing {expected} in {py}"
            pos = d[py].index(expected)
            print(f"  {py:6s} -> {d[py][:8]}  ('{expected}' at #{pos})")
        return 0
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf8") != text:
            print("pinyin IME generated table differs", file=sys.stderr)
            return 1
        print("pinyin IME generated table matches")
        return 0
    OUTPUT.write_text(text, encoding="utf8", newline="\n")
    print(f"Wrote {OUTPUT} ({len(text)} chars)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
